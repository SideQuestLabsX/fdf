# fdf (Flexible Data Format)

A text data format for the kind of thing you'd normally use JSON, YAML, TOML or INI for:
config, asset metadata and so on. Designed to be frequently read and modified by hand

```fdf
name       = "MyGame"
version    = 1.0.0.0
resolution = 1920|1080
fullscreen = true

window
{
    title = "Main"
    pos   = 100|100
    flags[ "resizable", "vsync" ]
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
- Comments are preserved and exposed through the API
- Built-in types (Hex, Version, Timestamp, Duration)
- Extensive styling options with predictable output and clean diffs
- Fully constexpr
- CLI validator and editor syntax highlighting

## Quick start

Add [`include/fdf.h`](include/fdf.h) to your include path and include it.

### Reading

```cpp
#include "fdf.h"

fdf::UniqueEntryPtr root = fdf::ParseFile("examples/config.fdf");
if(root)
{
    auto name  = root->GetValue<fdf::String>("appName");  // ["MyGame"]
    auto vsync = root->GetValue<bool>("graphics.vsync");  // [false]

    root->ForEach<fdf::ForEachFlags::Recursive>([](const fdf::Entry&)
    {
        // visit every node
    });
}
```

`GetValue<T>` returns a span-like view. It is empty if the entry can't be found or has a different
type.

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

fdf::WriteFile(*root, "out.fdf");
fdf::WriteFile<fdf::Style{ .bCommas = false }>(*root, "out.fdf");
```

### At compile time

```cpp
consteval int64_t ReadAnswer()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("answer = 42\n");
    return root->GetValue<int64_t>("answer")[0];
}
static_assert(ReadAnswer() == 42);
```

## Building and testing

```sh
cmake -B build .
cmake --build build
ctest --test-dir build --verbose
```

## Checking files

`fdf-validate` parses files and reports diagnostics. Build it with CMake or download a release
binary:

```sh
cmake --build build --target fdf_validate
./build/tools/fdf-validate --round-trip examples/config.fdf
```

It returns 0 for success, 1 for invalid or unreadable input and 2 for bad usage.

Releases include Linux and Windows validator binaries plus editor packages for VS Code, Sublime
Text, JetBrains IDEs and TextMate. See [`editors/`](editors/README.md) for installation. The
`fdf-<version>.zip` archive bundles them with the header, module source and license.

## Docs

- [`docs/`](docs/README.md) is the reference, split by topic (syntax, types, API, styling, ...)
- [`examples/`](examples/README.md) is a set of real, tested fdf files to copy from
- [`CHANGELOG.md`](CHANGELOG.md) records what changed between releases

## Ideas for later

- A compact binary representation for embedding and network transfer
- C++26 reflection-based conversion between FDF and C++ structs
- Validating a file against an expected shape
- An LSP for editor diagnostics

## License

Public domain ([Unlicense](LICENSE)), do whatever you want with it
