gippity also wrote this. Tested on fedora
# VST3 setup

LMMS's VST3 host currently runs on Linux. It supports native Linux VST3
plugins directly and Windows VST3 plugins through Wine and yabridge.

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

1. Restart LMMS; there is no live VST3 rescan yet.
2. Run `yabridgectl status` and fix any reported errors.
3. Make sure the yabridge library and host versions match, then run
   `yabridgectl sync` again.
4. Confirm that the plugin or wrapper is under one of the directories listed
   above.

For a native plugin, you can also add **VST3 (load file)** as an instrument
and select its `.vst3` bundle manually.
