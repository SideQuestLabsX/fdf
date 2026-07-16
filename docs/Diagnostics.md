# Diagnostics

When the parser finds a malformed entry, it reports and skips that entry, then continues.
A token the lexer cannot interpret stops parsing because there is no safe place to resume.

To receive the reports, pass a callback as the `DIAG` template argument of `ParseFile` or
`ParseBuffer`.

```cpp
constexpr void OnDiagnostic(const fdf::Diagnostic& d) { /* log it */ }

auto root = fdf::ParseFile<&OnDiagnostic>("config.fdf");
```

Diagnostics work the same at runtime and at compile time.

## Diagnostic fields

| Field | Meaning |
|-------|---------|
| `severity` | `Warning` keeps all data, `Error` skips the entry, `Fatal` aborts the parse |
| `type` | which problem was found, see below |
| `message` | the offending text, or a short description |
| `line`, `column` | position of the offending token, 1-based |
| `offset` | byte offset into the parsed buffer, 0-based |

## Diagnostic types

| Type | Severity | Meaning |
|------|----------|---------|
| `AlreadyHasComment` | Warning | second comment on one entry, the later comment wins |
| `InvalidUtf8` | Warning | malformed UTF-8, bytes still pass through, `offset` points at the first bad byte |
| `UnexpectedToken` | Error | token out of place, e.g. `=` before a container |
| `InvalidIdentifier` | Error | bad key: over 30 chars, a keyword, a stray character or a leading digit |
| `InvalidNumber` | Error | malformed numeric value |
| `InvalidPack` | Error | dangling `\|` or components with no common type |
| `InvalidTimestamp` | Error | ISO-8601 structure or range violation |
| `UnexpectedEndOfFile` | Fatal | input ends inside a container |
| `UnterminatedString` | Fatal | missing closing quote |
| `UnterminatedComment` | Fatal | block comment missing `*/` |
| `InvalidComment` | Fatal | `/` not followed by `/` or `*` |
| `InvalidToken` | Fatal | lexer failure with no more specific reason |
| `InputTooLarge` | Fatal | input would overflow the 32-bit offsets, refused before parsing |
