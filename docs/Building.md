# Building

The library is header only. You can copy [`fdf.h`](../include/fdf.h) into your project and include it.
It doesn't require an implementation .cpp file.

If you prefer you can also use CMake and optionally build C++ as module:

```cmake
include(FetchContent)
FetchContent_Declare(
    fdf
    GIT_REPOSITORY https://github.com/SideQuestLabsX/fdf.git
    GIT_TAG        main   # or a release tag like v0.1.0
)

# optional, build the C++ module instead of header-only
# set(FDF_USE_CPP_MODULES ON)

FetchContent_MakeAvailable(fdf)

target_link_libraries(your_target PRIVATE fdf)
```

## Building the tests

```sh
cmake -B build .
cmake --build build
ctest --test-dir build --verbose
```

## Options

All off by default

| Option | What it does |
|--------|--------------|
| `FDF_USE_CPP_MODULES` | build the `fdf` module instead of header-only |
| `FDF_NO_COMMENTS` | drop comment storage for a smaller node |
| `FDF_EXTENDED_NO_COMMENT_IDENTIFIERS` | with comments off, allow 38-char identifiers instead of 30 |
