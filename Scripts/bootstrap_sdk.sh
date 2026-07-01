#!/bin/bash
#
# bootstrap_sdk.sh - Download and build the foobar2000 SDK this component links against.
#
# The SDK is third-party (foobar2000.org) and is NOT vendored in this repo. This
# script fetches SDK-2025-03-07, extracts it to the repo root, and builds the five
# static libraries the Xcode project links (arm64, Release).
#
# Usage:
#   ./Scripts/bootstrap_sdk.sh
#
# Override the destination with FB2K_SDK_PATH (must match what build.sh uses).

set -euo pipefail

SDK_VERSION="SDK-2025-03-07"
SDK_URL="https://www.foobar2000.org/files/${SDK_VERSION}.7z"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK_DIR="${FB2K_SDK_PATH:-$REPO_ROOT/$SDK_VERSION}"

echo "==> SDK destination: $SDK_DIR"

# --- Fetch + extract -------------------------------------------------------
if [ ! -d "$SDK_DIR/pfc" ]; then
    if ! command -v 7zz >/dev/null 2>&1 && ! command -v 7z >/dev/null 2>&1; then
        echo "ERROR: need a 7-Zip extractor. Install one:  brew install sevenzip" >&2
        exit 1
    fi
    SEVENZIP="$(command -v 7zz || command -v 7z)"

    ARCHIVE="$REPO_ROOT/${SDK_VERSION}.7z"
    if [ ! -f "$ARCHIVE" ]; then
        echo "==> Downloading $SDK_URL"
        curl -fL "$SDK_URL" -o "$ARCHIVE"
    fi

    echo "==> Extracting to $SDK_DIR"
    mkdir -p "$SDK_DIR"
    # The archive contains pfc/, foobar2000/ at its top level.
    "$SEVENZIP" x -y -o"$SDK_DIR" "$ARCHIVE" >/dev/null
else
    echo "==> SDK sources already present, skipping download."
fi

# --- Build the five static libraries --------------------------------------
# name:project-dir:project-file
PROJECTS=(
    "pfc:pfc:pfc.xcodeproj"
    "SDK:foobar2000/SDK:foobar2000_SDK.xcodeproj"
    "helpers:foobar2000/helpers:foobar2000_SDK_helpers.xcodeproj"
    "component_client:foobar2000/foobar2000_component_client:foobar2000_component_client.xcodeproj"
    "shared:foobar2000/shared:shared.xcodeproj"
)

for entry in "${PROJECTS[@]}"; do
    IFS=":" read -r label reldir projfile <<< "$entry"
    projdir="$SDK_DIR/$reldir"
    echo "==> Building $label ($projfile)"
    ( cd "$projdir" && xcodebuild \
        -project "$projfile" \
        -configuration Release \
        ARCHS=arm64 ONLY_ACTIVE_ARCH=NO \
        SYMROOT="$projdir/build" \
        build >/dev/null )
done

# --- Verify ----------------------------------------------------------------
EXPECTED=(
    "pfc/build/Release/libpfc-Mac.a"
    "foobar2000/SDK/build/Release/libfoobar2000_SDK.a"
    "foobar2000/helpers/build/Release/libfoobar2000_SDK_helpers.a"
    "foobar2000/foobar2000_component_client/build/Release/libfoobar2000_component_client.a"
    "foobar2000/shared/build/Release/libshared.a"
)
missing=0
for lib in "${EXPECTED[@]}"; do
    if [ -f "$SDK_DIR/$lib" ]; then
        echo "    ok  $lib"
    else
        echo "    MISSING  $lib" >&2
        missing=1
    fi
done

if [ "$missing" -ne 0 ]; then
    echo "ERROR: one or more SDK libraries failed to build." >&2
    exit 1
fi

echo "==> SDK ready at $SDK_DIR"
echo "    Now build the component:  ./Scripts/build.sh --install"
