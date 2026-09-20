use wolfmark::markdown::convert::{kind, style};
use wolfmark::markdown::{Block, Inline, parse, to_presentation};
use wolfmark::presentation::PresentationMetrics;
use wolfmark::settings::Settings;
use wolfmark::theme::palette::ALL_DEFAULT_COLOURS;
use wolfmark::window::{WindowMode, WindowState};

#[test]
fn required_markdown_constructs_reach_the_semantic_model() {
    let model = parse(
        "# Heading\n\n- [x] task\n  - nested\n\n[link](https://example.invalid) ![image](images/a.png)\n\n|A|B|\n|-|-|\n|1|2|\n\n```rust\ncode\n```",
    );
    assert!(
        model
            .blocks
            .iter()
            .any(|block| matches!(block, Block::Heading { .. }))
    );
    assert!(
        model
            .blocks
            .iter()
            .any(|block| matches!(block, Block::List { .. }))
    );
    assert!(
        model
            .blocks
            .iter()
            .any(|block| matches!(block, Block::Table { .. }))
    );
    assert!(
        model
            .blocks
            .iter()
            .any(|block| matches!(block, Block::CodeBlock { .. }))
    );
    let paragraph = model
        .blocks
        .iter()
        .find_map(|block| match block {
            Block::Paragraph(inlines) => Some(inlines),
            _ => None,
        })
        .unwrap();
    assert!(
        paragraph
            .iter()
            .any(|inline| matches!(inline, Inline::Link { .. }))
    );
    assert!(
        paragraph
            .iter()
            .any(|inline| matches!(inline, Inline::Image { .. }))
    );
}

#[test]
fn maximize_and_fullscreen_are_independent_states() {
    let mut state = WindowState::default();
    state.toggle_maximize_model();
    state.enter_fullscreen_model();
    assert_eq!(state.mode, WindowMode::BorderlessFullscreen);
    assert!(state.leave_fullscreen_model());
    assert_eq!(state.mode, WindowMode::Maximized);
}

#[test]
fn default_palette_is_entirely_achromatic() {
    assert!(
        ALL_DEFAULT_COLOURS
            .iter()
            .all(|colour| colour.is_achromatic())
    );
}

#[test]
fn presentation_heading_targets_match_unique_toc_anchors() {
    let model = parse("# Repeat\n\n## Repeat\n\n[Jump](#repeat-1)");
    let (presentation, images) = to_presentation(
        &model,
        std::path::Path::new("C:/Wolfmark/document.md"),
        PresentationMetrics {
            revision: 1,
            ..PresentationMetrics::default()
        },
        &Settings::default(),
    );
    assert!(images.is_empty());
    assert_eq!(
        presentation
            .toc
            .iter()
            .map(|entry| entry.anchor.as_str())
            .collect::<Vec<_>>(),
        ["repeat", "repeat-1"]
    );
    assert_eq!(
        presentation
            .commands
            .iter()
            .filter(|command| command.kind == 2)
            .map(|command| command.target.as_str())
            .collect::<Vec<_>>(),
        ["repeat", "repeat-1"]
    );
}

#[test]
fn practical_markdown_profile_reaches_the_presentation_model() {
    let fixture = std::fs::read_to_string(
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("fixtures")
            .join("markdown-compatibility.md"),
    )
    .expect("read compatibility fixture");
    let model = parse(&fixture);
    let (presentation, images) = to_presentation(
        &model,
        std::path::Path::new("C:/Wolfmark/markdown-compatibility.md"),
        PresentationMetrics::default(),
        &Settings::default(),
    );

    assert!(images.is_empty());
    for expected_kind in [
        kind::BEGIN_HEADING,
        kind::BEGIN_QUOTE,
        kind::BEGIN_LIST,
        kind::CODE_BLOCK,
        kind::BEGIN_TABLE,
        kind::HORIZONTAL_RULE,
        kind::RAW_HTML,
    ] {
        assert!(
            presentation
                .commands
                .iter()
                .any(|command| command.kind == expected_kind),
            "missing presentation command kind {expected_kind}"
        );
    }
    for expected_style in [
        style::EMPHASIS,
        style::STRONG,
        style::STRIKE,
        style::CODE,
        style::LINK,
        style::CHECKED,
        style::UNCHECKED,
    ] {
        assert!(
            presentation
                .commands
                .iter()
                .any(|command| command.flags & expected_style != 0),
            "missing presentation style {expected_style}"
        );
    }
    let item_flags = presentation
        .commands
        .iter()
        .filter(|command| command.kind == kind::BEGIN_ITEM)
        .map(|command| command.flags)
        .collect::<Vec<_>>();
    assert!(item_flags.contains(&0), "ordinary list item became a task");
    assert!(item_flags.contains(&style::UNCHECKED));
    assert!(item_flags.contains(&style::CHECKED));
    let destinations = presentation
        .commands
        .iter()
        .filter(|command| command.flags & style::LINK != 0)
        .map(|command| command.target.as_str())
        .collect::<Vec<_>>();
    for expected in [
        "https://example.com/autolink",
        "https://example.com/",
        "https://example.com/reference",
        "https://example.com/collapsed",
        "https://example.com/shortcut",
    ] {
        assert!(destinations.contains(&expected), "missing link {expected}");
    }
    assert!(
        presentation
            .commands
            .iter()
            .any(|command| { command.kind == kind::CODE_BLOCK && command.extra == "rust" })
    );
    for nested_text in ["nested emphasis", "nested strength"] {
        assert!(presentation.commands.iter().any(|command| {
            command.kind == kind::TEXT
                && command.text == nested_text
                && command.flags & style::EMPHASIS != 0
                && command.flags & style::STRONG != 0
        }));
    }
    assert!(
        presentation.commands.iter().any(|command| {
            command.kind == kind::TEXT && command.text.contains("[compatibility]")
        })
    );
    assert_eq!(
        presentation
            .toc
            .iter()
            .filter(|entry| entry.title == "Repeated heading")
            .map(|entry| entry.anchor.as_str())
            .collect::<Vec<_>>(),
        ["repeated-heading", "repeated-heading-1"]
    );
}
