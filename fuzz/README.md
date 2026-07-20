# Fuzzing

CMake builds three parser targets from `fuzz_parse.cpp`, each with a different define:

| Target | Checks |
|--------|--------|
| `parse` | the parser survives arbitrary bytes |
| `roundtrip` | parse, write and reparse produce the same shape and byte-identical text |
| `combine` | split and merge the input to cover `AddChild`'s duplicate paths |

## Replay (every compiler)

`fuzz_replay_*` runs targets over files on disk and needs no fuzzing engine. The executables build
on every supported compiler and run the corpus through `ctest`:

```sh
ctest --test-dir build -R FuzzReplay
build/fuzz/fuzz_replay_roundtrip some/input.fdf
```

Drop a crashing input into `corpus/` and it stays covered from then on.

## libFuzzer (Clang, non-Windows)

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFDF_BUILD_FUZZERS=ON
cmake --build build --target fuzz_roundtrip
mkdir -p /tmp/fdf-corpus
build/fuzz/fuzz_roundtrip -max_total_time=120 /tmp/fdf-corpus fuzz/corpus
```

libFuzzer writes what it discovers into its **first** corpus directory, so give it a scratch one and
pass `fuzz/corpus` after it as read-only seeds. Pointing it straight at `fuzz/corpus` fills the
checked-in tree with hash-named files.

ASan is enabled. Windows configuration fails because prebuilt `clang_rt.fuzzer` uses the static
release CRT while the MSVC-targeting driver uses the dynamic CRT. Run libFuzzer under WSL. The
replay targets work natively on Windows.
