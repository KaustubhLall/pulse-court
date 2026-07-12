# Replay Format

File extension: `.pulse`

## Header

| Field | Size | Description |
|-------|------|-------------|
| magic | 8 bytes | `"PULSEREPL"` (padded to 8) |
| format_version | 4 bytes | uint32, currently 1 |
| ruleset_version | 4 bytes | uint32, currently 1 |
| left_character | 1 byte | uint8 `Character` value |
| right_character | 1 byte | uint8 `Character` value |
| score_to_win | 4 bytes | int32 `MatchConfig` field |
| regulation_ticks | 4 bytes | int32 `MatchConfig` field |
| kickoff_ticks | 4 bytes | int32 `MatchConfig` field |
| tick_count | 4 bytes | uint32 number of recorded ticks |

## Per-tick frame

Each tick stores two `FrameInput` records and a state hash:

| Field | Size |
|-------|------|
| move_x | 1 byte (int8) |
| move_y | 1 byte (int8) |
| aim_x | 1 byte (int8) |
| aim_y | 1 byte (int8) |
| buttons | 1 byte (uint8) |

The above 5 bytes are repeated for player 0 then player 1, followed by:

| Field | Size |
|-------|------|
| expected_hash | 8 bytes (uint64) |

## Compatibility

`load_replay` rejects files whose magic, `format_version`, or `ruleset_version`
do not match the current `pulse_core` expectations. This prevents silent match
outcome drift between versions.
