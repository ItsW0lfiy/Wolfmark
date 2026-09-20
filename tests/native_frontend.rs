#![cfg(target_os = "windows")]

use std::path::PathBuf;
use std::process::Command;
use std::{io::Read, io::Write, net::TcpListener, thread};

fn fixture(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("fixtures")
        .join(name)
}

fn run_smoke(mode: &str, fixture_name: &str) -> String {
    run_smoke_many(mode, &[fixture_name])
}

fn run_smoke_many(mode: &str, fixture_names: &[&str]) -> String {
    let fixtures = fixture_names
        .iter()
        .map(|name| fixture(name))
        .collect::<Vec<_>>();
    let output = Command::new(env!("CARGO_BIN_EXE_wolfmark"))
        .arg(mode)
        .args(fixtures)
        .current_dir(env!("CARGO_MANIFEST_DIR"))
        .output()
        .expect("launch Wolfmark native smoke test");
    assert!(
        output.status.success(),
        "Wolfmark smoke failed ({:?}): {}{}",
        output.status.code(),
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    String::from_utf8_lossy(&output.stdout).into_owned()
}

fn numeric_metric(output: &str, name: &str) -> u64 {
    output
        .split_whitespace()
        .find_map(|field| {
            field
                .strip_prefix(&format!("{name}="))
                .and_then(|value| value.parse().ok())
        })
        .unwrap_or_else(|| panic!("missing numeric metric {name} in {output}"))
}

fn run_startup_arguments_smoke(arguments: &[PathBuf]) -> String {
    let output = Command::new(env!("CARGO_BIN_EXE_wolfmark"))
        .arg("--smoke-startup-arguments")
        .args(arguments)
        .current_dir(env!("CARGO_MANIFEST_DIR"))
        .output()
        .expect("launch Wolfmark startup-argument smoke test");
    assert!(
        output.status.success(),
        "Wolfmark startup-argument smoke failed ({:?}): {}{}",
        output.status.code(),
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    String::from_utf8_lossy(&output.stdout).into_owned()
}

struct GeneratedTestDirectory(PathBuf);

impl GeneratedTestDirectory {
    fn recreate(path: PathBuf) -> Self {
        if path.exists() {
            std::fs::remove_dir_all(&path).expect("remove stale generated test directory");
        }
        std::fs::create_dir_all(&path).expect("create generated test directory");
        Self(path)
    }

    fn path(&self) -> &std::path::Path {
        &self.0
    }
}

impl Drop for GeneratedTestDirectory {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

#[test]
fn shell_startup_arguments_open_supported_documents_once() {
    let generated = GeneratedTestDirectory::recreate(
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("out")
            .join("tests")
            .join("wolfmark-native-tests")
            .join("startup-arguments"),
    );
    let root = generated.path();
    let nested = root.join("deeply nested");
    std::fs::create_dir_all(&nested).expect("create startup-argument test directory");

    let markdown = root.join("README.md");
    let spaced = root.join("My Document.markdown");
    let unicode = root.join("日本語 document.txt");
    let deep = nested.join("notes.md");
    let unsupported = root.join("unsupported.pdf");
    for (path, contents) in [
        (&markdown, "# Markdown\n"),
        (&spaced, "# Spaced Markdown\n"),
        (&unicode, "literal Unicode text\n"),
        (&deep, "# Deep document\n"),
        (&unsupported, "not a supported document\n"),
    ] {
        std::fs::write(path, contents).expect("write startup-argument fixture");
    }

    let empty = run_startup_arguments_smoke(&[]);
    assert!(empty.contains("documents=0"), "{empty}");

    let relative_markdown = markdown
        .strip_prefix(env!("CARGO_MANIFEST_DIR"))
        .expect("fixture is below manifest directory")
        .to_path_buf();
    let duplicate = nested.join("..").join("README.md");
    let missing = root.join("missing.md");
    let output = run_startup_arguments_smoke(&[
        relative_markdown,
        markdown.clone(),
        duplicate,
        spaced.clone(),
        unicode.clone(),
        deep.clone(),
        unsupported,
        missing,
    ]);
    assert!(output.contains("startup_arguments=ok"), "{output}");
    assert!(output.contains("documents=4"), "{output}");
    for name in [
        "README.md",
        "My Document.markdown",
        "日本語 document.txt",
        "notes.md",
    ] {
        assert!(output.contains(name), "missing {name} in {output}");
    }
}

#[test]
fn native_settings_migrate_and_recover_safely() {
    let output = Command::new(env!("CARGO_BIN_EXE_wolfmark"))
        .arg("--smoke-settings")
        .current_dir(env!("CARGO_MANIFEST_DIR"))
        .output()
        .expect("launch Wolfmark settings smoke test");
    assert!(
        output.status.success(),
        "Wolfmark settings smoke failed ({:?}): {}{}",
        output.status.code(),
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("settings=ok"), "{stdout}");
    assert!(stdout.contains("migration=ok"), "{stdout}");
    assert!(stdout.contains("round_trip=ok"), "{stdout}");
    assert!(stdout.contains("malformed=recovered"), "{stdout}");
    assert!(stdout.contains("atomic=ok"), "{stdout}");
}

#[test]
fn native_update_transport_uses_etag_cache_and_fails_safely() {
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind update fixture server");
    let endpoint = format!("http://{}/releases", listener.local_addr().unwrap());
    let server = thread::spawn(move || {
        let release_json = serde_json::json!([{
            "draft": false,
            "prerelease": true,
            "tag_name": "0.1.0-dev.8",
            "name": "Wolfmark dev.8 fixture",
            "body": "Fixture release",
            "html_url": "https://github.com/ItsW0lfiy/Moonmark/releases/tag/0.1.0-dev.8",
            "assets": []
        }])
        .to_string();
        for request_index in 0..2 {
            let (mut stream, _) = listener.accept().expect("accept update fixture request");
            let mut request = Vec::new();
            let mut buffer = [0_u8; 1024];
            while !request.windows(4).any(|window| window == b"\r\n\r\n") {
                let count = stream
                    .read(&mut buffer)
                    .expect("read update fixture request");
                if count == 0 {
                    break;
                }
                request.extend_from_slice(&buffer[..count]);
            }
            let request = String::from_utf8_lossy(&request).to_ascii_lowercase();
            assert!(
                request.contains("user-agent: wolfmark/0.1.0-dev.7"),
                "{request}"
            );
            if request_index == 0 {
                let response = format!(
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nETag: \"wolfmark-test\"\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
                    release_json.len(),
                    release_json
                );
                stream
                    .write_all(response.as_bytes())
                    .expect("write update response");
            } else {
                assert!(
                    request.contains("if-none-match: \"wolfmark-test\""),
                    "{request}"
                );
                stream.write_all(
                    b"HTTP/1.1 304 Not Modified\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
                ).expect("write update not-modified response");
            }
        }
    });

    let output = Command::new(env!("CARGO_BIN_EXE_wolfmark"))
        .arg("--smoke-updates")
        .env("WOLFMARK_UPDATE_TEST_ENDPOINT", endpoint)
        .current_dir(env!("CARGO_MANIFEST_DIR"))
        .output()
        .expect("launch Wolfmark update transport smoke test");
    server.join().expect("join update fixture server");
    assert!(
        output.status.success(),
        "Wolfmark update smoke failed ({:?}): {}{}",
        output.status.code(),
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("updates=ok"), "{stdout}");
    assert!(stdout.contains("cache_304=ok"), "{stdout}");
    assert!(stdout.contains("http_failure=safe"), "{stdout}");
}

#[test]
fn outline_navigation_survives_image_reflow_with_one_click() {
    for fixture_name in [
        "outline-reflow-single.md",
        "outline-reflow-multiple.md",
        "outline-reflow-missing.md",
        "outline-reflow-failed.md",
        "outline-reflow-bottom.md",
        "outline-reflow-rapid.md",
    ] {
        let output = run_smoke("--smoke-outline-reflow", fixture_name);
        assert!(
            output.contains("outline_reflow=ok"),
            "{fixture_name}: {output}"
        );
        assert!(output.contains("one_click=ok"), "{fixture_name}: {output}");
        assert!(
            output.contains("latest_wins=ok"),
            "{fixture_name}: {output}"
        );
        assert!(
            output.contains("counters=stable"),
            "{fixture_name}: {output}"
        );
        assert!(
            output.contains("motion=stopped"),
            "{fixture_name}: {output}"
        );
    }
}

#[test]
fn native_window_exposes_windows_caption_and_resize_semantics() {
    let output = run_smoke("--smoke-native-window", "layout-transitions.md");
    assert!(output.contains("native_window=ok"), "{output}");
    assert!(output.contains("thick_frame=yes"), "{output}");
    assert!(output.contains("caption_hit=ok"), "{output}");
    assert!(output.contains("max_hit=ok"), "{output}");
    assert!(output.contains("min_client=ok"), "{output}");
    assert!(output.contains("close_client=ok"), "{output}");
    assert!(output.contains("resize_hits=ok"), "{output}");
    assert!(output.contains("buttons=ok"), "{output}");
    assert!(output.contains("counters=stable"), "{output}");
}

#[test]
fn middle_autoscroll_indicator_tracks_its_full_lifecycle() {
    let output = run_smoke("--smoke-autoscroll-anchor", "layout-transitions.md");
    assert!(output.contains("autoscroll_anchor=ok"), "{output}");
    assert!(output.contains("lifecycle=ok"), "{output}");
    assert!(output.contains("cursor=neutral"), "{output}");
    assert!(output.contains("pinned=ok"), "{output}");
    assert!(output.contains("counters=stable"), "{output}");
}

#[test]
fn literal_space_image_destinations_render_through_the_native_pipeline() {
    let output = run_smoke("--smoke-images", "literal-space-destinations.md");
    assert!(output.contains("images=ok"), "{output}");
    assert!(output.contains("discovered=4"), "{output}");
    assert!(output.contains("loaded=4"), "{output}");
    assert!(output.contains("failed=0"), "{output}");
    assert!(output.contains("requests=1"), "{output}");
}

#[test]
fn practical_markdown_profile_renders_through_the_native_document() {
    let output = run_smoke("--smoke-render", "markdown-compatibility.md");
    assert!(output.contains("render=ok"), "{output}");
    assert!(output.contains("selection_copy=ok"), "{output}");
}

#[test]
fn qt_frontend_smoke_matrix() {
    let output = run_smoke("--smoke-image-geometry", "image-layout-regression.md");
    assert!(output.contains("images=ok"), "{output}");
    let output = run_smoke("--smoke-render", "inline-code-quality.md");
    assert!(output.contains("selection_copy=ok"), "{output}");
    let output = run_smoke("--smoke-navigation", "concept-presentation.md");
    assert!(output.contains("navigation=ok"), "{output}");
    let output = run_smoke("--smoke-motion", "navigation-motion.md");
    assert!(output.contains("motion=ok"), "{output}");
    assert!(output.contains("counters=stable"), "{output}");
    assert!(output.contains("partial_wheel=ok"), "{output}");
    assert!(output.contains("cancelled=ok"), "{output}");
    assert!(
        output.contains("pixel=direct home=direct end=direct"),
        "{output}"
    );

    let output = run_smoke("--smoke-scroll-profile", "generated/large-text.md");
    assert!(output.contains("scroll_profile=ok"), "{output}");
    assert!(output.contains("paint_interval_ms_p50="), "{output}");
    assert!(output.contains("over_50="), "{output}");
    let controller_frames = numeric_metric(&output, "controller_frames");
    let image_scans = numeric_metric(&output, "image_scans");
    assert!(controller_frames > 20, "{output}");
    assert!(image_scans * 2 < controller_frames, "{output}");

    let output = run_smoke("--smoke-plaintext", "text/literal.txt");
    assert!(output.contains("plaintext=ok"), "{output}");
    assert!(output.contains("parse_count=0"), "{output}");

    let output = run_smoke_many(
        "--smoke-multidoc",
        &["code-block-quality.md", "text/literal.txt"],
    );
    assert!(output.contains("multidoc=ok"), "{output}");
    assert!(output.contains("duplicate=deduplicated"), "{output}");
    assert!(output.contains("state=retained"), "{output}");
    assert!(output.contains("counters=stable"), "{output}");
    let output = run_smoke("--smoke-zoom", "wolfmark-visual-test.md");
    assert!(output.contains("zoom=ok"), "{output}");
    assert!(output.contains("image_request_delta=0"), "{output}");

    let output = run_smoke("--smoke-render", "code-block-quality.md");
    assert!(output.contains("render=ok"), "{output}");
    assert!(output.contains("selection_copy=ok"), "{output}");
    assert!(output.contains("code_copy=ok"), "{output}");

    let output = run_smoke("--smoke-style", "document-tables.md");
    assert!(output.contains("document_style=ok"), "{output}");

    let output = run_smoke("--smoke-layout", "layout-transitions.md");
    assert!(output.contains("layout=ok"), "{output}");
    assert!(output.contains("parse_delta=0"), "{output}");
    assert!(output.contains("load_delta=0"), "{output}");
    assert!(output.contains("construction_delta=0"), "{output}");
    assert!(output.contains("image_request_delta=0"), "{output}");

    let output = run_smoke("--smoke-maximize", "layout-transitions.md");
    assert!(output.contains("maximize=ok"), "{output}");
    assert!(output.contains("geometry=restored"), "{output}");

    let output = run_smoke("--smoke-layout-normal", "layout-transitions.md");
    assert!(output.contains("layout_normal=ok"), "{output}");
    assert!(output.contains("geometry=restored"), "{output}");
    let output = run_smoke("--smoke-images", "wolfmark-visual-test.md");
    assert!(output.contains("images=ok"), "{output}");
    assert!(output.contains("failed=0"), "{output}");
    assert!(output.contains("pending=0"), "{output}");

    let watcher_root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("out")
        .join("tests")
        .join("wolfmark-native-tests");
    std::fs::create_dir_all(&watcher_root).expect("create native test directory");
    let watcher_fixture = watcher_root.join("watcher.md");
    std::fs::write(&watcher_fixture, "# Watcher fixture\n\nOriginal content.\n")
        .expect("write watcher fixture");
    let output = Command::new(env!("CARGO_BIN_EXE_wolfmark"))
        .arg("--smoke-watcher")
        .arg(&watcher_fixture)
        .current_dir(env!("CARGO_MANIFEST_DIR"))
        .output()
        .expect("launch Wolfmark watcher smoke test");
    assert!(
        output.status.success(),
        "{}{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("watcher=ok"), "{stdout}");
    std::fs::remove_file(watcher_fixture).expect("remove watcher fixture");
}

#[test]
fn qt_background_text_watcher_updates_only_its_session() {
    let watcher_root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("out")
        .join("tests")
        .join("wolfmark-native-tests");
    std::fs::create_dir_all(&watcher_root).expect("create native test directory");
    let active_fixture = watcher_root.join("active.md");
    let background_fixture = watcher_root.join("background.txt");
    std::fs::write(
        &active_fixture,
        "# Active fixture\n\nDo not reload this document.\n",
    )
    .expect("write active watcher fixture");
    std::fs::write(&background_fixture, "Literal **background** text.\n")
        .expect("write background watcher fixture");
    let output = Command::new(env!("CARGO_BIN_EXE_wolfmark"))
        .arg("--smoke-multidoc-watcher")
        .arg(&active_fixture)
        .arg(&background_fixture)
        .current_dir(env!("CARGO_MANIFEST_DIR"))
        .output()
        .expect("launch Wolfmark background watcher smoke test");
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        output.status.success(),
        "Wolfmark background watcher smoke failed\nstdout:\n{stdout}\nstderr:\n{stderr}"
    );
    assert!(stdout.contains("multidoc_watcher=ok"), "{stdout}");
    assert!(stdout.contains("active=stable"), "{stdout}");
    assert!(stdout.contains("background_parse_delta=0"), "{stdout}");
    std::fs::remove_file(active_fixture).expect("remove active watcher fixture");
    std::fs::remove_file(background_fixture).expect("remove background watcher fixture");
}
