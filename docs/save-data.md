# Saving progress and settings

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

SaveData persists a game’s selected progress and preferences outside the installed content
directory. Typed values and fallback getters allow older saves to remain usable when a game adds new
settings.

## Usage and behavior

`SaveData` saves progress and settings as named values. It is available from `Script.h`, or include
`Engine/SaveData.h` in other game code. Use a separate slot for each profile or purpose:

```cpp
SaveData progress;
progress.SetInt("score", Score);
progress.SetVec3("position", GetWorldPosition());
if (!progress.Save("player-1"))
    FP_LOG_WARN("Could not save progress");

SaveData loaded;
if (loaded.Load("player-1"))
{
    Score = loaded.GetInt("score", 0);
    SetWorldPosition(loaded.GetVec3("position", GetWorldPosition()));
}

SaveData settings;
settings.Load("settings");
float volume = settings.GetFloat("volume", 1.0f);
settings.SetFloat("volume", volume);
if (!settings.Save("settings"))
    FP_LOG_WARN("Could not save settings");
```

Use `SetBool`, `SetInt`, `SetFloat`, `SetVec2`, `SetVec3`, `SetColor` and `SetString`, with matching
`Get` functions. Getters return the fallback when a key is missing or has another type. This lets
new settings have defaults when loading older saves. `Has(key)`, `Remove(key)`, `Clear()` and
`Num()` inspect and edit the values. `SaveData::Exists(slot)` checks for a file and
`SaveData::Delete(slot)` removes one. An existing file can still fail to load if it is damaged.

Slot names use 1 to 64 lowercase letters, digits, underscores or hyphens. Files are readable text
with a format version and keep their value types. Each project name gets its own folder under the
current user's application data, so saves work when the game is installed in a read only folder.
`Paths::GetSaveDir()` gives the folder. On macOS it is `~/Library/Application
Support/FirstPrinciples/<ProjectName>/Saves`. On Windows it is under
`%APPDATA%/FirstPrinciples/<ProjectName>/Saves`. Editor play and the runtime share these saves,
including across script reloads. Projects with the same name share saves.

Save and load calls are synchronous and return `false` on failure. A failed load leaves the current
values alone. Saving writes and flushes a temporary file before replacing the previous save. Invalid
slot names, nonfinite numbers, unsupported format versions and files over 16 MiB are rejected.
`SaveToString()` and `LoadFromString(text)` use the same format in memory, and `SaveToFile(path)`
and `LoadFromFile(path)` accept explicit paths for tools. Save at checkpoints or when settings
change, since disk access can stall a frame. SaveData does not capture the world or script fields
automatically.

The Sample project's `Checkpoint` script saves a player's position and score. Attach it to an entity
and use Save Progress and Load Progress in Details while playing. It loads its slot at play start.
Run `Projects/Sample/Binaries/Debug/Tests Save_` for the save tests.

## How it works

SaveData serializes named typed values into a versioned text format. It validates names, values and
size limits before replacing a save through a temporary file. Loading parses into temporary state so
an unsuccessful load leaves the current values intact.

Source: [SaveData.h](../src/Engine/SaveData.h), [SaveData.cpp](../src/Engine/SaveData.cpp),
[Paths.h](../src/Engine/Paths.h).

## Related guides

[Binding overrides](input.md), [Project layout](projects.md), [Installed games](packaging.md).
