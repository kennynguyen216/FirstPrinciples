# Logging and diagnostics

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Logging makes runtime behavior, import failures and build errors visible in the terminal and editor.
File logs retain diagnostic information across launches.

## Usage and behavior

Log with printf style formatting from anywhere, scripts included:

```cpp
FP_LOG("Loaded %d enemies", count);
FP_LOG_VERBOSE("Path took %.2f ms", ms);
FP_LOG_WARN("No spawn point in %s", scene);
FP_LOG_ERROR("Could not open %s", path);
```

Logging runs on [spdlog](https://github.com/gabime/spdlog), and every line goes to four places:

- The terminal, with the level in color. Warnings and errors go to stderr.
- The project's `Saved/Logs/Editor.log` or `Runtime.log`, with the date, time and thread. Each run
  starts a fresh file and keeps the last three runs as `.1.log`, `.2.log` and `.3.log`.
- The editor's Output Log, with the time.
- The Visual Studio Output window on Windows.

`Log::SetMinimumLevel` hides lines below a level, `Log::AddSink` adds another output and
`Log::Flush` writes out anything buffered. Only verbose lines are ever held back, so a crash never
loses a warning.

## How it works

The logging facade formats messages and routes them through spdlog and registered sinks. The editor
sink forwards messages to the UI thread before appending them to the Output Log. Minimum-level
filtering and flushing are shared by engine code and scripts.

Source: [Log.h](../src/Core/Log.h), [Log.cpp](../src/Core/Log.cpp),
[EditorWindow.cpp](../src/Editor/EditorWindow.cpp).

## Related guides

[Output Log and profiler](editor.md), [Testing](testing.md), [Performance
measurements](benchmarks.md).
