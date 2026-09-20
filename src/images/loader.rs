use std::collections::HashSet;
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, OnceLock};

use crossbeam_channel::{Receiver, Sender, unbounded};
use image::imageops::FilterType;
use rayon::ThreadPool;

use super::{ImageRequest, ImageResult};

use super::cache::{CachedImage, ImageCache};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ImageDimensions {
    pub width: u32,
    pub height: u32,
}

pub fn probe_dimensions(path: &Path) -> Result<ImageDimensions, String> {
    let (width, height) = if path
        .extension()
        .is_some_and(|extension| extension.eq_ignore_ascii_case("svg"))
    {
        const MAX_SVG_BYTES: u64 = 32 * 1024 * 1024;
        let metadata = std::fs::metadata(path).map_err(|error| error.to_string())?;
        if metadata.len() > MAX_SVG_BYTES {
            return Err("SVG source exceeds the safety limit".into());
        }
        let data = std::fs::read(path).map_err(|error| error.to_string())?;
        let tree = resvg::usvg::Tree::from_data(&data, &svg_options())
            .map_err(|error| format!("Invalid SVG: {error}"))?;
        let size = tree.size().to_int_size();
        (size.width(), size.height())
    } else {
        let reader = image::ImageReader::open(path)
            .and_then(image::ImageReader::with_guessed_format)
            .map_err(|error| error.to_string())?;
        reader
            .into_dimensions()
            .map_err(|error| error.to_string())?
    };
    if unsafe_dimensions(width, height) {
        return Err("Image dimensions exceed the safety limit".into());
    }
    Ok(ImageDimensions { width, height })
}

pub struct ImagePipeline {
    pool: ThreadPool,
    sender: Sender<ImageResult>,
    receiver: Receiver<ImageResult>,
    cache: Arc<Mutex<ImageCache>>,
    queued: HashSet<String>,
    generation: Arc<AtomicU64>,
}

impl ImagePipeline {
    pub fn new(workers: usize, budget: usize) -> Self {
        let (sender, receiver) = unbounded();
        Self {
            pool: rayon::ThreadPoolBuilder::new()
                .num_threads(workers.clamp(1, 8))
                .thread_name(|index| format!("wolfmark-image-{index}"))
                .build()
                .expect("bounded Wolfmark image pool"),
            sender,
            receiver,
            cache: Arc::new(Mutex::new(ImageCache::new(budget))),
            queued: HashSet::new(),
            generation: Arc::new(AtomicU64::new(0)),
        }
    }

    pub fn queue(&mut self, request: ImageRequest) {
        let key = format!("{}@{}", request.path, request.max_width);
        if !self.queued.insert(key.clone()) {
            return;
        }
        if let Some(cached) = self.cache.lock().expect("image cache").get(&key) {
            let _ = self.sender.send(success(request.id, cached));
            return;
        }
        let sender = self.sender.clone();
        let cache = Arc::clone(&self.cache);
        let generation = Arc::clone(&self.generation);
        let request_generation = generation.load(Ordering::Acquire);
        let profile = std::env::var_os("WOLFMARK_PROFILE").is_some();
        let queued_at = std::time::Instant::now();
        self.pool.spawn(move || {
            if generation.load(Ordering::Acquire) != request_generation {
                return;
            }
            let started = std::time::Instant::now();
            let result = decode(&request);
            if profile {
                eprintln!(
                    "IMAGE_WORKER id={} queue_us={} decode_us={}",
                    request.id,
                    started.duration_since(queued_at).as_micros(),
                    started.elapsed().as_micros()
                );
            }
            if generation.load(Ordering::Acquire) != request_generation {
                return;
            }
            if result.error.is_empty() {
                cache.lock().expect("image cache").insert(
                    key,
                    CachedImage {
                        width: result.width,
                        height: result.height,
                        rgba: result.rgba.clone(),
                    },
                );
            }
            let _ = sender.send(result);
        });
    }

