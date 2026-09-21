#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Rgb(pub u8, pub u8, pub u8);

impl Rgb {
    #[must_use]
    pub const fn is_achromatic(self) -> bool {
        self.0 == self.1 && self.1 == self.2
    }

    #[must_use]
    pub const fn has_blue_bias(self) -> bool {
        self.2 > self.0 && self.2 > self.1
    }
}

pub const BACKGROUND: Rgb = Rgb(11, 11, 11);
pub const SHELL: Rgb = Rgb(16, 16, 16);
pub const DOCUMENT: Rgb = Rgb(18, 18, 18);
pub const SURFACE: Rgb = Rgb(25, 25, 25);
pub const RAISED: Rgb = Rgb(33, 33, 33);
pub const HOVER: Rgb = Rgb(41, 41, 41);
pub const ACTIVE: Rgb = Rgb(52, 35, 38);
pub const BORDER: Rgb = Rgb(52, 52, 52);
pub const BORDER_STRONG: Rgb = Rgb(80, 80, 80);
pub const TEXT: Rgb = Rgb(233, 231, 230);
pub const TEXT_SECONDARY: Rgb = Rgb(187, 184, 182);
pub const TEXT_MUTED: Rgb = Rgb(138, 135, 133);
pub const TEXT_DISABLED: Rgb = Rgb(101, 98, 96);
pub const SILVER: Rgb = Rgb(208, 205, 202);
pub const BRIGHT_SILVER: Rgb = Rgb(244, 241, 239);
pub const FOCUS: Rgb = Rgb(214, 45, 78);
pub const SELECTION: Rgb = Rgb(105, 49, 60);
pub const CRIMSON: Rgb = Rgb(214, 45, 78);
pub const ERROR: Rgb = Rgb(212, 90, 100);

pub const ALL_DEFAULT_COLOURS: &[Rgb] = &[
    BACKGROUND,
    SHELL,
    DOCUMENT,
    SURFACE,
    RAISED,
    HOVER,
    ACTIVE,
    BORDER,
    BORDER_STRONG,
    TEXT,
    TEXT_SECONDARY,
    TEXT_MUTED,
    TEXT_DISABLED,
    SILVER,
    BRIGHT_SILVER,
    FOCUS,
    SELECTION,
    CRIMSON,
];

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_theme_contains_only_neutrals_and_crimson_without_blue_bias() {
        assert!(
            ALL_DEFAULT_COLOURS
                .iter()
                .all(|colour| !colour.has_blue_bias())
        );
    }
}
