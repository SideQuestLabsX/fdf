# Types

## Scalars

| Type | Examples | Notes |
|------|----------|-------|
| Bool | `true`, `false` | |
| Int | `12345`, `-7` | 64-bit, literals accepted in `-2^63..2^64-1` |
| Float | `3.14`, `1.0e21` | 64-bit, scientific notation supported. `inf`/`nan` not representable |
| String | `"text"`, `'text'` | single or double quotes |
| Hex | `0xFF5733`, `0xABC` and `0x` | `0x`/`0X` prefix marks it as hex, stored as bytes |
| Version | `1.0.0`, `1.0.0.0` | three- or four-component dotted numeric version |
| Timestamp | `2024-12-24T15:30:00Z`, `2024-12-24`, `15:30:00` | RFC 3339-profile date, time or datetime |
| Duration | `1h30m`, `1.5h`, `-30m` | signed 64-bit nanosecond time span |
| Null / Nil | `null`, `nil` | absence of a value, `nil` is an alias of `null` |

Timestamp syntax is restricted to these forms:

| Shape | Syntax |
|---|---|
| Date | `YYYY-MM-DD` |
| Time | `HH:MM:SS` with optional `.d{1,9}` |
| Datetime | `<date>T<time>[.frac]` with `T` or `t` and an optional zone |
| Zone | `Z`, `z`, `+HH:MM` or `-HH:MM` |

Ordinal and ISO week dates are not accepted. A leap second (`60`) is preserved in text and
converts to the first second of the following minute. A time-only value takes no zone.

RFC 3339 §4.3 gives `-00:00` a special meaning: the instant is UTC, but its local offset is unknown.
`+00:00` states that the local offset is UTC. FDF stores `UnknownOffset`, `Offset` and `Utc`
separately. They compare unequal and round-trip independently, but convert to the same epoch second.

Int stores 64 bits with no separate unsigned type. Literals above `2^63-1` keep their unsigned
bit pattern and read back exactly through `GetValue<uint64_t>()`. They re-serialize in signed
form: `18446744073709551615` is written back as `-1`, same bits.

Hex is a byte string. Length is part of the value, so `0x00FFFF` and `0xFFFF` differ, and `0x` is the
zero-byte value. Odd-length literals are accepted: `0xABC` stores `0A BC` with an implied zero
leading nibble and comes back as `0x0ABC`. Digit case is a writer style choice.

Timestamp values are stored as contiguous 16-byte `Timestamp` structs. `GetValue<Timestamp>()`
returns a mutable span over those structs. The named fields define the value, so do not persist the
compiler-specific bitfield object representation.

## Duration

A duration is an optional leading `-` followed by one or more number and unit groups. Units must
appear from largest to smallest and may occur at most once.

| Unit | Meaning |
|---|---|
| `w` | week |
| `d` | day |
| `h` | hour |
| `m` | minute |
| `s` | second |
| `ms` | millisecond |
| `us` | microsecond |
| `ns` | nanosecond |

Month and year durations depend on a calendar, so the supported units stop at weeks. Fractions are
accepted on any unit when the complete value resolves to a whole number of nanoseconds. The total
must fit a signed 64-bit nanosecond count.

The writer expands weeks into days and emits integer components from largest to smallest. Zero is
`0s`. For example, `90m` and `1.5h` both write as `1h30m`, while `2w` writes as `14d`.

## Packs

Numbers, bools, strings, hex, versions, timestamps or durations joined with `|` form a pack:
one atomic value with N uniform components. All components must share a type. Integers can
widen to floats, but a mix with no common type, such as `1|true` or `1|"a"`, is an error.

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
timeouts   = 1h|2h|30m
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
