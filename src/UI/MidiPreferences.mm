//
//  MidiPreferences.mm
//  foo_jl_midi_mac
//
//  Preferences pane for the MIDI player. Phase 2: choose the SoundFont that
//  FluidSynth renders through. Changes are written to fb2k::configStore
//  immediately and picked up by the next track that starts decoding.
//

#import "MidiPreferences.h"
#include "../fb2k_sdk.h"
#include "../Core/MidiConfig.h"
#include "../Core/MidiPreload.h"
#if FOO_TUN_MIDI_CLAP
#include "../Clap/ClapScanner.h"
#include "../Clap/ClapPresets.h"
#include <string>
#include <vector>
#endif
#import "../PreferencesCommon.h"

// Flipped view for top-to-bottom layout (uniquely named per extension to avoid
// Obj-C runtime class collisions when several components are loaded).
@interface MidiFlippedView : NSView
@end
@implementation MidiFlippedView
- (BOOL)isFlipped { return YES; }
@end

@interface MidiPreferences () {
    NSTextField *_pathField;
    NSTextField *_statusLabel;
    NSPopUpButton *_percussionPopup;
    NSPopUpButton *_sampleRatePopup;
#if FOO_TUN_MIDI_CLAP
    NSPopUpButton *_enginePopup;
    NSPopUpButton *_clapPluginPopup;
    NSPopUpButton *_clapPresetPopup;
    NSTextField *_clapStatusLabel;
    std::vector<foo_midi::ClapPreset> _clapPresets;   // maps popup index -> preset
#endif
}
@end

@implementation MidiPreferences

- (instancetype)init {
    self = [super initWithNibName:nil bundle:nil];
    return self;
}

- (NSString *)preferencesTitle {
    return @"MIDI Player";
}

- (void)loadView {
    CGFloat height = 380;
#if FOO_TUN_MIDI_CLAP
    height = 596;   // room for the Engine section (incl. preset row)
#endif
    MidiFlippedView *view = [[MidiFlippedView alloc] initWithFrame:NSMakeRect(0, 0, 480, height)];
    self.view = view;
    [self buildUI];
    [self loadSettings];
}

- (void)buildUI {
    CGFloat y = 10;
    CGFloat labelX = 20;

    NSTextField *title = JLCreatePreferencesTitle(@"MIDI Player");
    title.frame = NSMakeRect(labelX, y, 440, 20);
    [self.view addSubview:title];
    y += 30;

#if FOO_TUN_MIDI_CLAP
    // Engine picker (CLAP-enabled build only): FluidSynth vs a hosted CLAP
    // instrument. The SoundFont section below still configures FluidSynth.
    NSTextField *engHeader = JLCreateSectionHeader(@"Engine");
    engHeader.frame = NSMakeRect(labelX, y, 300, 17);
    [self.view addSubview:engHeader];
    y += 24;

    NSTextField *engLabel = JLCreateHelperText(@"Renderer:");
    engLabel.frame = NSMakeRect(labelX, y + 4, 100, 16);
    [self.view addSubview:engLabel];

    // Order matches midi_config::Engine (0 FluidSynth, 1 CLAP).
    _enginePopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(labelX + 104, y, 220, 26)];
    [_enginePopup addItemWithTitle:@"FluidSynth (SoundFont)"];
    [_enginePopup addItemWithTitle:@"CLAP plugin"];
    [_enginePopup setTarget:self];
    [_enginePopup setAction:@selector(engineChanged:)];
    [self.view addSubview:_enginePopup];
    y += 30;

    NSTextField *pluginLabel = JLCreateHelperText(@"Plugin:");
    pluginLabel.frame = NSMakeRect(labelX, y + 4, 100, 16);
    [self.view addSubview:pluginLabel];

    // Populated by an on-demand scan of the installed CLAP instruments.
    _clapPluginPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(labelX + 104, y, 236, 26)];
    [_clapPluginPopup setTarget:self];
    [_clapPluginPopup setAction:@selector(clapPluginChanged:)];
    [self.view addSubview:_clapPluginPopup];

    NSButton *rescan = [[NSButton alloc] initWithFrame:NSMakeRect(370, y - 1, 90, 26)];
    rescan.bezelStyle = NSBezelStyleRounded;
    rescan.title = @"Rescan";
    [rescan setTarget:self];
    [rescan setAction:@selector(rescanClapClicked:)];
    [self.view addSubview:rescan];
    y += 30;

    // Preset row: headless preset switching for plugins that ship a CLAP
    // preset-discovery provider. Populated on demand (the "Presets" button loads
    // the selected plugin to read its preset list).
    NSTextField *presetLabel = JLCreateHelperText(@"Preset:");
    presetLabel.frame = NSMakeRect(labelX, y + 4, 100, 16);
    [self.view addSubview:presetLabel];

    _clapPresetPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(labelX + 104, y, 236, 26)];
    [_clapPresetPopup setTarget:self];
    [_clapPresetPopup setAction:@selector(clapPresetChanged:)];
    [self.view addSubview:_clapPresetPopup];

    NSButton *loadPresets = [[NSButton alloc] initWithFrame:NSMakeRect(370, y - 1, 90, 26)];
    loadPresets.bezelStyle = NSBezelStyleRounded;
    loadPresets.title = @"Presets";
    [loadPresets setTarget:self];
    [loadPresets setAction:@selector(loadPresetsClicked:)];
    [self.view addSubview:loadPresets];
    y += 30;

    _clapStatusLabel = JLCreateHelperText(@"");
    _clapStatusLabel.frame = NSMakeRect(labelX, y, 440, 16);
    [self.view addSubview:_clapStatusLabel];
    y += 22;

    NSTextField *engHint = JLCreateHelperText(
        @"CLAP hosts a single instrument plugin to preview a pattern (no seeking; "
        @"the plugin owns its own sound). The list is cached — only Rescan/Presets "
        @"load plugins, which raises memory use until foobar2000 restarts, so use "
        @"them sparingly. Presets works only for plugins that expose a preset "
        @"provider (many use their own browser). FluidSynth uses the SoundFont "
        @"below. Takes effect on the next track you play.");
    engHint.frame = NSMakeRect(labelX, y, 450, 88);
    engHint.maximumNumberOfLines = 6;
    [engHint.cell setWraps:YES];
    [self.view addSubview:engHint];
    y += 94;

    [self populateClapPopupRescan:NO];
    [self rebuildPresetPopupDiscover:NO];
