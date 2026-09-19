use serde::Serialize;

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PresentationDocument {
    pub title: String,
    pub source_type: String,
    pub literal_text: String,
    pub error: String,
    pub commands: Vec<PresentationCommand>,
    pub image_ids: Vec<u32>,
    pub toc: Vec<TocEntry>,
    pub metrics: PresentationMetrics,
    pub settings: PresentationSettings,
}

impl PresentationDocument {
    pub fn error(message: String) -> Self {
        Self {
            title: "Moonmark".into(),
            source_type: "error".into(),
            literal_text: String::new(),
            error: message,
            commands: Vec::new(),
            image_ids: Vec::new(),
            toc: Vec::new(),
            metrics: PresentationMetrics::default(),
            settings: PresentationSettings::default(),
        }
    }

    pub fn plain_text(title: String, source: String, metrics: PresentationMetrics) -> Self {
        Self {
            title,
            source_type: "plainText".into(),
            literal_text: source,
            error: String::new(),
            commands: Vec::new(),
            image_ids: Vec::new(),
            toc: Vec::new(),
            metrics,
            settings: PresentationSettings::default(),
        }
    }
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PresentationCommand {
    pub kind: u8,
    pub level: u8,
    pub flags: u16,
    pub text: String,
    pub target: String,
    pub extra: String,
    pub number: i64,
    pub image_width: u32,
    pub image_height: u32,
    pub spans: Vec<CodeSpan>,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CodeSpan {
    pub text: String,
    pub red: u8,
    pub green: u8,
    pub blue: u8,
    pub flags: u8,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TocEntry {
    pub level: u8,
    pub title: String,
    pub anchor: String,
}

#[derive(Clone, Debug, Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PresentationMetrics {
    pub revision: u64,
    pub source_bytes: u64,
    pub read_us: u64,
    pub parse_us: u64,
    pub presentation_us: u64,
    pub parse_count: u64,
    pub load_count: u64,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PresentationSettings {
    pub body_font_points: f64,
    pub document_padding: i32,
    pub line_height_percent: i32,
}

impl Default for PresentationSettings {
    fn default() -> Self {
        Self {
            body_font_points: 12.75,
            document_padding: 48,
            line_height_percent: 150,
        }
    }
}
