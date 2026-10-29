//! Search offsets are UTF-16, matching Servo DOM Range positions.
#[derive(Default)]
pub struct Search {
    query: String,
    text: String,
    matches: Vec<(usize, usize)>,
    current: Option<usize>,
}
impl Search {
    pub fn next(
        &mut self,
        text: &str,
        query: &str,
        backwards: bool,
    ) -> Option<(usize, usize, usize, usize)> {
        if self.query != query || self.text != text {
            self.query = query.into();
            self.text = text.into();
            self.matches = find_matches(text, query);
            self.current = None;
        }
        let count = self.matches.len();
        if count == 0 {
            self.current = None;
            return None;
        }
        let current = match self.current {
            None => {
                if backwards {
                    count - 1
                } else {
                    0
                }
            }
            Some(index) => {
                if backwards {
                    (index + count - 1) % count
                } else {
                    (index + 1) % count
                }
            }
        };
        self.current = Some(current);
        let (start, end) = self.matches[current];
        Some((start, end, current + 1, count))
    }
}
fn find_matches(text: &str, query: &str) -> Vec<(usize, usize)> {
    let needle: Vec<char> = query.chars().flat_map(char::to_lowercase).collect();
    if needle.is_empty() {
        return Vec::new();
    }
    let mut offset = 0;
    let mut haystack = Vec::new();
    for ch in text.chars() {
        let end = offset + ch.len_utf16();
        haystack.extend(ch.to_lowercase().map(|c| (c, offset, end)));
        offset = end;
    }
    haystack
        .windows(needle.len())
        .filter_map(|window| {
            window
                .iter()
                .map(|v| v.0)
                .eq(needle.iter().copied())
                .then(|| (window[0].1, window.last().unwrap().2))
        })
        .collect()
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn unicode_casefold_uses_dom_offsets() {
        assert_eq!(find_matches("🐺 Élan élan", "ÉLAN"), vec![(3, 7), (8, 12)]);
        assert!(find_matches("wolf", "").is_empty());
    }
    #[test]
    fn navigation_wraps_and_new_query_resets() {
        let mut search = Search::default();
        assert_eq!(search.next("one one", "one", false), Some((0, 3, 1, 2)));
        assert_eq!(search.next("one one", "one", true), Some((4, 7, 2, 2)));
        assert_eq!(search.next("one one", "one", false), Some((0, 3, 1, 2)));
        assert_eq!(search.next("one one", "absent", false), None);
    }
}
