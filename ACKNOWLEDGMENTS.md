# Acknowledgments

`foo_tun_midi` builds on the work of others.

## foobar2000 SDK

© Peter Pawlowski / foobar2000.org. Downloaded and built by
`Scripts/bootstrap_sdk.sh`; not redistributed in this repository. Use is subject
to the SDK's own license terms — see the license inside the SDK archive from
<https://www.foobar2000.org/SDK>.

## FluidSynth

The audio is rendered by [FluidSynth](https://www.fluidsynth.org/), licensed
under the **GNU LGPL v2.1+**. It is linked at build time from the Homebrew keg
(`brew install fluid-synth`) and is not bundled or modified here.

## JendaT/fb2k-components-mac-suite

The project structure, the programmatic Xcode-project generator, the build
helper library (`Scripts/lib.sh`), and the preferences-pane UI helpers
(`src/PreferencesCommon.h`) are adapted from
[JendaT/fb2k-components-mac-suite](https://github.com/JendaT/fb2k-components-mac-suite)
by Jenda Legenda, used under the **MIT License**. The SMF parsing and FluidSynth
rendering in this component are original.

## SoundFonts

`foo_tun_midi` ships no SoundFont. A good general-purpose option is
[MuseScore_General](https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/)
(MIT). You supply your own.
