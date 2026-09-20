use std::fmt::Write as _;
use std::path::{Path, PathBuf};

use image::{DynamicImage, ImageFormat, Rgba, RgbaImage};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("fixtures/generated");
    let images = root.join("images");
    std::fs::create_dir_all(&images)?;

    write_raster(&images.join("small moon.png"), 640, 360, ImageFormat::Png)?;
    write_raster(&images.join("unicode-月.jpg"), 960, 540, ImageFormat::Jpeg)?;
    write_raster(&images.join("sample.webp"), 1280, 720, ImageFormat::WebP)?;
    write_raster(&images.join("first-frame.gif"), 480, 270, ImageFormat::Gif)?;
    write_raster(&images.join("large-4096.png"), 4096, 2304, ImageFormat::Png)?;
    std::fs::write(
        images.join("vector.svg"),
        r##"<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="500"><rect width="1200" height="500" fill="#171717"/><circle cx="600" cy="250" r="190" fill="#d0d0d0"/><circle cx="680" cy="190" r="190" fill="#171717"/></svg>"##,
    )?;

    let references = [
        "images/small%20moon.png",
        "images/unicode-%E6%9C%88.jpg",
        "images/sample.webp",
        "images/first-frame.gif",
        "images/vector.svg",
        "images/large-4096.png",
    ];
    let mut markdown = String::from(
        "# Wolfmark 250-image stress fixture\n\nGenerated deterministically. Images intentionally repeat to exercise decode deduplication.\n\n",
    );
    for group in 0..25 {
        writeln!(markdown, "## Image group {}\n", group + 1)?;
        writeln!(
            markdown,
            "Paragraph before group {} keeps text readable while images decode.\n",
            group + 1
        )?;
        markdown.push_str("- compact list item\n  - nested list item\n\n");
        markdown.push_str("| Group | State |\n|---:|:---|\n");
        writeln!(markdown, "| {} | generated |\n", group + 1)?;
        markdown.push_str("```rust\nlet renderer = \"Wolfmark\";\n```\n\n");
        for item in 0..10 {
            let reference = references[(group * 10 + item) % references.len()];
            writeln!(
                markdown,
                "![Generated image {}-{}]({reference})\n",
                group + 1,
                item + 1
            )?;
        }
        if group % 6 == 0 {
            writeln!(
                markdown,
                "![Intentionally missing](images/missing-{}.png)\n",
                group
            )?;
        }
    }
    std::fs::write(root.join("image-stress.md"), markdown)?;

    let mut large_text = String::from("# Wolfmark 10,000-block fixture\n\n");
    for index in 0..10_000 {
        writeln!(
            large_text,
            "Paragraph {index} contains **strong text**, *emphasis*, and `inline code`.\n"
        )?;
    }
    std::fs::write(root.join("large-text.md"), large_text)?;
    println!("Generated fixtures in {}", root.display());
    Ok(())
}

fn write_raster(
    path: &Path,
    width: u32,
    height: u32,
    format: ImageFormat,
) -> Result<(), image::ImageError> {
    let mut image = RgbaImage::new(width, height);
    for (x, y, pixel) in image.enumerate_pixels_mut() {
        let checker = ((x / 64) + (y / 64)) % 2;
        let shade = if checker == 0 { 28 } else { 52 };
        *pixel = Rgba([shade, shade, shade, 255]);
    }
    const SILVER: Rgba<u8> = Rgba([210, 210, 210, 255]);
    const SHADOW: Rgba<u8> = Rgba([28, 28, 28, 255]);
    let radius = (height.min(width) / 3) as i64;
    let center_x = (width / 2) as i64;
    let center_y = (height / 2) as i64;
    for y in 0..height {
        for x in 0..width {
            let dx = x as i64 - center_x;
            let dy = y as i64 - center_y;
            if dx * dx + dy * dy <= radius * radius {
                image.put_pixel(x, y, SILVER);
            }
            let shadow_dx = x as i64 - (center_x + radius / 3);
            if shadow_dx * shadow_dx + dy * dy <= radius * radius {
                image.put_pixel(x, y, SHADOW);
            }
        }
    }
    DynamicImage::ImageRgba8(image).save_with_format(path, format)
}