#endif

    NSTextField *header = JLCreateSectionHeader(@"SoundFont");
    header.frame = NSMakeRect(labelX, y, 300, 17);
    [self.view addSubview:header];
    y += 24;

    // Path field (editable so power users can paste; Browse fills it too).
    _pathField = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, y, 340, 24)];
    _pathField.placeholderString = @"Path to a .sf2 / .sf3 SoundFont";
    _pathField.font = [NSFont systemFontOfSize:11];
    [_pathField setTarget:self];
    [_pathField setAction:@selector(pathFieldChanged:)];
    [self.view addSubview:_pathField];

    NSButton *browse = [[NSButton alloc] initWithFrame:NSMakeRect(370, y - 1, 90, 26)];
    browse.bezelStyle = NSBezelStyleRounded;
    browse.title = @"Browse…";
    [browse setTarget:self];
    [browse setAction:@selector(browseClicked:)];
    [self.view addSubview:browse];
    y += 30;

    _statusLabel = JLCreateHelperText(@"");
    _statusLabel.frame = NSMakeRect(labelX, y, 440, 16);
    [self.view addSubview:_statusLabel];
    y += 26;

    NSTextField *hint = JLCreateHelperText(
        @"Use a full General MIDI SoundFont for correct instruments (drums render "
        @"on channel 10). Takes effect on the next track you play.");
    hint.frame = NSMakeRect(labelX, y, 450, 40);
    hint.maximumNumberOfLines = 3;
    [hint.cell setWraps:YES];
    [self.view addSubview:hint];
    y += 46;

    NSTextField *drumHeader = JLCreateSectionHeader(@"Percussion");
    drumHeader.frame = NSMakeRect(labelX, y, 300, 17);
    [self.view addSubview:drumHeader];
    y += 24;

    NSTextField *drumLabel = JLCreateHelperText(@"Force drum kit:");
    drumLabel.frame = NSMakeRect(labelX, y + 4, 100, 16);
    [self.view addSubview:drumLabel];

    // Order matches midi_config::PercussionMode (0 Off, 1 Auto, 2 Always).
    _percussionPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(labelX + 104, y, 220, 26)];
    [_percussionPopup addItemWithTitle:@"Off (General MIDI)"];
    [_percussionPopup addItemWithTitle:@"Auto-detect drum files"];
    [_percussionPopup addItemWithTitle:@"Always force"];
    [_percussionPopup setTarget:self];
    [_percussionPopup setAction:@selector(percussionModeChanged:)];
    [self.view addSubview:_percussionPopup];
    y += 30;

    NSTextField *drumHint = JLCreateHelperText(
        @"Many drum-pattern libraries put GM-drum notes on channel 1 with no "
        @"program change (they expect a DAW drum rack), so they'd otherwise play "
        @"as piano. Auto forces the drum kit only for files that look like that; "
        @"Always forces every file; Off honours General MIDI (drums on channel 10).");
    drumHint.frame = NSMakeRect(labelX, y, 450, 60);
    drumHint.maximumNumberOfLines = 5;
    [drumHint.cell setWraps:YES];
    [self.view addSubview:drumHint];
    y += 66;

    NSTextField *outHeader = JLCreateSectionHeader(@"Output");
    outHeader.frame = NSMakeRect(labelX, y, 300, 17);
    [self.view addSubview:outHeader];
    y += 24;

    NSTextField *srLabel = JLCreateHelperText(@"Sample rate:");
    srLabel.frame = NSMakeRect(labelX, y + 4, 100, 16);
    [self.view addSubview:srLabel];

    // Item tags carry the actual rate in Hz (see midi_config::sampleRate()).
    _sampleRatePopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(labelX + 104, y, 160, 26)];
    const int kRates[] = { 44100, 48000, 88200, 96000 };
    for (int r : kRates) {
        [_sampleRatePopup addItemWithTitle:[NSString stringWithFormat:@"%d Hz", r]];
        [[_sampleRatePopup lastItem] setTag:r];
    }
    [_sampleRatePopup setTarget:self];
    [_sampleRatePopup setAction:@selector(sampleRateChanged:)];
    [self.view addSubview:_sampleRatePopup];
    y += 30;

    NSTextField *srHint = JLCreateHelperText(
        @"Rate FluidSynth renders at. foobar2000 resamples to your output device, "
        @"so 44100 is fine; match your device rate to avoid an extra resample.");
    srHint.frame = NSMakeRect(labelX, y, 450, 40);
    srHint.maximumNumberOfLines = 3;
    [srHint.cell setWraps:YES];
    [self.view addSubview:srHint];
}

