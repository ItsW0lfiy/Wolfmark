//! Offline package writing for document exports. No filesystem or process access.
use std::collections::HashSet;
use std::io::{Cursor, Write};

use base64::{Engine, prelude::BASE64_STANDARD};
use serde::Deserialize;
use zip::{CompressionMethod, ZipWriter, write::SimpleFileOptions};

#[derive(Deserialize)]
struct Part {
    name: String,
    data: String,
}

pub fn package(input: &[u8]) -> Result<Vec<u8>, String> {
    if input.len() > 384 * 1024 * 1024 {
        return Err("Document package exceeds the 384 MiB input limit".into());
    }
    let parts: Vec<Part> = serde_json::from_slice(input).map_err(|e| e.to_string())?;
    let mut names = HashSet::new();
    let mut writer = ZipWriter::new(Cursor::new(Vec::new()));
    let options = SimpleFileOptions::default().compression_method(CompressionMethod::Stored);
    for part in parts {
        if part.name.is_empty()
            || part.name.contains(['\\', ':'])
            || part
                .name
                .split('/')
                .any(|p| p.is_empty() || p == ".." || p == ".")
            || !names.insert(part.name.clone())
        {
            return Err("Invalid or duplicate document package part".into());
        }
        let data = BASE64_STANDARD
            .decode(part.data)
            .map_err(|e| e.to_string())?;
        writer
            .start_file(part.name, options)
            .map_err(|e| e.to_string())?;
        writer.write_all(&data).map_err(|e| e.to_string())?;
    }
    writer
        .finish()
        .map(|v| v.into_inner())
        .map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Read;

    #[test]
    fn package_is_a_readable_zip_with_exact_unicode_xml_and_binary_parts() {
        let xml = "<w:t>月 🐺 &amp; Ω</w:t>".as_bytes();
        let binary = [0, 255, 13, 10];
        let bytes = package(
            &serde_json::to_vec(&serde_json::json!([
                {"name":"word/document.xml","data":BASE64_STANDARD.encode(xml)},
                {"name":"word/media/image1.png","data":BASE64_STANDARD.encode(binary)}
            ]))
            .unwrap(),
        )
        .unwrap();
        let mut archive = zip::ZipArchive::new(Cursor::new(bytes)).unwrap();
        assert_eq!(archive.len(), 2);
        let mut read = Vec::new();
        archive
            .by_name("word/document.xml")
            .unwrap()
            .read_to_end(&mut read)
            .unwrap();
        assert_eq!(read, xml);
        read.clear();
        archive
            .by_name("word/media/image1.png")
            .unwrap()
            .read_to_end(&mut read)
            .unwrap();
        assert_eq!(read, binary);
    }

    #[test]
    fn package_rejects_unsafe_and_duplicate_names_and_invalid_data() {
        for name in ["../file", "/root", "C:/file", "a/../b", "a\\b", "a//b"] {
            assert!(
                package(
                    &serde_json::to_vec(&serde_json::json!([
                        {"name":name,"data":"eA=="}
                    ]))
                    .unwrap()
                )
                .is_err()
            );
        }
        assert!(package(br#"[{"name":"a","data":"eA=="},{"name":"a","data":"eA=="}]"#).is_err());
        assert!(package(br#"[{"name":"a","data":"!"}]"#).is_err());
    }
}
