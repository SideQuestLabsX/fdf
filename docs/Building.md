# Building

fdf needs a C++26 compiler. It works at runtime and at compile time.

## Using it

It's header-only, so drop [`../include/fdf.h`](../include/fdf.h) on your include path and
include it

```cpp
#include "fdf.h"
```

There's also an optional C++ modules build if you'd rather `import fdf;`, turned on with
`FDF_USE_CPP_MODULES`.

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
