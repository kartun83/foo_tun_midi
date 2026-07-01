#!/bin/bash
#
# install.sh - Install foo_tun_midi to foobar2000
#
# Usage:
#   ./Scripts/install.sh [OPTIONS]
#
# Options:
#   --config CONFIG  Build configuration (Debug/Release, default: Release)
#   --help           Show this help message

set -e

PROJECT_NAME="foo_tun_midi"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib.sh"

show_help() {
    head -12 "$0" | tail -8
    exit 0
}

if ! parse_install_args "$@"; then
    show_help
fi

do_install
