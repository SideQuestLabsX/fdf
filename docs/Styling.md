# Styling

`WriteFile` / `WriteBuffer` take a compile-time `fdf::Style`. The defaults stay readable
and keep diffs small. Pass a designated initializer to change just what you want, like
`WriteFile<fdf::Style{ .bCommas = false }>(root, "out.fdf")`

| Field | Default | Effect |
|-------|---------|--------|
| `bUseSpacesOverTabs` / `tabSize` | `true` / `4` | indentation |
| `bSpaceBeforeAndAfterEqualSign` | `false` | `k=v` vs `k = v` |
| `bParenthesesOnNewLine` | `true` | brace on its own line |
| `bFileComment` / `bEntryComment` | `true` | emit comments |
| `bAlignCloseComments` | `true` | pad inline comments to a shared column |
| `singleLineCommentLimit` | `80` | chars before a comment wraps to its own line |
| `singleLineContainerLimit` | `80` | chars before a container goes multi-line |
| `bCommas` | `true` | trailing comma on multi-line entries |
| `bTopLevelCommas` | `false` | comma-terminate top-level entries |
| `bGroupSimilarTypes` | `false` | off keeps source order, on groups by type |
| `bUppercaseHex` | `true` | `0xFF` vs `0xff` |
| `bUseNilInsteadOfNull` | `false` | emit `nil` instead of `null` |
| `bAlwaysUseDoubleQuoteForStrings` | `false` | force `"` quoting |

The `STYLE` is a template argument, so it's all resolved at compile time.
