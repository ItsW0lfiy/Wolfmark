use std::str::FromStr;
use std::sync::OnceLock;

use two_face::re_exports::syntect::easy::HighlightLines;
use two_face::re_exports::syntect::highlighting::{
    Color, FontStyle, ScopeSelectors, StyleModifier, Theme, ThemeItem, ThemeSettings,
};
use two_face::re_exports::syntect::parsing::{SyntaxReference, SyntaxSet};

use crate::presentation::CodeSpan;

const PLAIN: Color = colour(231, 225, 216);
const COMMENT: Color = colour(143, 143, 143);
const KEYWORD: Color = colour(214, 168, 95);
const STRING: Color = colour(159, 188, 140);
const NUMBER: Color = colour(214, 154, 104);
const TYPE: Color = colour(216, 202, 177);
const FUNCTION: Color = colour(240, 230, 210);
const ACCENT: Color = colour(201, 141, 104);
const CONSTANT: Color = colour(201, 144, 134);
const PUNCTUATION: Color = colour(198, 198, 198);
const BACKGROUND: Color = colour(24, 24, 24);

const fn colour(r: u8, g: u8, b: u8) -> Color {
    Color { r, g, b, a: 255 }
}

pub fn highlight_code(language: &str, source: &str) -> Vec<CodeSpan> {
    let source = normalize_source(source);
    if source.is_empty() {
        return Vec::new();
    }
    let Some(syntax) = find_syntax(language) else {
        return vec![span(PLAIN, FontStyle::empty(), source)];
    };
    if syntax.name == "Plain Text" {
        return vec![span(PLAIN, FontStyle::empty(), source)];
    }

    let syntax_set = syntax_set();
    let mut highlighter = HighlightLines::new(syntax, wolfmark_theme());
    let mut output = Vec::new();
    for line in source.split_inclusive('\n') {
        let Ok(ranges) = highlighter.highlight_line(line, syntax_set) else {
            return vec![span(PLAIN, FontStyle::empty(), source)];
        };
        for (style, text) in ranges {
            push_merged(&mut output, style.foreground, style.font_style, text);
        }
    }
    output
}

fn normalize_source(source: &str) -> String {
    let mut normalized = source.replace("\r\n", "\n").replace('\r', "\n");
    if normalized.ends_with('\n') {
        normalized.pop();
    }
    normalized
}

fn syntax_set() -> &'static SyntaxSet {
    static SYNTAXES: OnceLock<SyntaxSet> = OnceLock::new();
    SYNTAXES.get_or_init(two_face::syntax::extra_newlines)
}

fn find_syntax(language: &str) -> Option<&'static SyntaxReference> {
    let token = language
        .split_whitespace()
        .next()
        .unwrap_or_default()
        .trim()
        .to_ascii_lowercase();
    if matches!(token.as_str(), "" | "text" | "txt" | "plain" | "plaintext") {
        return Some(syntax_set().find_syntax_plain_text());
    }
    language_tokens(&token)
        .iter()
        .find_map(|candidate| syntax_set().find_syntax_by_token(candidate))
}

fn language_tokens(language: &str) -> &'static [&'static str] {
    match language {
        "rust" | "rs" => &["rs", "Rust"],
        "python" | "py" => &["py", "Python"],
        "powershell" | "pwsh" | "ps1" => &["ps1", "PowerShell"],
        "json" | "jsonc" => &["json", "JSON"],
        "toml" => &["toml", "TOML"],
        "yaml" | "yml" => &["yaml", "YAML"],
        "javascript" | "js" | "jsx" => &["js", "JavaScript"],
        "typescript" | "ts" | "tsx" => &["ts", "TypeScript"],
        "c" => &["c", "C"],
        "c++" | "cpp" | "cc" | "cxx" => &["cpp", "C++"],
        "c#" | "csharp" | "cs" => &["cs", "C#"],
        "java" => &["java", "Java"],
        "shell" | "bash" | "sh" | "zsh" => &["sh", "Shell-Unix-Generic"],
        "html" | "htm" => &["html", "HTML"],
        "xml" => &["xml", "XML"],
        _ => &[],
    }
}

