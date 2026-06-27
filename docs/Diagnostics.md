# Diagnostics

When parsing hits a malformed entry it skips that entry, reports it, and keeps going, so
one bad line doesn't sink the whole file. A token that can't be made sense of at all
stops parsing, since there's no safe place to resume.

To receive the reports, pass a callback as the `DIAG` template argument of `ParseFile` /
`ParseBuffer`

```cpp
constexpr void OnDiagnostic(const fdf::Diagnostic& d) { /* log it */ }

auto root = fdf::ParseFile<&OnDiagnostic>("config.fdf");
```

Because `DIAG` is a template argument, diagnostics work the same at runtime and at
compile time.
