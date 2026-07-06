# VST3 hosting for LMMS

This directory contains a native VST3 host, exposed to LMMS through two
plugins:

* **Vst3Instrument** – lists all installed VST3 instruments in the
  instrument browser (next to VeSTige, LV2, etc.)
* **Vst3Effect** – lists all installed VST3 effects in the effect chain's
  "Add effect" dialog

## What is supported

* Native Linux `.vst3` bundles (and bare `.so` modules) found in
  `~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3` or the directories in
  the `VST3_PATH` environment variable
* Both single-component plugins and plugins with a separate edit
  controller (connection points, component state synchronisation)
* Audio processing (32 bit float, stereo main buses, aux buses are fed
  silence), MIDI note input, MIDI CC/pitch bend via `IMidiMapping`
* Parameters exposed as automatable LMMS models with a generic knob UI,
  plus native plugin GUI embedding via X11 (`IPlugFrame` + `IRunLoop`)
* Full state persistence (component + controller state chunks are stored
  base64 encoded in the project file, automated parameters additionally
  as LMMS models)

## Display scaling

Plugin editors detect the system scale themselves (JUCE and DPF do this,
and yabridge has its own scaling configuration), so the host does not
report a content scale factor by default - reporting one is a common
source of misplaced mouse hit testing. To force a factor for plugins
that only scale when the host asks for it, set `LMMS_VST3_SCALE=1.5`
(any factor) or `LMMS_VST3_SCALE=auto` (use the Qt device pixel ratio).

Windows VST3 plugins can be used through
[yabridge](https://github.com/robbert-vdh/yabridge), which exposes
Windows VST3 plugins as native Linux VST3 bundles that this host loads
like any other plugin.

## Layout

* `vst3sdk/pluginterfaces/` – vendored VST3 interface headers from
  [Steinberg's `vst3_pluginterfaces`](https://github.com/steinbergmedia/vst3_pluginterfaces)
  (v3.8.0, MIT licensed - see `vst3sdk/pluginterfaces/LICENSE.txt`).
  Only the ABI headers are vendored; the host implementation
  (`public.sdk`) is not needed.
* `Vst3Module` – `dlopen`s a bundle, handles `ModuleEntry`/`ModuleExit`
  and `GetPluginFactory`; modules are shared and refcounted
* `Vst3Manager` – scans the search paths and keeps a registry of all
  audio effect classes
* `Vst3Plugin` – one plugin instance: component + controller +
  processor, bus/parameter/event handling, processing, state
* `Vst3HostApp` – `IHostApplication`, `IMessage`, `IAttributeList`,
  in-memory `IBStream`
* `Vst3SubPluginFeatures` – sub plugin keys so plugins appear in the
  browser; keys store the class uid and module path
* `Vst3ViewBase` – native editor window (X11 embedding, run loop
  integration) and the generic knob-grid widget

## Not implemented yet

* Out-of-process scanning (a crashing plugin takes the scan down with it)
* `.vstpreset` loading, program lists/units
* Latency reporting (`kLatencyChanged` restarts)
* 64 bit float processing, sidechain routing to aux buses
