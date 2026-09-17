# VST3 instruments and effects

For a short installation guide, see [VST3 setup](../../doc/VST3_SETUP.md).

LMMS on Linux builds `Vst3Instrument`, `Vst3Effect`, and the shared VST3 host
by default. Instruments appear in the instrument browser; effects appear in
**Add effect** under VST3. The **VST3 (load file)** instrument also accepts a
plugin selected manually.

## Finding plugins

The host recursively searches:

* `~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3`;
* LMMS's configured VST and LADSPA directories;
* additional directories in the colon-separated `VST3_PATH` variable.

Select the `.so` inside a native `.vst3/Contents/<architecture>-linux/`
bundle in the file picker. Bare shared objects can also be loaded manually.
Only the running host's architecture is loaded. Symlinked plugin directories
are supported. Restart LMMS after adding plugins to refresh the browser.

Factories are inspected by the separate `Vst3Scanner` executable, installed
beside the plugin libraries. Failed or crashing scans are skipped, with a
30-second timeout per module. Processing of native plugins runs inside LMMS;
scan isolation does not isolate crashes during playback. Plugin instances
and factories are released normally, but module code remains mapped until
process exit so asynchronous toolkit cleanup cannot execute unloaded code.

## Windows plugins on Linux

Install Wine and [yabridge](https://github.com/robbert-vdh/yabridge), including
`yabridgectl`. Register the directories containing your Windows plugins:

```sh
yabridgectl add "$HOME/.wine/drive_c/Program Files/Common Files/VST3"
# Also register any other directories containing Windows VST3 plugins.
yabridgectl sync
```

For the upstream archive installation, `yabridgectl` is usually at
`~/.local/share/yabridge/yabridgectl`.

Yabridge creates native bundles in `~/.vst3/yabridge`, which LMMS discovers
automatically. Selecting an original Windows `.vst3` file or bundle also
works once it has been bridged: LMMS matches the bridge's link to the original
binary. Projects keep both the class UID and the native/bridged module path,
so plugins with the same UID in different directories remain distinguishable.

Windows support depends on a working Wine/yabridge installation. In the local
validation, 64-bit Krush, Scream, and TDR Nova worked with Wine 11.13 and
yabridge 5.1.1. The 32-bit TDR Nova bridge could not start with this Wine
installation and was safely skipped; use its tested 64-bit version here.

## Host features

* Stereo 32-bit float processing, mono conversion, and silent auxiliary inputs.
* MIDI notes, CC, channel pressure and pitch bend through `IMidiMapping`.
* Automatable normalized parameters and searchable generic controls.
* Separate component/controller connections and binary state persistence.
* Native editor embedding on X11/XWayland, including resize and event-loop
  integration. On Wayland desktops with XWayland available, LMMS selects
  Qt's `xcb` backend by default so editors work without launch overrides.
  An explicit `QT_QPA_PLATFORM` setting is respected; if native Wayland is
  forced, the editor panel explains how to restart with `xcb`.

Editors normally choose their own display scale. `LMMS_VST3_SCALE=1.5`
forces an explicit factor; `LMMS_VST3_SCALE=auto` uses Qt's device pixel ratio.
Editor rendering and mouse behavior can also depend on the plugin toolkit
and the installed Wine/yabridge versions.

Not implemented: `.vstpreset` files, program/unit lists, latency compensation,
64-bit float processing, sidechain routing, and native Windows/macOS LMMS
hosting. Windows plugins are supported in the Linux build through yabridge.

## Build and test

```sh
cmake -S . -B build
cmake --build build --target Vst3HostTest -j 6
ctest --test-dir build/tests -R Vst3HostTest --output-on-failure
```

`Vst3HostTest` builds a deterministic test plugin and verifies audio gain,
MIDI sample offsets and note-off, silence flags, parameter persistence,
factory crash isolation, and LMMS instrument/effect loading and teardown.
Installed-plugin checks are opt-in:

```sh
QT_QPA_PLATFORM=offscreen \
LMMS_DATA_DIR="$PWD/data" LMMS_PLUGIN_DIR="$PWD/build/plugins" \
LMMS_TEST_VST3="/path/to/Plugin.vst3" \
build/tests/Vst3HostTest installedPlugin

# Add LMMS_TEST_VST3_GUI=1 and omit QT_QPA_PLATFORM to test the Show GUI
# button using the same platform selection as a normal LMMS launch.
# Set LMMS_TEST_VST3_SCAN=1 and run installedDiscovery to test discovery.
```

The installed-plugin test checks nonzero, finite audio, parameter restoration
with and without state chunks, instance recreation, and processing restart.
Local validation covered Vital, all four Dragonfly reverbs, TAL Reverb 2/4,
Graillon 3, and Windows Krush, Scream and 64-bit TDR Nova. Vital and Krush also
passed editor attachment/teardown. Complete LMMS projects rendered Vital
alone, with Dragonfly Plate Reverb, and with Windows Scream to distinct,
non-silent WAV files.

## Source layout

`Vst3Module` loads modules; `Vst3Manager` discovers classes via `Vst3Scanner`;
`Vst3Plugin` handles instances, buses, events and state; `Vst3HostApp` supplies
host interfaces; `Vst3ViewBase` supplies editors and generic controls.
`vst3sdk/pluginterfaces` contains the vendored Steinberg VST3 3.8.0 interface
headers, MIT licensed (see `vst3sdk/pluginterfaces/LICENSE.txt`).