    pub fn poll(&self) -> Vec<ImageResult> {
        self.receiver.try_iter().collect()
    }

    pub fn poll_one(&self) -> Option<ImageResult> {
        self.receiver.try_recv().ok()
    }

    pub fn reset_requests(&mut self) {
        self.generation.fetch_add(1, Ordering::AcqRel);
        self.queued.clear();
        while self.receiver.try_recv().is_ok() {}
    }

    pub fn cache_cost(&self) -> u64 {
        self.cache.lock().expect("image cache").cost() as u64
    }

    pub fn request_count(&self) -> u64 {
        self.queued.len() as u64
    }
}

impl Drop for ImagePipeline {
    fn drop(&mut self) {
        self.generation.fetch_add(1, Ordering::AcqRel);
    }
}

fn decode(request: &ImageRequest) -> ImageResult {
    if Path::new(&request.path)
        .extension()
        .is_some_and(|extension| extension.eq_ignore_ascii_case("svg"))
    {
        return decode_svg(request);
    }

    if (request.intrinsic_width == 0 || request.intrinsic_height == 0)
        && let Err(error) = probe_dimensions(Path::new(&request.path))
    {
        return failure(request.id, error);
    }
    let reader = match image::ImageReader::open(&request.path)
        .and_then(image::ImageReader::with_guessed_format)
    {
        Ok(reader) => reader,
        Err(error) => return failure(request.id, error.to_string()),
    };
    let image = match reader.decode() {
        Ok(image) => image,
        Err(error) => return failure(request.id, error.to_string()),
    };
    let max_width = request.max_width.clamp(64, 4096);
    let scaled = if image.width() > max_width {
        image.resize(max_width, u32::MAX, FilterType::Triangle)
    } else {
        image
    };
    let rgba = scaled.to_rgba8();
    ImageResult {
        id: request.id,
        width: rgba.width(),
        height: rgba.height(),
        rgba: rgba.into_raw(),
        error: String::new(),
    }
}

fn decode_svg(request: &ImageRequest) -> ImageResult {
    const MAX_SVG_BYTES: u64 = 32 * 1024 * 1024;
    let metadata = match std::fs::metadata(&request.path) {
        Ok(metadata) => metadata,
        Err(error) => return failure(request.id, error.to_string()),
    };
    if metadata.len() > MAX_SVG_BYTES {
        return failure(request.id, "SVG source exceeds the safety limit".into());
    }
    let data = match std::fs::read(&request.path) {
        Ok(data) => data,
        Err(error) => return failure(request.id, error.to_string()),
    };
    let options = svg_options();
    let tree = match resvg::usvg::Tree::from_data(&data, &options) {
        Ok(tree) => tree,
        Err(error) => return failure(request.id, format!("Invalid SVG: {error}")),
    };
    let source_size = tree.size().to_int_size();
    if unsafe_dimensions(source_size.width(), source_size.height()) {
        return failure(
            request.id,
            "Image dimensions exceed the safety limit".into(),
        );
    }
    let width = source_size.width().min(request.max_width.clamp(64, 4096));
    let scale = width as f32 / source_size.width() as f32;
    let height = ((source_size.height() as f32 * scale).ceil() as u32).max(1);
    let Some(mut pixmap) = resvg::tiny_skia::Pixmap::new(width, height) else {
        return failure(request.id, "SVG display size is invalid".into());
    };
    resvg::render(
        &tree,
        resvg::tiny_skia::Transform::from_scale(scale, scale),
        &mut pixmap.as_mut(),
    );
    let mut rgba = pixmap.take();
    unpremultiply_rgba(&mut rgba);
    ImageResult {
        id: request.id,
        width,
        height,
        rgba,
        error: String::new(),
    }
}

