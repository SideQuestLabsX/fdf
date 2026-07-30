# Tests

`cases/` holds stress and edge-case `.fdf` inputs. Every `.fdf` here, plus the reference
files in `examples/`, gets parsed and round-tripped by the suite. Generated dumps are
written to the build tree under `tests/output`.

`Test --case DurationTest` runs one named case. Add `--stress` to include stress-only checks.
