# VST3 setup

LMMS's VST3 host runs on Linux and macOS. It supports native VST3 plugins
and, on Linux, Windows VST3 plugins through Wine and yabridge.

## Native macOS plugins

Install the macOS `.vst3` bundle in either location:

- `~/Library/Audio/Plug-Ins/VST3`
- `/Library/Audio/Plug-Ins/VST3`

Restart LMMS after installation. Instruments appear in the instrument browser
and effects under **Add effect**. You can also select a bundle with
**VST3 (load file)**. Custom search directories can be added using the
colon-separated `VST3_PATH` environment variable on either platform.

The plugin must support the architecture of the running LMMS process
(Apple Silicon or Intel). Native plugin editors use Cocoa and logical-point sizing for Retina
displays, following the [VST3 view coordinate specification](https://steinbergmedia.github.io/vst3_doc/base/classSteinberg_1_1IPlugView.html). Windows plugins and yabridge are only supported on Linux.

VST3 hosting is built automatically on macOS; no separate VST3 SDK download
is needed. Build and run the host regression tests with:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target Vst3HostTest
ctest --test-dir build/tests -R '^Vst3HostTest$' --output-on-failure
```

## Native Linux plugins

Install or copy the plugin to one of these directories:

- `~/.vst3`
- `/usr/lib/vst3`
- `/usr/local/lib/vst3`

Restart LMMS after installing a plugin. Instruments appear in the instrument
browser. Effects appear under **Add effect**.

## Windows plugins on Linux

Install Wine and
[yabridge](https://github.com/robbert-vdh/yabridge), then install the Windows
plugin into your Wine prefix. VST3 plugins are usually placed in:

```text
~/.wine/drive_c/Program Files/Common Files/VST3
```

Register that directory and create the Linux wrappers:

```sh
yabridgectl add "$HOME/.wine/drive_c/Program Files/Common Files/VST3"
yabridgectl sync
yabridgectl status
```

Restart LMMS. Yabridge places the wrappers in `~/.vst3/yabridge`, which LMMS
searches automatically.

Do not copy a Windows `.vst3` bundle directly into `/usr/lib/vst3`; Linux
cannot load it without a yabridge wrapper.

## Windows

This VST3 host is not currently built for the Windows version of LMMS. The
Wine and yabridge instructions above are only for running Windows plugins in
LMMS on Linux.

## If a plugin is missing

On macOS, LMMS checks the native executable's code signature before loading
a VST3 plugin. A damaged signature can otherwise cause macOS to terminate the
whole application when plugin code runs. If the log reports **code signature
validation failed**, reinstall the plugin using its original installer. This
check also applies to scanning, so an invalid plugin may be absent from the list.

1. Restart LMMS; there is no live VST3 rescan yet.
2. On Linux, run `yabridgectl status` and fix any reported errors.
3. For yabridge plugins, make sure the library and host versions match, then run
   `yabridgectl sync` again.
4. Confirm that the plugin or wrapper is under one of the directories listed
   above.

For a native plugin, you can also add **VST3 (load file)** as an instrument
and select its `.vst3` bundle manually.
