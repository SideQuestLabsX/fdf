# fdf (Flexible Data Format)

A text data format for the kind of thing you'd normally use JSON, YAML, TOML or INI for:
config, asset metadata, and so on. Designed to be frequently read and modified by hand

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

- Header only (with C++ modules support)
- Human first design
- Comments preserved, read and write them through the API
- A lot of built-in types (timestamp, hex, ...)
- Round-trip guarantee
- Extensive styling options
- fully constexpr

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
- C++26 reflection to serialize from/to C++ structs
- Validating a file against an expected shape, ideally with C++26 reflection too
- Syntax highlighting (maybe LSP reusing the parser and writer)

## License

Public domain ([Unlicense](LICENSE)), do whatever you want with it