- (void)loadSettings {
    std::string path = midi_config::getConfigString(
        midi_config::kKeySoundFontPath, midi_config::kDefaultSoundFont);
    _pathField.stringValue = [NSString stringWithUTF8String:path.c_str()];
    [_percussionPopup selectItemAtIndex:midi_config::percussionMode()];
    [_sampleRatePopup selectItemWithTag:midi_config::sampleRate()];
#if FOO_TUN_MIDI_CLAP
    [_enginePopup selectItemAtIndex:midi_config::engine()];
    // The plugin popup is populated in buildUI (populateClapPopupRescan:), which
    // also selects the saved plugin and updates the status label.
#endif
    [self updateStatus];
}

- (void)updateStatus {
    NSString *path = _pathField.stringValue;
    if (path.length == 0) {
        _statusLabel.stringValue = @"No SoundFont set — using the built-in default.";
        _statusLabel.textColor = [NSColor secondaryLabelColor];
        return;
    }
    BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:path];
    if (exists) {
        _statusLabel.stringValue = @"✓ SoundFont found.";
        _statusLabel.textColor = [NSColor systemGreenColor];
    } else {
        _statusLabel.stringValue = @"⚠ File not found (is the volume mounted?).";
        _statusLabel.textColor = [NSColor systemOrangeColor];
    }
}

#pragma mark - Actions

- (void)save {
    midi_config::setConfigString(midi_config::kKeySoundFontPath,
                                 _pathField.stringValue.UTF8String);
    [self updateStatus];
    // Warm the cache for the new SoundFont so the next play is instant.
    foo_midi::preloadCurrentSoundFont();
}

- (void)pathFieldChanged:(id)sender {
    [self save];
}

- (void)percussionModeChanged:(id)sender {
    midi_config::setConfigInt(midi_config::kKeyPercussionMode,
                              (int64_t)_percussionPopup.indexOfSelectedItem);
    // Percussion mode is part of the engine key, so re-warm the cache.
    foo_midi::preloadCurrentSoundFont();
}

- (void)sampleRateChanged:(id)sender {
    midi_config::setConfigInt(midi_config::kKeySampleRate,
                              (int64_t)_sampleRatePopup.selectedItem.tag);
    // Sample rate is part of the engine key, so re-warm the cache.
    foo_midi::preloadCurrentSoundFont();
}

