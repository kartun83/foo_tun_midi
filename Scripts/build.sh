#!/bin/bash
#
# build.sh - Build the foo_tun_midi component(s)
#
# Two variants are produced from one source tree:
#   lite -> foo_tun_midi        (FluidSynth only)
#   full -> foo_tun_midi_clap   (FluidSynth + hosted CLAP instrument)
#
# Usage:
#   ./Scripts/build.sh [OPTIONS]
#
# Options:
#   --variant V   Build one variant: lite | full | both (default: both)
#   --debug       Build Debug configuration (default: Release)
#   --release     Build Release configuration
#   --clean       Clean before building
#   --regenerate  Regenerate Xcode project before building (implied per variant)
#   --install     Install to foobar2000 after building (single variant only)
#   --package     Zip each built .component into a .fb2k-component
#   --help        Show this help message

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# lib.sh requires PROJECT_NAME set before sourcing; it's re-pointed per variant.
PROJECT_NAME="foo_tun_midi"
source "$SCRIPT_DIR/lib.sh"

VARIANT_SEL="both"
DO_INSTALL=false
DO_PACKAGE=false
DO_CLEAN=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --variant)   VARIANT_SEL="$2"; shift 2 ;;
        --debug)     BUILD_CONFIG="Debug"; shift ;;
        --release)   BUILD_CONFIG="Release"; shift ;;
        --clean)     DO_CLEAN=true; shift ;;
        --regenerate) shift ;;   # always regenerated per variant; accepted for compat
        --install)   DO_INSTALL=true; shift ;;
        --package)   DO_PACKAGE=true; shift ;;
        --help|-h)   head -21 "$0" | tail -18; exit 0 ;;
        *)           print_error "Unknown option: $1"; exit 1 ;;
    esac
done

case "$VARIANT_SEL" in
    lite) VARIANTS=("lite") ;;
    full) VARIANTS=("full") ;;
    both) VARIANTS=("lite" "full") ;;
    *)    print_error "--variant must be lite, full, or both"; exit 1 ;;
esac

if [ "$DO_INSTALL" = true ] && [ "${#VARIANTS[@]}" -gt 1 ]; then
    print_error "--install requires a single --variant (lite or full): the two"
    print_error "components register the same input, so install only one."
    exit 1
fi

# Point all lib.sh path globals at one variant's product name.
set_variant() {
    case "$1" in
        lite) export VARIANT=lite; PROJECT_NAME="foo_tun_midi" ;;
        full) export VARIANT=full; PROJECT_NAME="foo_tun_midi_clap" ;;
    esac
    COMPONENT_FOLDER="$FOOBAR_COMPONENTS/$PROJECT_NAME"
    COMPONENT_PATH="$BUILD_DIR/$BUILD_CONFIG/$PROJECT_NAME.component"
    DEST_PATH="$COMPONENT_FOLDER/$PROJECT_NAME.component"
}

# Zip a built .component into a distributable .fb2k-component (a plain zip).
do_package() {
    local src="$COMPONENT_PATH"
    local out="$BUILD_DIR/$BUILD_CONFIG/$PROJECT_NAME.fb2k-component"
    if [ ! -d "$src" ]; then
        print_error "Cannot package: $src not found"; return 1
    fi
    rm -f "$out"
    ( cd "$(dirname "$src")" && zip -r -X -q "$out" "$(basename "$src")" )
    print_success "Packaged: $out"
}

[ "$DO_CLEAN" = true ] && { print_status "Cleaning build directory..."; rm -rf "$BUILD_DIR"; }

for v in "${VARIANTS[@]}"; do
    set_variant "$v"
    print_status "===== Variant: $v  ($PROJECT_NAME) ====="
    # Force a fresh project so the correct VARIANT is baked in.
    rm -rf "$PROJECT_DIR/$PROJECT_NAME.xcodeproj"
    if ! do_build --regenerate; then
        exit 1
    fi
    [ "$DO_PACKAGE" = true ] && do_package
    [ "$DO_INSTALL" = true ] && do_install
done

print_success "All requested variants built."
