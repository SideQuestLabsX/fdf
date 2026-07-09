# Types

## Scalars

| Type | Examples | Notes |
|------|----------|-------|
| Bool | `true`, `false` | |
| Int | `12345`, `-7` | signed 64-bit |
| UInt | `12345` | unsigned 64-bit |
| Float | `3.14`, `1.0e21` | 64-bit, scientific notation supported. `inf`/`nan` not representable |
| String | `"text"`, `'text'` | single or double quotes |
| Hex | `0xFF5733#` | trailing `#` marks it as hex |
| Version | `1.0.0.0` | dotted version literal |
| Timestamp | `2024-12-24T15:30:00`, `2024-12-24`, `15:30:00` | ISO-8601 date, time, or both |
| Null / Nil | `null`, `nil` | absence of a value, `nil` is an alias of `null` |

## Multi-dimensional numbers

Numbers joined with `x` form a vector. Two or more components, no fixed upper bound. The only
restriction is all elements must be of same type. If not, we try to widen some elements to
reach that uniformity, if we can't it's an error.

```fdf
resolution = 1920x1080      // 2D
scale      = 1.0x1.0x1.0    // 3D
gradient   = 1x50x10x1      // 4D
five       = 1x2x3x4x5      // 5D
offset     = 0.5x-0.5x1.0   // mixing is fine
```

## Strings

- Either `"double"` or `'single'` quotes
- Escapes like `\t`, `\n`, `\"`, `\'`, `\\`, and so on
- UTF-8 content passes through as-is

```fdf
escaped1 = "She said, \"Hello.\""
escaped2 = 'It\'s fine'
path     = "C:/TestFolder/file.txt"
```

Reading these from C++ is covered in [API](API.md).
