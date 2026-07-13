# FAQ

Common questions about fdf. If something here is out of date or missing, the other docs go deeper:
[Syntax](Syntax.md), [Types](Types.md), [API](API.md), [Styling](Styling.md),
[Diagnostics](Diagnostics.md), [Building](Building.md).

## What is fdf, and why not just use JSON or YAML or TOML?

fdf is a text data format for the stuff you'd normally reach for JSON, YAML, TOML, or INI for:
config, asset metadata, that kind of thing. The difference is it's built to be read and edited by
hand first. Comments stick around instead of getting thrown away, it has real types for things like
versions and timestamps so you're not quoting everything, and the whole parser and writer run at C++
compile time. If a file is mostly written and reviewed by people, that's the case fdf is for.

It's not trying to be a wire format or a database. It's a human-facing config format, but a binary format is planned.

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
in `[ ]`. Whitespace outside strings doesn't matter, lay it out however reads best.

## What types are there?

fdf figures out the type from the value, there is no type annotation.

- **Bool** `true`, `false`
- **Int** `12345`, `-7` (signed 64-bit)
- **UInt** `9223372036854775808` (unsigned 64-bit)
- **Float** `3.14`, `1.0e21` (64-bit)
- **String** `"text"` or `'text'`
- **Hex** `0xFF5733`
- **Version** `1.2.3` or `1.2.3.0`
- **Timestamp** `2024-12-24T15:30:00`, or just a date, or just a time (ISO-8601)
- **Null / Nil** `null`, `nil` (same thing)

Plus packs, below.

## What's a "pack"? What's the `|` for?

A pack is one value made of several same-typed parts joined with `|`:

```fdf
resolution = 1920|1080      // one value, two components
color      = 0xFF|0x80|0x40
```

`resolution` isn't an array of two entries, it's a single value with two components. It's intended for
coordinates, colors, dimensions, and other homogeneous values. All the parts have to be the same type
(ints will widen to floats, but you can't mix something like `1|true`). That's the difference from an array:
an array `[ ]` holds separate indexed entries, you can comment and nest, in contrast a pack is one atomic value.

## Does it keep my comments?

Yes. Comments stay attached to the entry they belong to and survive a parse-then-write round-trip, so
loading a file and saving it again won't quietly eat them. You can read and edit them from the API
too. The only exception is if you build with `FDF_NO_COMMENTS`, which drops comment storage entirely
for a smaller footprint.

Comments are `//` for a line and `/* */` for a block. A block comment starting with `#`
(`/*# ... */`) at the very top of the file is the file comment.

## Will it preserve my exact formatting?

Your data and comments, yes. Your exact spacing and brace placement, no, and that's on purpose. When
you write a document back out it gets re-emitted in a consistent style that you control (see
[Styling](Styling.md)). Write it, parse it, write it again with the same style and you get identical
text, so diffs stay clean and reviewable. If you were hoping for a byte-for-byte formatter that leaves
your whitespace exactly as typed, that's explicitly not what fdf does.

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

Always check for null, `GetChild` returns `nullptr` when the path isn't there.

## Can I really parse it at compile time?

Yeah. `ParseBuffer`, the writer, and the whole `Entry` API are `constexpr`, so you can parse, poke
at, and serialize a document inside a `consteval` function:

```cpp
consteval int64_t ReadScore()
{
    auto doc = fdf::ParseBuffer("score = 42\n");
    return doc->GetChild("score")->GetValue<int64_t>()[0];
}
static_assert(ReadScore() == 42);
```

Note that's `ParseBuffer`, not `ParseFile`. File IO is not allowed at compile time. That's a limitation
of the language, not the library.

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

fdf files are UTF-8, and your bytes pass through untouched, nothing gets normalized or re-escaped. A
leading byte-order mark is stripped. If the file isn't valid UTF-8 it still parses (fdf doesn't mangle
your bytes either way), but you'll get a non-fatal warning through the diagnostic callback. Want to
check yourself, there's `fdf::IsValidUtf8(text)` and `String::IsValidUtf8()`.

One thing to know: `\u`/`\U` escapes are kept as literal text right now, they aren't decoded into
characters. Write the actual UTF-8 character if you want it in the value.

## Is it thread-safe?

Not yet, so treat it as single-threaded for now. The allocator underneath uses shared global state,
which means even parsing two separate files on two threads can race.

## Is there a binary format?

Not yet. fdf is text-only today. A compact binary form for embedding and network transfer is a
planned goal, but it isn't implemented, so don't count on it if you need it now.

## Can I use it from C, Python, or another language?

Not yet, the library is C++ only today and needs a C++26 capable compiler. A stable C ABI is planned
as the foundation for other language bindings, but the API and value layouts need to settle first.

## Is the format stable?

fdf is still pre-1.0 and parts of the format and API are still evolving. No stability promises are made,
but if you want to start using it, pin to a release version. A spec is also planned.

## How do I add it to my project?

fdf is a single header. Drop [fdf.h](../include/fdf.h) into a directory on your project's include path,
then `#include "fdf.h"`, and build with a C++26-capable compiler. There's also an optional C++ module
build if you'd rather `import fdf;`. [Building](Building.md) covers the requirements and the build options (like `FDF_NO_COMMENTS`).

## Why the 30-character limit on names?

It's a deliberate tradeoff. Identifiers are stored inline right next to the node, which keeps lookups
cache-friendly and nodes a fixed small size, and 30 characters is what fits. 30 chars is a reasonable
limit for most use cases. If you hit that limit, you might choose to shorten your names or nest them
for more context. You can also get to 38 chars, if you disable comments, but that's not recommended,
if the only purpose is to get longer identifiers.

## Does FDF support schemas or automatic struct serialization?

Not yet, but they are planned. We plan to leverage C++26 reflection for this. Other means of
achieving such feature can be discussed.
