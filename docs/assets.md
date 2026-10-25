# Assets, metadata and reimport

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Stable asset IDs allow content to be renamed without breaking stored references. Metadata,
dependency tracking and validation help the editor and packaged runtime agree on the same assets and
import settings.

## Usage and behavior

Every file under a project's `Content` has a versioned `.fpmeta` file beside it. The editor creates
missing metadata automatically. Commit these files with the assets. They contain the asset's stable
ID and import settings. Moving an asset outside the editor requires moving its metadata too.
Deleting metadata and importing again gives the asset a new ID.

Scene and material asset fields save references as `asset://<guid>`. Inspectors show the resolved
path, and loaders resolve the ID to the current file. Existing path references still load and
convert to IDs when saved. Renaming in the Content Browser keeps the ID, updates legacy references
on disk, and keeps unsaved scene and material edits usable. Duplicates get new IDs and retain their
import settings. The project start scene is retained by ID when assets are renamed.

Select a texture to edit Generate mipmaps and Color space, then choose Apply and Reimport. Auto uses
sRGB for color maps and linear for data maps. sRGB and Linear override that choice for every use of
the texture. The same settings are used in the editor and packaged games. Original files remain the
import sources, so there are no machine-specific source paths to repair. Game code can call
`Paths::AssetReference(path)` when a stored reference must survive a rename.

The registry tracks scene and material asset fields, glTF and GLB external buffers and images, OBJ
material libraries, MTL texture maps, exposed script asset fields, and static HTML, CSS and
JavaScript resource links. Content changes refresh the registry and invalidate affected texture,
mesh, skeletal model and material caches on their next use. UI pages reload when their tracked
resources change. Material reimport preserves unsaved edits. Audio changes apply to subsequent loads
and playback while existing voices retain their current audio snapshot.

Use Validate in the Content Browser to report missing dependencies, duplicate IDs, invalid metadata,
unsafe paths and names that differ only by case. The Output Log includes each issue's asset path.
Packaging runs the same validation before staging a game. Cooked content includes the metadata, and
the runtime reads it without writing into Content.

Editor refreshes reuse source hashes when file size and modification time are unchanged. File-change
notifications invalidate the affected source, including edits that preserve timestamps. Metadata and
dependencies are still checked on every refresh. Validate, CLI validation and packaging reread all
source bytes. Code can choose `AssetRefreshMode::ChangedFiles` for incremental scans, call
`Invalidate(path)` for known changes, or use the default `Refresh()` for full verification.
`GetLastScanStats()` reports how many files and source bytes were hashed.

After building, the same pipeline is available without opening the editor:

```bash
Projects/Sample/Binaries/Debug/AssetTool --import
Projects/Sample/Binaries/Debug/AssetTool --validate
Projects/Sample/Binaries/Debug/AssetTool --dependencies Scenes/Showcase3D.scene
Projects/Sample/Binaries/Debug/AssetTool --project Projects/MyGame --import
Projects/Sample/Binaries/Debug/Tests Assets
python3 Scripts/TestAssetPipeline.py
```

Use `AssetTool.exe` on Windows. `--import` creates missing metadata and preserves existing IDs and
settings. Validation returns a nonzero exit code when issues are found. A read-only scan accepts
legacy projects without metadata, but ID references require their matching metadata files.

Dependency discovery covers static references. Script path discovery needs the corresponding built
script types and fields marked with `Asset`. Explicit ID references are tracked even for unknown
scripts. Paths constructed in game code or JavaScript need to be managed by the game. glTF, OBJ and
web resources retain their format's relative references, so moves that would break those links are
rejected. Move a self-contained resource folder together, or update its source references first.
Imports and refreshes are synchronous, and the cooker still preserves source formats without
compression or unused-asset removal.

## How it works

AssetDatabase scans sources and .fpmeta records, resolves IDs to paths, tracks dependencies and
provides fingerprints to content caches. Incremental refreshes reuse source hashes when possible,
while explicit validation performs full verification. The editor and command-line asset tool share
this registry.

Source: [AssetDatabase.h](../src/Engine/Assets/AssetDatabase.h), [Paths.h](../src/Engine/Paths.h).

## Related guides

[Content layout](projects.md), [Content browser](editor.md), [Packaging](packaging.md).
