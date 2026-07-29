# FAQ

This page keeps the answers short. The other docs go deeper:
[Syntax](Syntax.md), [Types](Types.md), [API](API.md), [Styling](Styling.md),
[Diagnostics](Diagnostics.md), [Building](Building.md).

## What is fdf and when should I use it?

fdf is for the stuff you'd normally put in JSON, YAML, TOML or INI: config, asset metadata and
similar hand-edited files. It's meant to be read and edited by hand. Comments stick around through
a parse and write. Versions and timestamps are proper types. Most of the library works at compile
time too, apart from file I/O.

It's aimed at human-facing config. A binary format for embedding and network transfer is planned.

## What does a file actually look like?

```fdf
appName    = "MyGame"
appVersion = 1.4.2.0
lastOpened = 2024-12-24T15:30:00

graphics
{
    resolution = 1920|1080
    fullscreen = true

    // tried in order until one works
    backends[ "vulkan", "d3d12", "opengl" ]
}
```

Top level is a map. An entry is a name, then either a scalar after `=`, a map in `{ }` or an array
in `[ ]`. Whitespace outside strings does not matter. Lay out the file however it reads best.

## What types are there?

fdf figures out the type from the value. You don't write a type annotation.

| Type | Example |
|------|---------|
| Bool | `true`, `false` |
| Int | `12345`, `-7` (64-bit, unsigned range readable via `GetValue<uint64_t>()`) |
| Float | `3.14`, `1.0e21` (64-bit) |
| String | `"text"` or `'text'` |
| Hex | `0xFF5733` |
| Version | `1.2.3` or `1.2.3.0` |
| Timestamp | `2024-12-24T15:30:00` or just a date or time (RFC 3339 profile) |
| Duration | `90s`, `1h30m`, `-500ms` |
| Null / Nil | `null`, `nil` (same thing) |

Plus packs, below.

## What's a "pack"? What's the `|` for?

A pack is one value made of several same-typed parts joined with `|`:

```fdf
resolution = 1920|1080      // one value, two components
color      = 0xFF|0x80|0x40
```

`resolution` is a single two-component value. Packs suit values like coordinates or color channels.
All components must share a type. Integers can widen to floats. A mixed pack such as `1|true` is
invalid. Use an array when the parts need comments, nesting or individual addressing.

## Does it keep my comments?

Yes. Comments stay attached to their entries through a parse and write. The API can read and edit
them too. Building with `FDF_NO_COMMENTS` removes comment storage for a smaller footprint.

Comments are `//` for a line and `/* */` for a block. A block comment starting with `#`
(`/*# ... */`) at the very top of the file is the file comment.

## Will it preserve my exact formatting?

Data and comments survive a write. Spacing and brace placement get rewritten using the style you
pick (see [Styling](Styling.md)). Write, parse and write again with the same style and the second
output is identical. Keeps diffs clean.

## How do I read a file from C++?

Call `ParseFile` with the file name. It returns the root entry, or `nullptr` if the file can't be
read or parsing stops on a fatal error.

```cpp
fdf::UniqueEntryPtr doc = fdf::ParseFile("config.fdf");
if(!doc)
    return;

auto resolution = doc->GetValue<int64_t>("graphics.resolution");
if(resolution.size() != 2)
    return;

int width = int(resolution[0]);
int height = int(resolution[1]);
```

`GetValue<T>` returns an empty result if it can't find the entry or the type is wrong. Use
`GetChild` when you need the entry itself or want to know what went wrong.

## Can I really parse it at compile time?

Yeah. `ParseBuffer`, the writer and the whole `Entry` API are `constexpr`, so you can parse, poke
at and serialize a document inside a `consteval` function:

```cpp
consteval int64_t ReadScore()
{
    auto doc = fdf::ParseBuffer("score = 42\n");
    return doc->GetValue<int64_t>("score")[0];
}
static_assert(ReadScore() == 42);
```

Use `ParseBuffer`, not `ParseFile`, at compile time. C++ does not allow file I/O during constant
evaluation.

## How do I build a document and write it out?

```cpp
auto doc = fdf::NewEntry();
doc->Emplace("name")->SetValue("MyGame");
doc->Emplace("score")->SetValue(42);

if(!fdf::WriteFile(*doc, "out.fdf"))
{
    // handle the error
}
```

For arrays and nested maps you set the node to a container first and then `Emplace` into it. The
[API](API.md) page has the full editing surface.

## What happens if my file has an error?

Depends how broken it is. A malformed entry (a bad value, a busted key) gets skipped and reported, and
parsing keeps going, so one bad line doesn't sink the whole file. Something the lexer genuinely can't
make sense of, like an unterminated string or comment, stops the parse.

To actually see what went wrong, pass a diagnostic callback:

```cpp
constexpr void OnDiag(const fdf::Diagnostic& d) { /* log it */ }
auto doc = fdf::ParseFile<&OnDiag>("config.fdf");
```

Same callback works at compile time. See [Diagnostics](Diagnostics.md).

## How does it handle Unicode and non-ASCII text?

fdf files are UTF-8. The parser preserves their bytes without normalization or re-escaping and strips
a leading byte-order mark. Invalid UTF-8 still parses unchanged, but the diagnostic callback receives
a non-fatal warning. Check input with `fdf::IsValidUtf8(text)` or `String::IsValidUtf8()`.

`\u`/`\U` escapes remain literal text. Write the actual UTF-8 character to store it in the value.

## Is it thread-safe?

Keep fdf on one thread for now. The allocator uses shared global state, so even separate files can
race.

## Is there a binary format?

Right now fdf is text only. A compact binary form for embedding and network transfer is planned.

## Can I use it from C, Python or another language?

Right now it's C++ only and needs a C++26-capable compiler. A stable C ABI is planned as the base for
other language bindings once the API and value layouts settle.

## Is the format stable?

fdf is still pre-1.0 and parts of the format and API are moving. If you use it now, pin a release.
A spec is planned too.

## How do I add it to my project?

fdf is a single header. Put [fdf.h](../include/fdf.h) on your project's include path, then
`#include "fdf.h"` and build with a C++26-capable compiler. The optional module build supports
`import fdf;`. [Building](Building.md) covers requirements and options such as `FDF_NO_COMMENTS`.

## Why the 30-character limit on names?

Identifiers are stored inline with each node, which keeps nodes small and lookups cache-friendly.
The layout has room for 30 characters. If that's too short, shorten the name or add another nesting
level. Turning comments off raises the limit to 38. That's only useful when you already don't need
stored comments.

## Does FDF support schemas or automatic struct serialization?

They're both planned around C++26 reflection, but the design is still open.
