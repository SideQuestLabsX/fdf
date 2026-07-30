# Changelog

This file records user-visible changes to fdf and follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Run `fdf-validate` on existing documents. It reports syntax that needs updating.

## [0.2.1] - 2026-07-30

### Added

- `Entry::GetValue<T>` can now look up an entry and return its value in one call. It returns an
  empty span or view if it can't find the entry or the type is wrong.
- Hex transfers now handle `std::array`, `std::span` and raw `std::byte` values. `HexReader` can
  report or skip unread bytes.
- `FDF_NO_FILE_IO` removes file APIs. `FDF_NO_STD_FORMAT` removes fdf's
  `std::formatter<fdf::String>` specialization. Assertion-free builds omit the direct C standard I/O
  includes used by failure reporting.
- `MAX_IDENTIFIER_LENGTH` exposes the configured identifier limit.
- Added `ToString` overloads for `Type`, `DiagnosticSeverity` and `DiagnosticType`.
- Added `Entry::INVALID_CHILD_INDEX` for `FindChildIndex` misses.
- The test runner can run one named case with `--case <name>`.
- Added `Duration` overloads for `+=`, `-=`, `*=` and `scalar * duration`.

### Fixed

- Exposed the documented `fdf::IsValidUtf8` function in the public namespace.

## [0.2.0] - 2026-07-21

### Added

- `fdf-validate`, a command-line validator that reads files or stdin and reports
  `path:line:column` diagnostics. Its `--round-trip` check catches output that does not settle after
  one write. Linux and Windows binaries ship with the release.
- Syntax packages for VS Code, Sublime Text, JetBrains IDEs and TextMate, all generated from one
  TextMate grammar.
- An `fdf-<version>.zip` archive with the header, module source, validator binaries, editor packages
  and license under one versioned root.
- `Duration` values with exact unit syntax such as `1h30m`, `1.5h` and `-30m`.
- `_` digit separators for Int, Float and Hex values. `0x` represents an empty hex byte string.
- `fdf::IsValidUtf8` and `fdf::String::IsValidUtf8` for explicit UTF-8 checks. Malformed UTF-8 also
  produces a non-fatal parse diagnostic.
- `DuplicateKeyPolicy` for configurable duplicate map key handling.
- `FDF_ASSERTIONS`, a CMake setting that can follow `NDEBUG` or force library contract checks on or
  off.

### Changed

- The writer uses `k = v` by default. Set `Style::bSpaceBeforeAndAfterEqualSign` to `false` to
  write `k=v`.
- Packs use `|` and support every scalar type except Null. Each pack remains one value with mutable
  span access.
- String values use `fdf::String`, with standard method names, mutable access and implicit
  `std::string_view` conversion. Its `substr` returns a non-owning view.
- Version, Timestamp and Hex values use dedicated mutable types with span access. Timestamps expose
  named date, time and zone fields. Hex values own their bytes and provide big-endian read and write
  helpers.
- Timestamps follow an RFC 3339 profile. Ordinal dates, ISO week dates, fractions longer than nine
  digits and zones without a date are rejected.
- Diagnostic callbacks are regular trailing arguments now (was NTTP), so capturing lambdas and
  stateful callbacks works.
- Container nesting past 256 levels produces `NestingTooDeep`. The parser drops deeper containers
  and continues.
- `WriteFile` writes to a sibling temporary before replacing the destination.
- MSVC builds require `/Zc:preprocessor`. The CMake target sets it automatically and the header
  reports a clear error when it is missing.

### Removed

- `Type::UInt`. Use `GetValue<uint64_t>()` to get an `fdf::UIntSpan` over the same 64-bit Int
  storage. Values above `2^63-1` keep their bits but serialize in signed form.
- The trailing `#` on hex values, the `x` pack separator. Hex output always uses whole bytes,
  so `0xABC` writes as `0x0ABC`.
- `Entry` move construction and assignment. Relocate subtrees with `OrphanChild` and `AddChild`.

### Fixed

- Fixed comment round-trip bugs found by fuzzing: a crash while reporting diagnostics at EOF,
  length underflow for `/*/`, lost fragments in wrapped comments, unstable whitespace wrapping and
  `//#x` being mistaken for the file comment.

## [0.1.1] - 2026-07-10

### Changed

- Value classification moved from the lexer to the parser. More malformed values now produce
  recoverable diagnostics instead of stopping tokenization.

### Fixed

- Mixed numeric values widen correctly without corrupting memory.
- Inputs at or beyond the 32-bit offset limit are rejected.
- Diagnostic columns no longer overflow on lines longer than 65,535 characters.

## [0.1.0] - 2026-06-27

First tagged release.
