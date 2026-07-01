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
    MidiFlippedView *view = [[MidiFlippedView alloc] initWithFrame:NSMakeRect(0, 0, 480, 380)];
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
