use std::path::Path;
use std::time::Instant;

use wolfmark::presentation::PresentationMetrics;
use wolfmark::settings::Settings;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "fixtures/generated/image-stress-250.md".into());
    let source = std::fs::read_to_string(Path::new(&path))?;
    let document_path = Path::new(&path);
    let settings = Settings::default();
    let cold_started = Instant::now();
    let cold_model = wolfmark::markdown::parse(&source);
    let cold_parse_ms = cold_started.elapsed().as_secs_f64() * 1000.0;
    let cold_render_started = Instant::now();
    let _ = wolfmark::markdown::to_presentation(
        &cold_model,
        document_path,
        PresentationMetrics {
            revision: 1,
            source_bytes: source.len() as u64,
            ..PresentationMetrics::default()
        },
        &settings,
    );
    let cold_render_ms = cold_render_started.elapsed().as_secs_f64() * 1000.0;
    let mut parse_timings = Vec::with_capacity(20);
    let mut render_timings = Vec::with_capacity(20);
    let mut blocks = 0;
    for _ in 0..20 {
        let started = Instant::now();
        let model = wolfmark::markdown::parse(&source);
        blocks = model.blocks.len();
        parse_timings.push(started.elapsed().as_secs_f64() * 1000.0);
        let render_started = Instant::now();
        let _ = wolfmark::markdown::to_presentation(
            &model,
            document_path,
            PresentationMetrics {
                revision: 1,
                source_bytes: source.len() as u64,
                ..PresentationMetrics::default()
            },
            &settings,
        );
        render_timings.push(render_started.elapsed().as_secs_f64() * 1000.0);
    }
    parse_timings.sort_by(f64::total_cmp);
    render_timings.sort_by(f64::total_cmp);
    println!("Wolfmark Rust semantic benchmark");
    println!(
        "source_bytes={} top_level_blocks={} runs=20",
        source.len(),
        blocks
    );
    println!(
        "parse_p50_ms={:.3} parse_p95_ms={:.3}",
        parse_timings[10], parse_timings[19]
    );
    println!(
        "presentation_p50_ms={:.3} presentation_p95_ms={:.3} cold_parse_ms={cold_parse_ms:.3} cold_presentation_ms={cold_render_ms:.3}",
        render_timings[10], render_timings[19]
    );
    Ok(())
}
