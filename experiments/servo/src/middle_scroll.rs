// SPDX-License-Identifier: GPL-3.0-only
//! Middle-button document autoscroll. This is intentionally a Rust-owned
//! input/motion controller, not a browser's built-in scroll mode.
use std::time::{Duration, Instant};

const DEAD_ZONE: f64 = 15.0;
const MAX_SPEED: f64 = 2400.0;

#[derive(Default)]
pub struct MiddleScroll {
    origin: (f64, f64),
    cursor: (f64, f64),
    last_tick: Option<Instant>,
    due: Option<Instant>,
}

impl MiddleScroll {
    pub fn active(&self) -> bool { self.due.is_some() }

    pub fn origin(&self) -> (f64, f64) { self.origin }

    pub fn toggle(&mut self, origin: (f64, f64), now: Instant) -> bool {
        if self.active() {
            self.cancel();
            return false;
        }
        self.origin = origin;
        self.cursor = origin;
        self.last_tick = Some(now);
        self.due = Some(now + Duration::from_millis(16));
        true
    }

    pub fn update_cursor(&mut self, position: (f64, f64)) {
        if self.active() { self.cursor = position; }
    }

    pub fn cancel(&mut self) { self.due = None; self.last_tick = None; }

    pub fn next_tick(&self) -> Option<Instant> { self.due }

    pub fn tick(&mut self, now: Instant) -> Option<(f64, f64)> {
        let due = self.due?;
        if now < due { return None; }
        let dt = now.saturating_duration_since(self.last_tick.unwrap_or(now))
            .as_secs_f64().clamp(0.0, 0.05);
        self.last_tick = Some(now);
        self.due = Some(now + Duration::from_millis(16));
        Some((
            velocity(self.cursor.0 - self.origin.0) * dt,
            velocity(self.cursor.1 - self.origin.1) * dt,
        ))
    }
}

/// Low-speed precision close to the anchor, accelerated motion farther away.
fn velocity(distance: f64) -> f64 {
    if !distance.is_finite() || distance.abs() <= DEAD_ZONE { return 0.0; }
    let x = (distance.abs() - DEAD_ZONE) / 70.0;
    // No discontinuity at the dead-zone boundary, and no unbounded speed.
    distance.signum() * (720.0 * x.powf(1.35)).min(MAX_SPEED)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn middle_autoscroll_handles_lifecycle_and_direction() {
        let mut m = MiddleScroll::default();
        let mut t = Instant::now();
        assert!(m.toggle((400.0, 300.0), t));
        assert!(m.active());
        m.update_cursor((400.0, 300.0));
        t += Duration::from_millis(16);
        assert_eq!(m.tick(t), Some((0.0, 0.0)));
        m.update_cursor((400.0, 470.0));
        t += Duration::from_millis(16);
        assert!(m.tick(t).unwrap().1 > 0.0);
        m.update_cursor((400.0, 120.0));
        t += Duration::from_millis(16);
        assert!(m.tick(t).unwrap().1 < 0.0);
        assert!(!m.toggle((400.0, 300.0), t));
        assert!(!m.active());
        assert!(m.next_tick().is_none());
    }

    #[test]
    fn motion_is_bounded_and_respects_dead_zone() {
        assert_eq!(velocity(15.0), 0.0);
        assert_eq!(velocity(-15.0), 0.0);
        assert_eq!(velocity(f64::NAN), 0.0);
        assert!(velocity(1500.0) <= MAX_SPEED);
        assert!(velocity(-1500.0) >= -MAX_SPEED);
    }
}
