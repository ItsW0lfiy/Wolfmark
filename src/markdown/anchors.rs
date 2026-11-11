pub fn heading_slug(title: &str) -> String {
    let mut slug = String::new();
    let mut pending_dash = false;
    for character in title.chars().flat_map(char::to_lowercase) {
        if character.is_alphanumeric() || character == '_' {
            if pending_dash && !slug.is_empty() {
                slug.push('-');
            }
            slug.push(character);
            pending_dash = false;
        } else if character.is_whitespace() || character == '-' {
            pending_dash = true;
        }
    }
    if slug.is_empty() {
        "section".into()
    } else {
        slug
    }
}
