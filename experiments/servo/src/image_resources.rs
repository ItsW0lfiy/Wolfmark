//! Local-only, intercepted image delivery. No socket or file URL is exposed to Servo.
//! Decoding and PNG encoding run on a bounded pool using Wolfmark's shared decoder.
use crate::images::{self, ImageRequest};
use image::ImageEncoder;
use std::{
    cell::RefCell,
    collections::HashMap,
    path::{Path, PathBuf},
    sync::{
        Arc, OnceLock,
        atomic::{AtomicBool, AtomicU64, Ordering},
        mpsc,
    },
};
use url::Url;

const ORIGIN: &str = "wolfmark.invalid";
const BUDGET: usize = 128 * 1024 * 1024;
static NEXT_ID: AtomicU64 = AtomicU64::new(1);
static POOL: OnceLock<rayon::ThreadPool> = OnceLock::new();
struct Asset {
    path: PathBuf,
    bytes: Option<Arc<Vec<u8>>>,
    error: Option<String>,
    queued: bool,
    touched: u64,
    loads: Vec<servo::WebResourceLoad>,
}
struct Completed {
    id: u64,
    result: Result<Vec<u8>, String>,
}
pub struct Resources {
    assets: RefCell<HashMap<u64, Asset>>,
    sender: mpsc::Sender<Completed>,
    receiver: mpsc::Receiver<Completed>,
    alive: Arc<AtomicBool>,
    clock: std::cell::Cell<u64>,
}
impl Default for Resources {
    fn default() -> Self {
        let (sender, receiver) = mpsc::channel();
        Self {
            assets: RefCell::new(HashMap::new()),
            sender,
            receiver,
            alive: Arc::new(AtomicBool::new(true)),
            clock: std::cell::Cell::new(0),
        }
    }
}
impl Resources {
    pub fn register(&self, source: &str, document: Option<&Path>) -> Option<String> {
        let resolution = images::resolve_local_image(document?, source);
        if resolution.state != images::AssetState::Allowed {
            return None;
        }
        let path = resolution.canonical_path?;
        let mut assets = self.assets.borrow_mut();
        let id = assets
            .iter()
            .find_map(|(id, asset)| (asset.path == path).then_some(*id))
            .unwrap_or_else(|| {
                let id = NEXT_ID.fetch_add(1, Ordering::Relaxed);
                assets.insert(
                    id,
                    Asset {
                        path,
                        bytes: None,
                        error: None,
                        queued: false,
                        touched: 0,
                        loads: Vec::new(),
                    },
                );
                id
            });
        Some(format!("https://{ORIGIN}/asset/{id}"))
    }
    fn id(url: &Url) -> Option<u64> {
        if url.scheme() != "https"
            || url.host_str() != Some(ORIGIN)
            || url.port().is_some()
            || !url.username().is_empty()
            || url.password().is_some()
            || url.query().is_some()
            || url.fragment().is_some()
        {
            return None;
        }
        url.path().strip_prefix("/asset/")?.parse().ok()
    }
    pub fn contains(&self, url: &Url) -> bool {
        Self::id(url).is_some_and(|id| self.assets.borrow().contains_key(&id))
    }
    pub fn load(&self, load: servo::WebResourceLoad) {
        let id = Self::id(&load.request().url).expect("registered asset URL");
        let mut assets = self.assets.borrow_mut();
        let asset = assets.get_mut(&id).expect("registered asset");
        self.clock.set(self.clock.get() + 1);
        asset.touched = self.clock.get();
        if let Some(bytes) = &asset.bytes {
            respond(load, Ok(bytes));
            return;
        }
        if let Some(error) = &asset.error {
            respond(load, Err(error));
            return;
        }
        asset.loads.push(load);
        if asset.queued {
            return;
        }
        asset.queued = true;
        let path = asset.path.clone();
        let sender = self.sender.clone();
        let alive = self.alive.clone();
        POOL.get_or_init(|| {
            rayon::ThreadPoolBuilder::new()
                .num_threads(4)
                .thread_name(|i| format!("wolfmark-servo-image-{i}"))
                .build()
                .expect("image workers")
        })
        .spawn(move || {
            if !alive.load(Ordering::Acquire) {
                return;
            }
            let result = encode(&path);
            if alive.load(Ordering::Acquire) {
                let _ = sender.send(Completed { id, result });
            }
        });
    }
    pub fn poll(&self) -> bool {
        let mut assets = self.assets.borrow_mut();
        for completed in self.receiver.try_iter() {
            if let Some(asset) = assets.get_mut(&completed.id) {
                asset.queued = false;
                match completed.result {
                    Ok(bytes) => asset.bytes = Some(Arc::new(bytes)),
                    Err(error) => asset.error = Some(error),
                }
                for load in asset.loads.drain(..) {
                    respond(
                        load,
                        asset
                            .bytes
                            .as_ref()
                            .ok_or_else(|| asset.error.as_deref().unwrap_or("Decode failed")),
                    );
                }
            }
        }
        // Evict encoded resources by least recent use; registration survives so a
        // later request can safely requeue. Four decodes at most run concurrently.
        let mut cost: usize = assets
            .values()
            .filter_map(|a| a.bytes.as_ref())
            .map(|b| b.len())
            .sum();
        while cost > BUDGET {
            let Some(id) = assets
                .iter()
                .filter(|(_, a)| a.bytes.is_some())
                .min_by_key(|(_, a)| a.touched)
                .map(|(id, _)| *id)
            else {
                break;
            };
            cost -= assets.get_mut(&id).unwrap().bytes.take().unwrap().len();
        }
        assets.values().any(|a| a.queued)
    }
}
impl Drop for Resources {
    fn drop(&mut self) {
        self.alive.store(false, Ordering::Release);
        // Never drop an unintercepted load: that would permit a network fallback.
        for asset in self.assets.get_mut().values_mut() {
            for load in asset.loads.drain(..) {
                respond(load, Err("Document closed"));
            }
        }
    }
}
fn respond(load: servo::WebResourceLoad, result: Result<&Arc<Vec<u8>>, &str>) {
    let mut headers = http::HeaderMap::new();
    headers.insert(
        http::header::CONTENT_TYPE,
        http::HeaderValue::from_static("image/png"),
    );
    let response = embedder_traits::WebResourceResponse::new(load.request().url.clone())
        .headers(headers)
        .status_code(if result.is_ok() {
            http::StatusCode::OK
        } else {
            http::StatusCode::UNPROCESSABLE_ENTITY
        });
    let mut stream = load.intercept(response);
    if let Ok(bytes) = result {
        stream.send_body_data(bytes.as_ref().clone());
    }
    stream.finish();
}
fn encode(path: &Path) -> Result<Vec<u8>, String> {
    let result = images::decode_image(&ImageRequest {
        id: 0,
        path: path.to_string_lossy().into_owned(),
        max_width: 4096,
        intrinsic_width: 0,
        intrinsic_height: 0,
    });
    if !result.error.is_empty() {
        return Err(result.error);
    }
    let mut bytes = Vec::new();
    image::codecs::png::PngEncoder::new(&mut bytes)
        .write_image(
            &result.rgba,
            result.width,
            result.height,
            image::ExtendedColorType::Rgba8,
        )
        .map_err(|e| e.to_string())?;
    Ok(bytes)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn namespace_is_exact_and_unknown_ids_are_rejected() {
        let resources = Resources::default();
        for value in [
            "https://wolfmark.invalid/asset/1",
            "https://evil.invalid/asset/1",
            "http://wolfmark.invalid/asset/1",
            "https://wolfmark.invalid/asset/1?path=x",
        ] {
            assert!(!resources.contains(&Url::parse(value).unwrap()));
        }
    }
    #[test]
    fn shared_decoder_rejects_corruption_and_rasterizes_svg() {
        let root =
            std::env::temp_dir().join(format!("wolfmark-servo-decode-{}", std::process::id()));
        std::fs::create_dir_all(&root).unwrap();
        let path = root.join("scene.svg");
        std::fs::write(&path, r##"<svg xmlns="http://www.w3.org/2000/svg" width="16" height="8"><rect width="16" height="8" fill="#a82e51"/></svg>"##).unwrap();
        let bytes = encode(&path).unwrap();
        let image = image::load_from_memory(&bytes).unwrap();
        assert_eq!((image.width(), image.height()), (16, 8));
        std::fs::write(&path, "broken image").unwrap();
        assert!(encode(&path).is_err());
        std::fs::remove_dir_all(&root).unwrap();
    }
}
