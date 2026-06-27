# fdf (Flexible Data Format)

A text data format for the kind of thing you'd normally use JSON, YAML, TOML or INI for:
config, asset metadata, and so on. It's built to be read and edited by hand, so it stays
easy on the eyes.

A single-header C++26 library that works at runtime and at compile time. Header-only by
default, with an optional C++ modules build if you'd rather `import fdf;`. A binary form
for embedding and network transfer is planned.

```fdf
name       = "MyGame"
version    = 1.0.0.0
resolution = 1920x1080
fullscreen = true

window
{
    title = "Main"
    pos   = 100x100
    flags [ "resizable", "vsync" ]
}

items
[
    { id = 1, name = "Potion" }
    { id = 2, name = "Elixir" }
]
```

## Features

- Comments are first-class citizens (read and write them through the API)
- A lot of built-in types
- Round-trip guarantee
- Extensive styling options for the output
- Works at runtime and at compile time

## Quick start

Header-only, drop [`include/fdf.h`](include/fdf.h) on your include path and include it

### Reading

```cpp
#include "fdf.h"

fdf::UniqueEntryPtr root = fdf::ParseFile("config.fdf");
if(root)
{
    auto name  = root->GetChild("name")->GetValue<std::string_view>();
    auto title = root->GetChild("window.title")->GetValue<std::string_view>();

    root->ForEach<fdf::ForEachFlags::Recursive>([](const fdf::Entry& e)
    {
        // visit every node
    });
}
```

### Building and writing

```cpp
fdf::UniqueEntryPtr root = fdf::NewEntry();

if(fdf::Entry* e = root->Emplace("name"))
    e->SetValue("MyGame");
if(fdf::Entry* e = root->Emplace("score"))
    e->SetValue(42);

if(fdf::Entry* arr = root->Emplace("levels"))
{
    arr->SetValue(fdf::ArrayType());
    arr->Emplace("")->SetValue(1);
    arr->Emplace("")->SetValue(2);
}

fdf::WriteFile(*root, "out.fdf");                                  // default style
fdf::WriteFile<fdf::Style{ .bCommas = false }>(*root, "out.fdf");  // tweak the style
```

### At compile time

```cpp
consteval int64_t ReadAnswer()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("answer = 42\n");
    return root->GetChild("answer")->GetValue<int64_t>()[0];
}
static_assert(ReadAnswer() == 42);
```

## Building and testing

```sh
cmake -B build .
cmake --build build
ctest --test-dir build --verbose
```

## Docs

- [`docs/`](docs/README.md) is the reference, split by topic (syntax, types, API, styling, ...)
- [`examples/`](examples/README.md) is a set of real, tested fdf files to copy from

## Down the road

Rough ideas, nothing set in stone

- A compact binary representation for embedding and network transfer
- Leaning on C++26 reflection to map fdf straight onto your own structs
- Validating a file against an expected shape, ideally derived from those structs
- Editor support, syntax highlighting first, maybe an LSP later reusing the parser and writer

## License

Public domain ([Unlicense](LICENSE)), do whatever you want with it
