# Pulse Court 1v1 Ruleset (v2)

## Match

- Court: 38 by 22 world units, with goal mouths centered on the two short sides
  between y=8 and y=14.
- Teams: one player per side. Left (index 0) attacks right; right (index 1)
  attacks left.
- Win condition: first to five goals. A 180-second regulation clock ends the
  match if one side leads; a tie enters golden goal.
- After every non-winning goal, the core and players reset to fixed symmetric
  positions for 90 simulation ticks.
- There is no hidden serve randomizer and no gameplay RNG.

## Universal controls

- **Move:** an eight-direction movement vector (or neutral). Movement has
  acceleration and a character-dependent speed cap.
- **Strike:** a short directional hit pulse with a cooldown. A pulse can
  contact the core at most once per swing, only in front of the striker, and
  adds a directional impulse with a minimum forward launch speed.
- **Dash:** a universal directional burst with a cooldown.
- **Ability:** one character-specific active ability with a cooldown.

Direction comes from aim if supplied, otherwise the most recent movement
direction. The viewer maps movement to aim so the game is fully keyboard
playable.

## Characters

### Kite — interceptor

**Jetstep** grants a short speed burst. While it is active, Kite's next strike
receives extra core impulse. It rewards quick chase-and-convert play without
adding projectiles or randomness.

### Vale — trajectory controller

**Anchor Well** creates a short-lived point ahead of Vale. A nearby core is
gently pulled toward the point, allowing planned bends and defensive saves. It
is a deterministic force field, not a random curve.

### Bastion — zone defender

**Pulse Gate** places a short-lived core-only circular barrier ahead of
Bastion. The core bounces from it, while players pass through it. This gives
Bastion a readable defensive placement tool.

## Authoritative physics

- All positions, velocities, normals, cooldowns, and timers use signed
  integers.
- Fixed-point scale: 1024 fixed units == 1.0 world units.
- The core advances through two collision substeps per 120 Hz tick.
- Contacts are resolved in a fixed order with integer square-root normalization.
- Exact-overlap fallbacks are deterministic and documented in the source code.
- Wall, player, and Pulse Gate impacts use controlled restitution; the core
  receives a small deterministic drag after non-scoring ticks.
