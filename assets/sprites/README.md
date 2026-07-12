# Pulse Court sprite pack

This is an original, transparent PNG sprite pack for the Pulse Court viewer.
It is deliberately separate from the deterministic core, so it has no effect
on headless simulation, replay hashes, or training throughput.

## Sheets

Each player sheet is a two-column by two-row action sheet. Use normalized cell
coordinates from `manifest.json` rather than hard-coded pixel dimensions.

| Sheet | Top-left | Top-right | Bottom-left | Bottom-right |
| --- | --- | --- | --- | --- |
| `kite_actions.png` | idle | skate / move | strike | Jetstep dash |
| `vale_actions.png` | idle | skate / move | strike | Anchor Well cast |
| `bastion_actions.png` | idle | skate / move | strike | Pulse Gate cast |

`arena_fx.png` is a two-column by three-row VFX sheet: core, strike burst,
dash streak, Jetstep ring, Anchor Well, and Pulse Gate (reading left-to-right,
top-to-bottom).

All sheets have transparent alpha backgrounds and were created as original
project art. They contain no third-party game characters, logos, or UI.
