# Profiling and benchmarks

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Performance measurements distinguish improvements in a specific operation from changes to
whole-frame cost. Repeatable CPU benchmarks complement the editor’s system profiler and targeted GPU
checks.

## Usage and behavior

Build and run the standalone CPU benchmarks in Release:

```bash
premake5 gmake
make -C Projects/Sample/Build config=release -j8 Benchmarks
Projects/Sample/Binaries/Release/Benchmarks
```

On Windows, build the `Benchmarks` target in Release and run `Benchmarks.exe`. The tool prints CSV,
uses temporary assets, and reports median time per operation across seven batches. Build the same
configuration and use the same hardware when comparing revisions.

An Apple M2 run measured these changes from the engine before this optimization pass:

| Operation | Before | After | Speedup |
|---|---:|---:|---:|
| World matrix through 128 ancestors | 0.0570 ms | 0.00600 ms | 9.5x |
| World rotation through 128 ancestors | 0.0512 ms | 0.00258 ms | 19.9x |
| Repeated creation-order query over 10,000 entities | 0.0750 ms | 0.00107 ms | 70.2x |
| Query 16 children among 10,000 entities | 0.1768 ms | 0.00056 ms | 313.5x |
| Refresh 129 unchanged assets including a 64 MiB source | 105.107 ms | 5.319 ms | 19.8x |
| Lookup an asset fingerprint by ID | 0.661 microseconds | 0.117 microseconds | 5.6x |

Hierarchy traversal is iterative with cycle detection. Creation order is sorted after structural
changes and reused between queries. Child queries inspect parent components and sort only their
matches. Asset ID lookups use parsed IDs and fingerprints stored directly in asset records. These
are CPU microbenchmarks with warm filesystem caches. Frame time also depends on rendering, physics,
audio, scene content and editor presentation.

## How it works

The Benchmarks executable batches operations and reports their timing in CSV. CPU microbenchmarks
focus on world queries and asset work, while the editor profiler times system updates. The optional
black hole benchmark waits for GPU completion and measures a complete renderer submission.

Source: [Main.cpp](../src/Benchmarks/Main.cpp),
[BlackHoleRenderTests.cpp](../src/Tests/BlackHoleRenderTests.cpp).

## Related guides

[Tests](testing.md), [Black hole render measurements](black-holes.md), [System profiler](editor.md).
