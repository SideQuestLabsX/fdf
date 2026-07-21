# Building

The library is header only. Copy [`fdf.h`](../include/fdf.h) into your project and include it.

CMake can also consume the header directly or build the optional C++ module:

```cmake
include(FetchContent)
FetchContent_Declare(
    fdf
    GIT_REPOSITORY https://github.com/SideQuestLabsX/fdf.git
    GIT_TAG        main   # or a release tag like v0.2.0
)

# optional module build
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

## Validator

`fdf-validate` parses documents and reports diagnostics. It builds with the tests:

```sh
cmake --build build --target fdf_validate
./build/tools/fdf-validate --round-trip config.fdf
```

It accepts file paths and reads stdin when passed `-`. Diagnostics use
`path:line:column: severity: message` on stderr. `--round-trip` writes each document, re-parses it
and checks that a second write is identical. Exit status is 0 for valid input, 1 for invalid or
unreadable input and 2 for bad usage. Prebuilt binaries are attached to each release.

## Options

All options default to `OFF`, except `FDF_ASSERTIONS` which defaults to `auto`.

| Option | What it does                                                           |
|--------|------------------------------------------------------------------------|
| `FDF_USE_CPP_MODULES` | build the `fdf` C++ module                                             |
| `FDF_ASSERTIONS` | contract checks for misuse and sanity: `auto`, `on` or `off`           |
| `FDF_NO_COMMENTS` | drop comment storage for a smaller node                                |
| `FDF_EXTENDED_NO_COMMENT_IDENTIFIERS` | with comments off, raise the identifier limit from 30 to 38 characters |
| `FDF_DISABLE_SLAB_ALLOCATOR` | use one `operator new` allocation per object for sanitizer diagnostics |
| `FDF_ENABLE_ASAN` | enable AddressSanitizer (includes leak detection on Linux)             |
| `FDF_ENABLE_LSAN` | enable standalone LeakSanitizer where supported                        |

`FDF_ASSERTIONS` controls checks for misuse and sanity. The `auto` setting follows
`NDEBUG`. The `on` and `off` settings override it. A failed check prints to stderr and aborts.
Without CMake, define it to `true` or `false` before including the header.

MSVC needs `/Zc:preprocessor`. The CMake target adds it. A direct `cl` invocation has to pass it,
and the header reports the missing flag directly.
