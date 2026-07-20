# Styling

`WriteFile` and `WriteBuffer` take a compile-time `fdf::Style`. The defaults are readable
and keep diffs small. Use a designated initializer to change individual fields, for example:

`WriteFile<fdf::Style{ .bCommas = false }>(root, "out.fdf")`

| Field | Default | Effect |
|-------|---------|--------|
| `bUseSpacesOverTabs` / `tabSize` | `true` / `4` | indentation |
| `bSpaceBeforeAndAfterEqualSign` | `false` | `k=v` vs `k = v` |
| `bParenthesesOnNewLine` | `true` | brace on its own line |
| `bFileComment` / `bEntryComment` | `true` | emit comments |
| `bAlignCloseComments` | `true` | pad inline comments to a shared column |
| `singleLineCommentLimit` | `80` | longest comment kept inline, longer ones move to their own line and wrap |
| `singleLineContainerLimit` | `80` | chars before a container goes multi-line |
| `bCommas` | `true` | trailing comma on multi-line entries |
| `bTopLevelCommas` | `false` | comma-terminate top-level entries |
| `bGroupSimilarTypes` | `false` | off keeps source order, on groups by type |
| `bUppercaseHex` | `true` | `0xFF` vs `0xff` |
| `intDigitGrouping` | `0` | group Int and Float integer-part digits every N from the right, `0` is off |
| `hexDigitGrouping` | `0` | group Hex digits after `0x` every N from the right, `0` is off |
| `bUppercaseTimestamp` | `true` | `T`/`Z` vs `t`/`z` |
| `bUseNilInsteadOfNull` | `false` | emit `nil` for null values |
| `bAlwaysUseDoubleQuoteForStrings` | `false` | force `"` quoting |

`STYLE` is a template argument, so a style can't be picked at runtime.

Hex writes whole bytes, so an odd-length literal like `0xABC` is emitted as `0x0ABC`. Both literals
represent the same bytes.

Parsing discards digit separators. The writer adds them when `intDigitGrouping` or
`hexDigitGrouping` is set and derives the grouping itself.

A comment longer than `singleLineCommentLimit` moves to its own line and wraps as a block comment
rather than a run of `//` lines. Consecutive `//` lines read back as separate comments and only the
last survives, so the block form is what makes a wrapped comment round-trip. Breaks land on a space
whose next character is not whitespace, since re-parsing collapses a newline and everything after it
into one space. A stretch with no such break point stays on one long line.
