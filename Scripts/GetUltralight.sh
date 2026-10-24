#!/usr/bin/env bash
# Downloads the free Ultralight SDK into ThirdParty/Ultralight. Game UI is drawn with it.
# The version and checksums are pinned so every machine gets the same files.
# Needs curl and 7-Zip (brew install sevenzip). Setup.sh runs this for you.
set -e
cd "$(dirname "$0")/.."
VERSION="2.0.0-beta.2"
case "$(uname -s)-$(uname -m)" in
	Darwin-arm64) PLATFORM=macos-arm64; SHA=87c679eba7d00eacfb81549a24754c1d87868dcfa82800b8be2719a74971b91c ;;
	Linux-x86_64) PLATFORM=linux-x64; SHA=6bd198e4e2a6e56f2cee89b8cd8122309ee7f65f3464b4cbc721fe00f3047dc6 ;;
	*) echo "Ultralight $VERSION has no SDK for $(uname -s) $(uname -m)."; exit 1 ;;
esac
DEST=ThirdParty/Ultralight
if [ -f "$DEST/version.txt" ] && [ "$(cat "$DEST/version.txt")" = "$VERSION-$PLATFORM" ]; then
	echo "Ultralight $VERSION is already in $DEST"
	exit 0
fi
SEVENZIP=$(command -v 7zz || command -v 7z || true)
[ -n "$SEVENZIP" ] || { echo "Install 7-Zip first: brew install sevenzip"; exit 1; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
echo "Downloading Ultralight $VERSION for $PLATFORM..."
URL=$(curl -fsS "https://ultralig.ht/api/v1/sdk/download?platform=$PLATFORM&version=$VERSION&format=json" | sed -E 's/.*"url":"([^"]+)".*/\1/' | sed 's#\\/#/#g')
curl -fL --progress-bar -o "$WORK/sdk.7z" "$URL"
echo "$SHA  $WORK/sdk.7z" | shasum -a 256 -c - >/dev/null || { echo "The download does not match the expected checksum."; exit 1; }
"$SEVENZIP" x -y -o"$WORK/sdk" "$WORK/sdk.7z" >/dev/null
rm -rf "$DEST"
mkdir -p "$DEST"
# The archive may hold one top folder. Its contents go straight into ThirdParty/Ultralight.
ROOT="$WORK/sdk"
if [ "$(ls -A "$ROOT" | wc -l)" -eq 1 ] && [ -d "$ROOT/$(ls -A "$ROOT")" ]; then ROOT="$ROOT/$(ls -A "$ROOT")"; fi
cp -R "$ROOT"/. "$DEST"/
echo "$VERSION-$PLATFORM" > "$DEST/version.txt"
echo "Ultralight $VERSION is in $DEST"
