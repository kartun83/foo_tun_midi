//
//  branding.h
//  foo_tun_midi
//
//  About-box branding. The macro structure is adapted from
//  JendaT/fb2k-components-mac-suite (shared/common_about.h, MIT).
//

#pragma once

#define TUN_AUTHOR "Alexey Tveritinov"
#define TUN_GITHUB_URL "https://github.com/kartun83/foo_tun_midi"
#define TUN_COPYRIGHT_YEAR "2026"

//
// DECLARE_COMPONENT_VERSION with this component's branding and acknowledgments.
//
// Usage:
//   TUN_COMPONENT_ABOUT(
//       "MIDI Player",
//       MIDI_VERSION,
//       "Description...");
//
#define TUN_COMPONENT_ABOUT(name, version, description) \
    DECLARE_COMPONENT_VERSION(name, version, \
        description "\n\n" \
        "Author: " TUN_AUTHOR "\n" \
        "Source: " TUN_GITHUB_URL "\n" \
        "Copyright (c) " TUN_COPYRIGHT_YEAR " " TUN_AUTHOR "\n\n" \
        "Built with FluidSynth (LGPL). Project structure and build tooling\n" \
        "adapted from JendaT/fb2k-components-mac-suite (MIT).")
