# Types

## Scalars

| Type | Examples | Notes |
|------|----------|-------|
| Bool | `true`, `false` | |
| Int | `12345`, `-7` | 64-bit, literals accepted in `-2^63..2^64-1` |
| Float | `3.14`, `1.0e21` | 64-bit, scientific notation supported. `inf`/`nan` not representable |
| String | `"text"`, `'text'` | single or double quotes |
| Hex | `0xFF5733` | `0x`/`0X` prefix marks it as hex |
| Version | `1.0.0`, `1.0.0.0` | three- or four-component dotted numeric version |
| Timestamp | `2024-12-24T15:30:00`, `2024-12-24`, `15:30:00` | ISO-8601 date, time or both |
| Null / Nil | `null`, `nil` | absence of a value, `nil` is an alias of `null` |

Timestamp dates may use calendar (`2024-12-24`), ordinal (`2024-359`), or ISO week
(`2024-W52-2`) notation. Each form can include a time and zone. Week 53 is only valid in
years that contain it.

Int stores 64 bits with no separate unsigned type. Literals above `2^63-1` keep their unsigned
bit pattern and read back exactly through `GetValue<uint64_t>()`. They re-serialize in signed
form: `18446744073709551615` is written back as `-1`, same bits.

## Packs

Numbers, bools, strings, hex, versions or timestamps joined with `|` form a pack: one atomic value with N
uniform components. All components must share a type. Integers can widen to floats, but a mix with no common
type, such as `1|true` or `1|"a"`, is an error.

```fdf
resolution = 1920|1080             // one value, two components
scale      = 1.0|1.0|1.0
gradient   = 1|50|10|1
five       = 1|2|3|4|5             // no fixed component cap
offset     = 0.5|-0.5|1.0          // widening is fine
flags      = true|false
tags       = "config"|"a|b"|'x'    // a '|' inside quotes is literal
channels   = 0xFF|0x80|0x40
versions   = 1.0.0|1.1.0.0         // 3 and 4 components can mix
window     = 2024-12-24|2024-12-31
```

Version components are `uint32_t`, except `major` is limited to `0..2147483647`.
`1.2.3` and `1.2.3.0` stay distinct.

Pack components have no comments, nesting or separate `Entry` nodes. Use packs for things
like coordinates or color channels, and arrays for lists of distinct values.

## Strings

- Either `"double"` or `'single'` quotes
- Escapes like `\t`, `\n`, `\"`, `\'`, `\\`, and so on (`\u`/`\U` are kept literal, not decoded)
- UTF-8 content passes through byte-for-byte without normalization or re-escaping. The parser strips
  a leading BOM. Malformed UTF-8 still parses and produces a non-fatal `InvalidUtf8` warning. Check
  input with `fdf::IsValidUtf8(sv)` or `String::IsValidUtf8()`

```fdf
escaped1 = "She said, \"Hello.\""
escaped2 = 'It\'s fine'
path     = "C:/TestFolder/file.txt"
```

Reading these from C++ is covered in [API](API.md).
