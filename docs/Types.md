# Types

## Scalars

| Type | Examples | Notes |
|------|----------|-------|
| Bool | `true`, `false` | |
| Int | `12345`, `-7` | signed 64-bit |
| UInt | `12345` | unsigned 64-bit |
| Float | `3.14`, `1.0e21` | 64-bit, scientific notation supported. `inf`/`nan` not representable |
| String | `"text"`, `'text'` | single or double quotes |
| Hex | `0xFF5733` | `0x`/`0X` prefix marks it as hex |
| Version | `1.0.0`, `1.0.0.0` | three- or four-component dotted numeric version |
| Timestamp | `2024-12-24T15:30:00`, `2024-12-24`, `15:30:00` | ISO-8601 date, time, or both |
| Null / Nil | `null`, `nil` | absence of a value, `nil` is an alias of `null` |

## Packs

Numbers, bools, strings, hex, versions, or timestamps joined with `|` form a pack: one atomic value with N
uniform components. All components must share a type. They can implicitly widen from int to float for example,
but still must be a single type. A mix that can't widen, like `1|true` or `1|"a"`, is an error.

```fdf
resolution = 1920|1080             // 2 components
scale      = 1.0|1.0|1.0           // 3 components
gradient   = 1|50|10|1             // 4 components
five       = 1|2|3|4|5             // 5 components
offset     = 0.5|-0.5|1.0          // widening is fine
flags      = true|false            // bool pack
tags       = "config"|"a|b"|'x'    // string pack, a '|' inside quotes is literal
channels   = 0xFF|0x80|0x40        // hex pack
versions   = 1.0.0|1.1.0.0         // 3 and 4 components can mix
window     = 2024-12-24|2024-12-31 // timestamp pack
```

Version components are `uint32_t`, except `major` is limited to `0..2147483647`.
`1.2.3` and `1.2.3.0` stay distinct.

A pack is not an array. A pack is a single value with N interchangeable components and no element
identity: no per-component comments, no nesting, no `Entry` per component. An array (`[ ]`) is a
container of entries, each an addressable node in its own right. Use a pack for a coordinate,
color channel set, or dimension tuple, an array for a list of distinct things.

## Strings

- Either `"double"` or `'single'` quotes
- Escapes like `\t`, `\n`, `\"`, `\'`, `\\`, and so on (`\u`/`\U` are kept literal, not decoded)
- UTF-8 content passes through byte-for-byte, never normalized or re-escaped. A leading BOM gets
  stripped, malformed UTF-8 still parses but you get a non-fatal `InvalidUtf8` warning. Want to
  check up front, use `fdf::IsValidUtf8(sv)` or `String::IsValidUtf8()`

```fdf
escaped1 = "She said, \"Hello.\""
escaped2 = 'It\'s fine'
path     = "C:/TestFolder/file.txt"
```

Reading these from C++ is covered in [API](API.md).