fn wolfmark_theme() -> &'static Theme {
    static THEME: OnceLock<Theme> = OnceLock::new();
    THEME.get_or_init(|| Theme {
        name: Some("Wolfmark Code".into()),
        author: Some("Wolfmark".into()),
        settings: ThemeSettings {
            foreground: Some(PLAIN),
            background: Some(BACKGROUND),
            ..ThemeSettings::default()
        },
        scopes: vec![
            theme_item("comment", COMMENT, FontStyle::ITALIC),
            theme_item("string", STRING, FontStyle::empty()),
            theme_item("constant.numeric", NUMBER, FontStyle::empty()),
            theme_item(
                "constant.language, constant.character, constant.other",
                CONSTANT,
                FontStyle::empty(),
            ),
            theme_item(
                "keyword, storage.type, storage.modifier",
                KEYWORD,
                FontStyle::BOLD,
            ),
            theme_item(
                "entity.name.type, entity.name.class, entity.name.struct, support.type, support.class",
                TYPE,
                FontStyle::empty(),
            ),
            theme_item(
                "entity.name.function, support.function",
                FUNCTION,
                FontStyle::empty(),
            ),
            theme_item(
                "entity.name.function.macro, meta.annotation, entity.name.tag, entity.other.attribute-name",
                ACCENT,
                FontStyle::empty(),
            ),
            theme_item(
                "variable.language, variable.other.constant, variable.parameter",
                TYPE,
                FontStyle::empty(),
            ),
            theme_item(
                "meta.mapping.key string, meta.structure.dictionary string",
                KEYWORD,
                FontStyle::empty(),
            ),
            theme_item(
                "punctuation, keyword.operator",
                PUNCTUATION,
                FontStyle::empty(),
            ),
        ],
    })
}

fn theme_item(selector: &str, foreground: Color, font_style: FontStyle) -> ThemeItem {
    ThemeItem {
        scope: ScopeSelectors::from_str(selector).expect("static Wolfmark scope selector"),
        style: StyleModifier {
            foreground: Some(foreground),
            background: None,
            font_style: Some(font_style),
        },
    }
}

fn push_merged(output: &mut Vec<CodeSpan>, colour: Color, font_style: FontStyle, text: &str) {
    let flags = u8::from(font_style.contains(FontStyle::BOLD))
        | (u8::from(font_style.contains(FontStyle::ITALIC)) << 1);
    if let Some(previous) = output.last_mut()
        && previous.red == colour.r
        && previous.green == colour.g
        && previous.blue == colour.b
        && previous.flags == flags
    {
        previous.text.push_str(text);
        return;
    }
    output.push(span(colour, font_style, text.to_owned()));
}

fn span(colour: Color, font_style: FontStyle, text: String) -> CodeSpan {
    CodeSpan {
        text,
        red: colour.r,
        green: colour.g,
        blue: colour.b,
        flags: u8::from(font_style.contains(FontStyle::BOLD))
            | (u8::from(font_style.contains(FontStyle::ITALIC)) << 1),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn required_language_tokens_have_syntaxes() {
        for language in [
            "rust",
            "python",
            "powershell",
            "json",
            "toml",
            "yaml",
            "javascript",
            "typescript",
            "c",
            "cpp",
            "csharp",
            "java",
            "bash",
            "html",
            "xml",
        ] {
            assert!(
                find_syntax(language).is_some(),
                "missing syntax: {language}"
            );
        }
    }

    #[test]
    fn rust_json_python_and_powershell_receive_multiple_styles() {
        let fixtures = [
            (
                "rust",
                "fn main() { let value = \"Wolfmark\"; println!(\"{value}\"); }",
            ),
            ("json", "{\"ready\": true, \"count\": 3}"),
            (
                "python",
                "def greet(name):\n    # quiet\n    return f\"Hello {name}\"",
            ),
            (
                "powershell",
                "$name = \"Wolfmark\"\nWrite-Output -InputObject $name # quiet",
            ),
        ];
        for (language, source) in fixtures {
            let spans = highlight_code(language, source);
            let colours: std::collections::HashSet<_> = spans
                .iter()
                .map(|span| (span.red, span.green, span.blue))
                .collect();
            assert!(
                colours.len() >= 3,
                "flat highlighting for {language}: {spans:?}"
            );
        }
    }

    #[test]
    fn code_palette_contains_no_blue_leaning_colour() {
        for colour in [
            PLAIN,
            COMMENT,
            KEYWORD,
            STRING,
            NUMBER,
            TYPE,
            FUNCTION,
            ACCENT,
            CONSTANT,
            PUNCTUATION,
            BACKGROUND,
        ] {
            assert!(colour.b <= colour.r.max(colour.g));
        }
    }

    #[test]
    fn unknown_languages_fall_back_to_one_plain_span() {
        let spans = highlight_code("moonlang", "orbit phase");
        assert_eq!(spans.len(), 1);
        assert_eq!(spans[0].text, "orbit phase");
        assert_eq!(
            (spans[0].red, spans[0].green, spans[0].blue),
            (231, 225, 216)
        );
    }
}