fn svg_options() -> resvg::usvg::Options<'static> {
    static FONTS: OnceLock<Arc<resvg::usvg::fontdb::Database>> = OnceLock::new();
    let mut options = resvg::usvg::Options {
        fontdb: Arc::clone(FONTS.get_or_init(|| {
            let mut database = resvg::usvg::fontdb::Database::new();
            database.load_system_fonts();
            Arc::new(database)
        })),
        ..resvg::usvg::Options::default()
    };
    // SVGs may contain data-URI resources, but may not use an href to read another
    // local file. This keeps SVG rendering inside the document's approved asset.
    options.image_href_resolver.resolve_string = Box::new(|_, _| None);
    options
}

fn unsafe_dimensions(width: u32, height: u32) -> bool {
    width == 0 || height == 0 || u64::from(width) * u64::from(height) > 200_000_000
}

fn unpremultiply_rgba(rgba: &mut [u8]) {
    let (pixels, _) = rgba.as_chunks_mut::<4>();
    for pixel in pixels {
        let alpha = u16::from(pixel[3]);
        if alpha == 0 || alpha == 255 {
            continue;
        }
        for channel in &mut pixel[..3] {
            *channel = ((u16::from(*channel) * 255 + alpha / 2) / alpha).min(255) as u8;
        }
    }
}

fn success(id: u32, image: CachedImage) -> ImageResult {
    ImageResult {
        id,
        width: image.width,
        height: image.height,
        rgba: image.rgba,
        error: String::new(),
    }
}

fn failure(id: u32, error: String) -> ImageResult {
    ImageResult {
        id,
        width: 0,
        height: 0,
        rgba: Vec::new(),
        error,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn probes_raster_dimensions_without_decoding_pixels() {
        let directory =
            std::env::temp_dir().join(format!("wolfmark-image-dimensions-{}", std::process::id()));
        std::fs::create_dir_all(&directory).expect("create dimensions test directory");
        let path = directory.join("image with spaces.png");
        image::RgbaImage::new(640, 360)
            .save(&path)
            .expect("write dimensions fixture");

        assert_eq!(
            probe_dimensions(&path),
            Ok(ImageDimensions {
                width: 640,
                height: 360
            })
        );
        let _ = std::fs::remove_dir_all(directory);
    }

    fn temporary_svg(name: &str, source: &str) -> std::path::PathBuf {
        let directory =
            std::env::temp_dir().join(format!("wolfmark-svg-test-{}-{}", std::process::id(), name));
        std::fs::create_dir_all(&directory).expect("create SVG test directory");
        let path = directory.join("fixture.svg");
        std::fs::write(&path, source).expect("write SVG fixture");
        path
    }

    #[test]
    fn renders_svg_to_bounded_rgba() {
        let path = temporary_svg(
            "valid",
            r##"<svg xmlns="http://www.w3.org/2000/svg" width="320" height="160"><rect width="320" height="160" fill="#888"/></svg>"##,
        );
        let result = decode(&ImageRequest {
            id: 7,
            path: path.to_string_lossy().into_owned(),
            max_width: 120,
            intrinsic_width: 320,
            intrinsic_height: 160,
        });
        assert!(result.error.is_empty(), "{}", result.error);
        assert_eq!((result.width, result.height), (120, 60));
        assert_eq!(result.rgba.len(), 120 * 60 * 4);
        let _ = std::fs::remove_dir_all(path.parent().expect("SVG parent"));
    }

    #[test]
    fn rejects_svg_dimensions_before_allocating_the_surface() {
        let path = temporary_svg(
            "huge",
            r#"<svg xmlns="http://www.w3.org/2000/svg" width="50000" height="50000"/>"#,
        );
        let result = decode(&ImageRequest {
            id: 9,
            path: path.to_string_lossy().into_owned(),
            max_width: 120,
            intrinsic_width: 50_000,
            intrinsic_height: 50_000,
        });
        assert!(result.error.contains("dimensions exceed"));
        let _ = std::fs::remove_dir_all(path.parent().expect("SVG parent"));
    }
}
