# Servo paint API — upstream GL context fix

This isolated vendored copy of `servo-paint-api v0.7.0` (MPL-2.0) comes
from [Servo v0.7.0](https://github.com/servo/servo/tree/v0.7.0/components/shared/paint).
It is used **only** by the Wolfmark Servo proof of concept, not the normal
Qt build.

The only code change relative to Servo v0.7.0 is in
`rendering_context.rs`: the GL context is made current **before**
initializing `glow`. If that fails, the context is destroyed and the
underlying error is returned. This backports upstream Servo
[commit 16e4b1591e1f](https://github.com/servo/servo/commit/16e4b1591e1fa4a47016643e34d4f259185c5f2f)
(PR #48636, October 5, 2026), fixing the `GL_VERSION` panic with
Surfman/WGL.

All original source retains the upstream MPL-2.0 license notices.
The modification is likewise distributed under MPL-2.0.
Keep this patch scoped to the experimental Cargo workspace and remove
the override after validating a compatible upstream Servo release
that includes the fix.

This is not evidence that other Servo functionality or strict offline
isolation works yet. Testing is still mandatory.