#if FOO_TUN_MIDI_CLAP
- (void)engineChanged:(id)sender {
    midi_config::setConfigInt(midi_config::kKeyEngine,
                              (int64_t)_enginePopup.indexOfSelectedItem);
    // The CLAP backend builds per track; only FluidSynth benefits from a warm
    // cache, and switching back to it re-uses whatever is already loaded.
}

// Rebuild the plugin dropdown from the scanned CLAP instruments. When `rescan`
// is YES the filesystem is re-walked (and the cache re-persisted); otherwise the
// cached list is used. The saved plugin is re-selected.
- (void)populateClapPopupRescan:(BOOL)rescan {
    const std::vector<foo_midi::ClapPluginEntry>& list =
        foo_midi::clapInstruments(rescan ? true : false);

    [_clapPluginPopup removeAllItems];
    if (list.empty()) {
        // addItem avoids NSPopUpButton's title de-duplication.
        NSMenuItem *none = [[NSMenuItem alloc] initWithTitle:@"No CLAP instruments found"
                                                      action:nil keyEquivalent:@""];
        [[_clapPluginPopup menu] addItem:none];
        _clapPluginPopup.enabled = NO;
        [self updateClapStatusCount:0];
        return;
    }

    _clapPluginPopup.enabled = YES;
    std::string curPath = midi_config::clapPluginPath();
    std::string curId = midi_config::clapPluginId();
    NSInteger sel = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        NSString *title = [NSString stringWithUTF8String:list[i].name.c_str()];
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
        [[_clapPluginPopup menu] addItem:item];   // preserves order + duplicates
        if (list[i].path == curPath && list[i].id == curId) sel = (NSInteger)i;
    }
    [_clapPluginPopup selectItemAtIndex:sel];
    // Persist the selection when nothing was stored yet, so the first play uses
    // the shown plugin rather than none.
    if (curPath.empty()) [self saveClapSelection:sel];
    [self updateClapStatusCount:(NSInteger)list.size()];
}

- (void)saveClapSelection:(NSInteger)idx {
    const std::vector<foo_midi::ClapPluginEntry>& list = foo_midi::clapInstruments(false);
    if (idx < 0 || (size_t)idx >= list.size()) return;
    midi_config::setConfigString(midi_config::kKeyClapPluginPath, list[idx].path.c_str());
    midi_config::setConfigString(midi_config::kKeyClapPluginId, list[idx].id.c_str());
}

- (void)clapPluginChanged:(id)sender {
    [self saveClapSelection:_clapPluginPopup.indexOfSelectedItem];
    // The saved preset belonged to the previous plugin — clear it and reset the
    // preset dropdown to the new plugin's default.
    midi_config::setConfigString(midi_config::kKeyClapPreset, "");
    midi_config::setConfigString(midi_config::kKeyClapPresetName, "");
    _clapPresets.clear();
    [self rebuildPresetPopupDiscover:NO];
}

// Rebuild the preset dropdown. When `discover` is YES the selected plugin is
// loaded (metadata only, no DSP instance) to read its preset-discovery provider;
// otherwise only the "Default" item (plus the saved preset's name, if any) is
// shown so opening Preferences stays cheap.
- (void)rebuildPresetPopupDiscover:(BOOL)discover {
    [_clapPresetPopup removeAllItems];

    // Item 0: no preset — the plugin loads its own default patch.
    NSMenuItem *def = [[NSMenuItem alloc] initWithTitle:@"Default (plugin's own patch)"
                                                 action:nil keyEquivalent:@""];
    def.representedObject = nil;
    [[_clapPresetPopup menu] addItem:def];

    midi_config::ClapPresetRef saved = midi_config::clapPreset();
    std::string savedName = midi_config::getConfigString(midi_config::kKeyClapPresetName, "");

    if (discover) {
        _clapPresets.clear();
        foo_midi::ClapPresetList pl = foo_midi::discoverClapPresets(
            midi_config::clapPluginPath(), midi_config::clapPluginId());
        _clapPresets = pl.presets;
        if (!pl.supported) {
            NSMenuItem *ns = [[NSMenuItem alloc]
                initWithTitle:@"— plugin has no host-switchable presets —"
                       action:nil keyEquivalent:@""];
            ns.enabled = NO;
            [[_clapPresetPopup menu] addItem:ns];
        } else if (_clapPresets.empty()) {
            NSMenuItem *ns = [[NSMenuItem alloc] initWithTitle:@"— no presets found —"
                                                        action:nil keyEquivalent:@""];
            ns.enabled = NO;
            [[_clapPresetPopup menu] addItem:ns];
        }
    }

    NSInteger sel = 0;
    for (size_t i = 0; i < _clapPresets.size(); ++i) {
        NSString *t = [NSString stringWithUTF8String:_clapPresets[i].name.c_str()];
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:t action:nil keyEquivalent:@""];
        item.representedObject = @((NSInteger)i);
        [[_clapPresetPopup menu] addItem:item];
        if (saved.valid && _clapPresets[i].location == saved.location &&
            _clapPresets[i].loadKey == saved.loadKey)
            sel = _clapPresetPopup.numberOfItems - 1;
    }

    // A preset is saved but we haven't discovered the list (cheap open): surface
    // its name as a placeholder so the user sees the current choice. Selecting it
    // is a no-op (representedObject @(-1) = keep).
    if (saved.valid && sel == 0 && _clapPresets.empty() && !savedName.empty()) {
        NSMenuItem *item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithUTF8String:savedName.c_str()]
                   action:nil keyEquivalent:@""];
        item.representedObject = @(-1);
        [[_clapPresetPopup menu] addItem:item];
        sel = _clapPresetPopup.numberOfItems - 1;
    }
    [_clapPresetPopup selectItemAtIndex:sel];
}

