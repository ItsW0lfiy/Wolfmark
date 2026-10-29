// SPDX-License-Identifier: GPL-3.0-only
//! Smooth discrete-wheel motion for the Servo embedding. Mirrors the native
//! Qt reader's critically damped, elapsed-time trajectory without JS, timers,
//! or a second document scroll state. Touchpad pixel deltas bypass this path.

use std::time::{Duration, Instant};

const RESPONSE_RATE: f64 = 17.0;
const STOPPED_DISTANCE: f64 = 1.25;
const STOPPED_VELOCITY: f64 = 35.0;
const RAPID_DISTANCE: f64 = 320.0;
const RAPID_VELOCITY: f64 = 900.0;
const WHEEL_LINE_PIXELS: f64 = 48.0;

#[derive(Default)]
struct Axis {
    position: f64,
    target: f64,
    velocity: f64,
    emitted: f64,
}

impl Axis {
    fn advance(&mut self, dt: f64) -> f64 {
        // Exact critically damped integration for one fixed target over dt.
        // New wheel notches extend the target without resetting the velocity;
        // direction changes therefore reverse the same ongoing motion.
        let displacement = self.position - self.target;
        let c = self.velocity + RESPONSE_RATE * displacement;
        let decay = (-RESPONSE_RATE * dt).exp();
        self.position = self.target + (displacement + c * dt) * decay;
        self.velocity = (self.velocity - RESPONSE_RATE * c * dt) * decay;

        if (self.target - self.position).abs() <= STOPPED_DISTANCE
            && self.velocity.abs() <= STOPPED_VELOCITY
        {
            self.position = self.target;
            self.velocity = 0.0;
        }
        let moved = self.position - self.emitted;
        self.emitted = self.position;
        moved
    }

    fn finished(&self) -> bool {
        self.position == self.target && self.velocity == 0.0
    }

    fn rapid(&self) -> bool {
        (self.target - self.position).abs() >= RAPID_DISTANCE
            || self.velocity.abs() >= RAPID_VELOCITY
    }
}

#[derive(Default)]
pub struct SmoothWheel {
    x: Axis,
    y: Axis,
    last_tick: Option<Instant>,
    next_tick: Option<Instant>,
}

impl SmoothWheel {
    pub fn add_lines(&mut self, lines_x: f32, lines_y: f32, now: Instant) {
        if !lines_x.is_finite() || !lines_y.is_finite() || (lines_x == 0.0 && lines_y == 0.0) {
            return;
        }
        if self.next_tick.is_none() {
            self.x = Axis::default();
            self.y = Axis::default();
            self.last_tick = Some(now);
        }
        self.x.target += f64::from(lines_x) * WHEEL_LINE_PIXELS;
        self.y.target += f64::from(lines_y) * WHEEL_LINE_PIXELS;
        self.next_tick = Some(now);
    }

    pub fn next_tick(&self) -> Option<Instant> {
        self.next_tick
    }

    pub fn cancel(&mut self) {
        *self = Self::default();
    }

    /// Returns exactly the incremental pixel delta since the last frame.
    /// Request a new frame at next_tick() until it returns None.
    pub fn tick(&mut self, now: Instant) -> Option<(f64, f64)> {
        if now < self.next_tick? {
            return None;
        }
        let dt = self.last_tick
            .map(|last| now.saturating_duration_since(last).as_secs_f64())
            .unwrap_or(0.016)
            .clamp(0.001, 0.050);
        self.last_tick = Some(now);
        let dx = self.x.advance(dt);
        let dy = self.y.advance(dt);
        if self.x.finished() && self.y.finished() {
            self.next_tick = None;
            self.last_tick = None;
        } else {
            let rapid = self.x.rapid() || self.y.rapid();
            self.next_tick = Some(now + Duration::from_millis(if rapid { 8 } else { 16 }));
        }
        Some((dx, dy))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn simulate(wheel: &mut SmoothWheel, now: &mut Instant) -> (f64, f64) {
        let mut sum = (0.0, 0.0);
        let mut frames = 0;
        while let Some(due) = wheel.next_tick() {
            *now = due;
            let (dx, dy) = wheel.tick(*now).expect("due frame");
            sum.0 += dx;
            sum.1 += dy;
            frames += 1;
            assert!(frames < 400, "controller never settled");
        }
        sum
    }

    #[test]
    fn each_wheel_notch_has_correct_total_displacement() {
        let mut now = Instant::now();
        let mut wheel = SmoothWheel::default();
        wheel.add_lines(0.0, -3.0, now);
        let (dx, dy) = simulate(&mut wheel, &mut now);
        assert!(dx.abs() < 0.01);
        assert!((dy + 144.0).abs() < 0.01, "{dy}");
    }

    #[test]
    fn repeated_input_extends_trajectory_without_restart() {
        let mut now = Instant::now();
        let mut wheel = SmoothWheel::default();
        wheel.add_lines(0.0, -2.0, now);
        now += Duration::from_millis(16);
        let first = wheel.tick(now).unwrap().1;
        assert!(first < 0.0);
        wheel.add_lines(0.0, -1.0, now);
        let (_, tail) = simulate(&mut wheel, &mut now);
        assert!((first + tail + 144.0).abs() < 0.01);
    }

    #[test]
    fn direction_reversal_preserves_distance_and_settles() {
        let mut now = Instant::now();
        let mut wheel = SmoothWheel::default();
        wheel.add_lines(0.0, 4.0, now);
        now += Duration::from_millis(16);
        let before = wheel.tick(now).unwrap().1;
        assert!(before > 0.0);
        wheel.add_lines(0.0, -5.0, now);
        let (_, rest) = simulate(&mut wheel, &mut now);
        assert!((before + rest + 48.0).abs() < 0.01);
    }

    #[test]
    fn cancel_stops_timed_updates() {
        let mut wheel = SmoothWheel::default();
        wheel.add_lines(2.0, 1.0, Instant::now());
        wheel.cancel();
        assert!(wheel.next_tick().is_none());
    }
}
