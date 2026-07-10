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
| Version | `1.0.0.0` | dotted version literal |
| Timestamp | `2024-12-24T15:30:00`, `2024-12-24`, `15:30:00` | ISO-8601 date, time, or both |
| Null / Nil | `null`, `nil` | absence of a value, `nil` is an alias of `null` |

## Packs

Numbers or bools joined with `|` form a pack: one atomic value with N uniform components. Two or
more components, no fixed upper bound. All components must share a type. Numerics widen to reach
that uniformity (any float makes the pack float, a value past the signed range makes it unsigned, a
negative makes it signed); a mix that can't widen, like `1|true`, is an error.

```fdf
resolution = 1920|1080      // 2 components
scale      = 1.0|1.0|1.0    // 3 components
gradient   = 1|50|10|1      // 4 components
five       = 1|2|3|4|5      // 5 components
offset     = 0.5|-0.5|1.0   // widening is fine
flags      = true|false     // bool pack
```

A pack is not an array. A pack is a single value with N interchangeable components and no element
identity: no per-component comments, no nesting, no `Entry` per component. An array (`[ ]`) is a
container of entries, each an addressable node in its own right. Use a pack for a coordinate,
color channel set, or dimension tuple, an array for a list of distinct things.

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