- (void)loadPresetsClicked:(id)sender {
    if (midi_config::clapPluginPath().empty()) return;
    [self rebuildPresetPopupDiscover:YES];
}

- (void)clapPresetChanged:(id)sender {
    NSMenuItem *item = _clapPresetPopup.selectedItem;
    id rep = item.representedObject;
    if (rep == nil) {   // "Default": clear the preset
        midi_config::setConfigString(midi_config::kKeyClapPreset, "");
        midi_config::setConfigString(midi_config::kKeyClapPresetName, "");
        return;
    }
    NSInteger idx = [rep integerValue];
    if (idx < 0 || idx >= (NSInteger)_clapPresets.size()) return;   // placeholder = keep
    const foo_midi::ClapPreset& p = _clapPresets[(size_t)idx];
    std::string packed = std::to_string(p.locationKind) + "\t" + p.location + "\t" + p.loadKey;
    midi_config::setConfigString(midi_config::kKeyClapPreset, packed.c_str());
    midi_config::setConfigString(midi_config::kKeyClapPresetName, p.name.c_str());
}

- (void)rescanClapClicked:(id)sender {
    [self populateClapPopupRescan:YES];
}

- (void)updateClapStatusCount:(NSInteger)n {
    if (n <= 0) {
        _clapStatusLabel.stringValue = @"No CLAP instruments found in the standard folders.";
        _clapStatusLabel.textColor = [NSColor systemOrangeColor];
    } else {
        _clapStatusLabel.stringValue = [NSString stringWithFormat:@"%ld CLAP instrument(s) found.", (long)n];
        _clapStatusLabel.textColor = [NSColor secondaryLabelColor];
    }
}
#endif

- (void)browseClicked:(id)sender {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedFileTypes = @[ @"sf2", @"sf3", @"sf4" ];
    panel.title = @"Choose a SoundFont";

    // Start in the current file's directory if it exists.
    NSString *current = _pathField.stringValue;
    if (current.length > 0) {
        NSString *dir = [current stringByDeletingLastPathComponent];
        if ([[NSFileManager defaultManager] fileExistsAtPath:dir]) {
            panel.directoryURL = [NSURL fileURLWithPath:dir];
        }
    }

    if ([panel runModal] == NSModalResponseOK) {
        _pathField.stringValue = panel.URL.path;
        [self save];
    }
}

@end

// Preferences page registration.
namespace {
    static const GUID guid_midi_preferences_page = {
        0x5c8d2a91, 0x74e3, 0x4f16,
        { 0xb2, 0x0a, 0x3e, 0x6f, 0x91, 0x4c, 0x7d, 0x83 }
    };

    class midi_preferences_page : public preferences_page {
    public:
        service_ptr instantiate() override {
            return fb2k::wrapNSObject([[MidiPreferences alloc] init]);
        }
        const char* get_name() override { return "MIDI Player"; }
        GUID get_guid() override { return guid_midi_preferences_page; }
        GUID get_parent_guid() override { return preferences_page::guid_input; }
    };

    FB2K_SERVICE_FACTORY(midi_preferences_page);
}
