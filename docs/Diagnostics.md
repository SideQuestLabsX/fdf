# Diagnostics

When the parser finds a malformed entry, it reports and skips that entry, then continues.
A token the lexer cannot interpret stops parsing because the parser cannot identify a reliable
next-entry boundary.

Pass any callable taking `const Diagnostic&` to `ParseFile` or `ParseBuffer`. Lambdas can capture
state as usual.

```cpp
int errors = 0;
auto root = fdf::ParseFile("config.fdf", [&](const fdf::Diagnostic& d)
{
    errors += d.severity != fdf::DiagnosticSeverity::Warning;
});
```

Every overload that accepts a sink keeps it last, leaving inline lambdas at the end of the call:

```cpp
root->ParseCombineBuffer(text);
root->ParseCombineBuffer(text, fdf::CommentCombineStrategy::UseNew);
root->ParseCombineBuffer(text, [&](const fdf::Diagnostic& d) { errors++; });
root->ParseCombineBuffer(text, fdf::CommentCombineStrategy::UseNew, fdf::DuplicateKeyPolicy::KeepFirst, [&](const fdf::Diagnostic& d) { errors++; });
```

`ParseCombineFile` and `ParseCombineBuffer` use overloads to keep the sink last. A strategy in the
second position selects the no-sink overload. Without a sink, diagnostic calls compile out.

Diagnostics work the same at runtime and at compile time.

## Diagnostic fields

| Field | Meaning |
|-------|---------|
| `severity` | `Warning` keeps all data, `Error` skips the entry, `Fatal` aborts the parse |
| `type` | which problem was found, see below |
| `message` | the offending text or a short description |
| `line`, `column` | position of the offending token, 1-based |
| `offset` | byte offset into the parsed buffer, 0-based |

## Diagnostic types

| Type | Severity | Meaning |
|------|----------|---------|
| `AlreadyHasComment` | Warning | second comment on one entry, the later comment wins |
| `InvalidUtf8` | Warning | malformed UTF-8, bytes still pass through, `offset` points at the first bad byte |
| `UnexpectedToken` | Error | token out of place, e.g. `=` before a container |
| `InvalidIdentifier` | Error | bad key: over the configured limit, a keyword, a stray character or a leading digit |
| `InvalidNumber` | Error | malformed numeric value or a float literal that overflows to infinity |
| `InvalidPack` | Error | dangling `\|` or components with no common type |
| `InvalidTimestamp` | Error | RFC 3339 profile structure or range violation |
| `InvalidDuration` | Error | malformed, out-of-order, non-integral or overflowing duration |
| `DuplicateKey` | Error | repeated direct map key, the duplicate is dropped and parsing continues |
| `NestingTooDeep` | Error | container nesting past 256 levels, the inner levels are dropped and parsing continues |
| `UnexpectedEndOfFile` | Fatal | input ends inside a container |
| `UnterminatedString` | Fatal | missing closing quote |
| `UnterminatedComment` | Fatal | block comment missing `*/` |
| `InvalidComment` | Fatal | `/` not followed by `/` or `*` |
| `InvalidToken` | Fatal | lexer failure with no more specific reason |
| `InputTooLarge` | Fatal | input would overflow the 32-bit offsets, refused before parsing |

## Nesting limit

Parsing recurses once per container and stops at 256 levels. Past the limit, the parser reports
`NestingTooDeep`, drops the deeper containers and continues with the outer structure and later
entries. A caller that ignores diagnostics receives an incomplete tree, so untrusted input should
always use a diagnostic callback.
