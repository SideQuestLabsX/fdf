# FAQ

Common questions about fdf. If something here is out of date or missing, the other docs go deeper:
[Syntax](Syntax.md), [Types](Types.md), [API](API.md), [Styling](Styling.md),
[Diagnostics](Diagnostics.md), [Building](Building.md).

## What is fdf, and why not just use JSON or YAML or TOML?

fdf is a text data format for the stuff you'd normally reach for JSON, YAML, TOML or INI for:
config, asset metadata, that kind of thing. The difference is it's built to be read and edited by
hand first. Comments stick around instead of getting thrown away. Types such as versions and timestamps
are first class citizens. The entire library (minus file IO) can run at compile time.

fdf is a human-facing config format, not a wire format or database. A binary format is planned.

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

Top level is a map. An entry is a name, then either a scalar after `=`, a map in `{ }`, or an array
in `[ ]`. Whitespace outside strings does not matter. Lay out the file however it reads best.

## What types are there?

fdf figures out the type from the value, there is no type annotation.

| Type | Example |
|------|---------|
| Bool | `true`, `false` |
| Int | `12345`, `-7` (64-bit, unsigned range readable via `GetValue<uint64_t>()`) |
| Float | `3.14`, `1.0e21` (64-bit) |
| String | `"text"` or `'text'` |
| Hex | `0xFF5733` |
| Version | `1.2.3` or `1.2.3.0` |
| Timestamp | `2024-12-24T15:30:00`, or just a date or time (RFC 3339 profile) |
| Duration | `90s`, `1h30m`, `-500ms` |
| Null / Nil | `null`, `nil` (same thing) |

Plus packs, below.

## What's a "pack"? What's the `|` for?

A pack is one value made of several same-typed parts joined with `|`:

```fdf
resolution = 1920|1080      // one value, two components
color      = 0xFF|0x80|0x40
```

`resolution` is one value with two components, not an array of two entries. Packs suit values like
coordinates or color channels. All components must share a type. Integers can widen to floats, but
`1|true` is invalid. When the parts need comments, nesting or individual addressing, use an array
instead.

## Does it keep my comments?

Yes. Comments stay attached to their entries through a parse and write. The API can read and edit
them too. Building with `FDF_NO_COMMENTS` removes comment storage for a smaller footprint.

Comments are `//` for a line and `/* */` for a block. A block comment starting with `#`
(`/*# ... */`) at the very top of the file is the file comment.

## Will it preserve my exact formatting?

Your data and comments, yes. Your exact spacing and brace placement, no, and that's on purpose. When
you write a document back out it gets re-emitted in a consistent style that you control (see
[Styling](Styling.md)). Write it, parse it, write it again with the same style and you get identical
text, so diffs stay clean and reviewable.

## How do I read a file from C++?

Parse it, then walk to what you want. `GetChild` takes a dotted path, `GetValue<T>()` hands back a
span over the components (one element for a plain scalar, more for a pack):

```cpp
auto doc = fdf::ParseFile("config.fdf");
if(!doc)
    return;   // parse failed outright

if(auto* e = doc->GetChild("graphics.resolution"))
{
    auto res = e->GetValue<int64_t>();     // [1920, 1080]
    if(res.size() == 2)
    {
        int w = int(res[0]);
        int h = int(res[1]);
    }
}
```

Always check for null. `GetChild` returns `nullptr` when the path isn't there.

## Can I really parse it at compile time?

Yeah. `ParseBuffer`, the writer and the whole `Entry` API are `constexpr`, so you can parse, poke
at and serialize a document inside a `consteval` function:

```cpp
consteval int64_t ReadScore()
{
    auto doc = fdf::ParseBuffer("score = 42\n");
    return doc->GetChild("score")->GetValue<int64_t>()[0];
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

One thing to know: `\u`/`\U` escapes are kept as literal text right now, they aren't decoded into
characters. Write the actual UTF-8 character if you want it in the value.

## Is it thread-safe?

Not yet, so treat it as single-threaded for now. The allocator underneath uses shared global state,
which means even parsing two separate files on two threads can race.

## Is there a binary format?

Not yet. fdf is text-only. A compact binary form for embedding and network transfer is planned but
not implemented.

## Can I use it from C, Python or another language?

Not yet. The library is C++ only and requires a C++26-capable compiler. A stable C ABI is planned as
the base for other language bindings, after the API and value layouts settle.

## Is the format stable?

fdf is still pre-1.0 and parts of the format and API are still evolving. No stability promises are made,
but if you want to start using it, pin to a release version. A spec is also planned.

## How do I add it to my project?

fdf is a single header. Put [fdf.h](../include/fdf.h) on your project's include path, then
`#include "fdf.h"` and build with a C++26-capable compiler. The optional module build supports
`import fdf;`. [Building](Building.md) covers requirements and options such as `FDF_NO_COMMENTS`.

## Why the 30-character limit on names?

Identifiers are stored inline with each node, which keeps nodes small and lookups cache-friendly.
The layout has room for 30 characters. If that is not enough, shorten the name or add another nesting
level. Disabling comments raises the limit to 38, but do not disable them solely for longer names.

## Does FDF support schemas or automatic struct serialization?

Not yet. Both features are planned around C++26 reflection, but the design is still open.
