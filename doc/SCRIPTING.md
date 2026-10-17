# Scripting LMMS with Python

LMMS can be driven from Python, in the spirit of Blender's `bpy`: build songs, place notes,
load instruments and effects (including VST3), turn any knob, automate, mix, render, and read
back loudness statistics, all from code. It's built so that **AI agents** can make music
with LMMS, and it works just as well for people who'd rather write a melody as a list.

```python
import lmms

song = lmms.song
song.new(empty=True)
song.tempo = 124

bass = song.add_instrument_track("lb302", name="Bass")
clip = bass.add_midi_clip(start=0, length=lmms.bars(4))
clip.add_notes([("C2", i * 0.5, 0.4) for i in range(32)])
bass.add_effect("bitcrush", params={"rate": 22000})

print(song.render("~/bass.wav"))   # {'seconds': 7.9, 'peak_db': -1.2, 'rms_db': -13.4, ...}
```

Contents

1. [Ways to run scripts](#1-ways-to-run-scripts)
2. [Conventions and units](#2-conventions-and-units)
3. [Python API reference](#3-python-api-reference)
4. [Plugins and parameters](#4-plugins-and-parameters)
5. [Recipes](#5-recipes)
6. [Guide for AI agents](#6-guide-for-ai-agents)
7. [The wire protocol (non-Python clients)](#7-the-wire-protocol-non-python-clients)
8. [Configuration and troubleshooting](#8-configuration-and-troubleshooting)

Example scripts are in [`data/scripting/examples`](../data/scripting/examples). They're also in the
Script Console's **Examples** menu.

---

## 1. Ways to run scripts

All of these use the same `lmms` Python module, and scripts behave the same way in each.

### Script Console (inside LMMS)

**View → Script Console** or **Ctrl+8** (it's also in the command palette, **Ctrl+K**).

* The top pane is an editor. **Ctrl+Enter** runs the whole script, or only the selected lines if
  there's a selection. Open, save, and the bundled examples are in the toolbar.
* The prompt at the bottom runs single lines, with **↑/↓** history, like Python's interactive
  prompt. The value of a bare expression is printed.
* All code runs in **one persistent interpreter**, so variables you define stay around between
  runs. `lmms`, `song`, `mixer`, `patterns`, `note`, `chord` and `scale` are predefined.
* **Stop** kills the running code. **Reset** starts a fresh interpreter.
* **Agent link** copies a Python snippet that connects another program to this LMMS instance.
  See [section 6](#6-guide-for-ai-agents).

The console starts Python on first use. It uses `python3` from `PATH` unless configured otherwise
(see [section 8](#8-configuration-and-troubleshooting)).

### From the command line

| Command | What it does |
|---|---|
| `lmms --script song.py` | Starts the GUI, runs the script once LMMS is ready, and stays open so you can see or keep working on the result. Output goes to the terminal and the Script Console. |
| `lmms --script song.py --exit-after-script` | Same, then quits with the script's exit code. |
| `lmms script song.py [args...]` | **Headless**: no window at all. Exits with the script's exit code. Same as `lmms --headless --script song.py -- args...`. |
| `lmms --script song.py project.mmpz` | Opens `project.mmpz` first, then runs the script against it. |
| `lmms --script-server` | Starts the GUI and accepts connections from external Python processes. |
| `lmms --headless --script-server` | A windowless LMMS that external programs control. It runs until `lmms.quit()` or Ctrl+C. |
| `--script-port 5555` | Fixed port for the server (default: any free port). |

Arguments after `--` are passed to the script as `sys.argv[1:]`.

When the script server starts, LMMS prints a machine-readable line:

```
LMMS_SCRIPT_SERVER {"host":"127.0.0.1","port":42473,"token":"…","pid":1234,"version":"1.3.0-alpha"}
```

It also writes the same data to `$XDG_RUNTIME_DIR/lmms-script-server.json`, readable only by you,
and deletes the file on exit.

### From any Python process (agents, notebooks, other tools)

```python
import sys
sys.path.insert(0, "/usr/share/lmms/scripting/python")   # or <lmms source>/data/scripting/python
import lmms

lmms.launch()            # start a new headless LMMS and connect to it
# or
lmms.connect()           # attach to a running LMMS started with --script-server
```

`launch(headless=True, project=None, binary=None, port=None, verbose=False)` finds the `lmms`
executable from `$LMMS_BINARY`, the install or source layout next to the module, or `PATH`. It
stops that LMMS when Python exits.

`connect(host=None, port=None, token=None)` tries the environment of scripts started by LMMS first
(`LMMS_SCRIPT_HOST/PORT/TOKEN`), then the connection file.

The module is a single pure-Python package with no dependencies. Copy
`data/scripting/python/lmms` anywhere you like.

---

## 2. Conventions and units

| Quantity | Unit |
|---|---|
| Positions, lengths | **beats** = quarter notes, as floats. 1 beat = 48 LMMS ticks, so the finest step is 1/48 beat. A bar is `lmms.bars(1)`, which is 4 beats in 4/4 and 3 in 6/8. |
| Note positions | Relative to the start of their clip. |
| Pitch | MIDI note number (60 = C4, 69 = A4 = 440 Hz) or a name: `"C4"`, `"F#2"`, `"Bb3"`. |
| Note velocity | LMMS scale **0–200, 100 = normal** (as in the piano roll). |
| Note pan | −100 (left) … 100 (right). |
| Track volume / panning | 0–200 % / −100…100. |
| Track pitch | Cents. |
| Mixer channel volume | 0–2, linear (1 = unity). Channel 0 is the master. |
| Master volume | 0–200 %. |
| Sample clip gain | Linear (1 = unity). Fades are in beats. |
| Plugin parameters | The parameter's own range: see `params` / `print(plugin.params)`. VST3 parameters are normalized 0–1. |

Objects such as tracks, clips, plugins and parameters are handles to live LMMS objects. Reading a
property always asks LMMS for the current value, so changes made in the GUI show up. Using an object
after it was deleted raises `LmmsError: … no longer exists`.

Every failure raises `lmms.LmmsError` with a message that says what to do. For a misspelled plugin,
parameter or option, the message lists the valid ones:

```
LmmsError: no parameter "cutof". Available: vcf_cut, vcf_res, vcf_mod, vcf_dec, dist, …
```

---

## 3. Python API reference

Call `lmms.help()` for a one-screen overview, or `help(lmms.MidiClip)` for any class.

### Module level

| Function | Description |
|---|---|
| `lmms.song` | The current project: a [`Song`](#song). |
| `lmms.mixer` | The [`Mixer`](#mixer). |
| `lmms.patterns` | The pattern editor: [`Patterns`](#patterns-the-pattern--beat-editor). |
| `connect(host, port, token)` / `disconnect()` / `launch(...)` | See [section 1](#from-any-python-process-agents-notebooks-other-tools). |
| `instruments(sub_plugins=False)` / `effects(...)` / `plugins(type, sub_plugins)` | Installed plugins as dicts: `name`, `display_name`, `description`, `type` and, with `sub_plugins=True`, the LADSPA/LV2/VST3 plugins inside wrappers. |
| `note("C#4") → 61`, `note_name(61) → "C#4"` | Note-name conversion. |
| `chord(root, quality="maj", inversion=0)` | `chord("C4", "maj7") → [60, 64, 67, 71]`. Also takes symbols like `chord("Am7")` (octave 4) or `chord("F#3dim")`. Qualities: maj, min/m, dim, aug, sus2, sus4, 5, 6, m6, 7, maj7, m7, mmaj7, dim7, m7b5, 9, maj9, m9, add9, 11, 13. |
| `scale(root, mode="major", count=None, octaves=1)` | Pitches of a scale. Modes: major/ionian, minor/aeolian, harmonic_minor, melodic_minor, dorian, phrygian, lydian, mixolydian, locrian, major_pentatonic, minor_pentatonic, blues, chromatic, whole_tone. |
| `bars(n)` | Beats in `n` bars of the current time signature. |
| `analyze(path)` | Peak/RMS statistics of any audio file (see `Song.render`). |
| `paths()` / `factory_sample("drums/kick01.ogg")` | Real directories of factory/user samples, presets and projects. |
| `screenshot(path, window="main")` | PNG of a window: `main`, `song_editor`, `piano_roll`, `pattern_editor`, `automation_editor`, `mixer`, `script_console`. GUI only. |
| `show(window)` | Bring an editor window to the front. GUI only. |
| `message(text)` | Print a line in the Script Console, or on LMMS' stdout when headless. |
| `quit(code=0)` | Quit LMMS. |
| `call(method, **params)` / `batch([(method, params), ...])` / `api()` | Raw commands ([section 7](#7-the-wire-protocol-non-python-clients)). |
| `Note(pitch, start, length, velocity=100, pan=0)` | Named tuple used for notes. It also has `.name` and `.end`. |
| `LmmsError` | Raised for every error reported by LMMS. |

### Song

| Member | Description |
|---|---|
| `tempo` | BPM (read/write). |
| `time_signature` | `(numerator, denominator)` (read/write). |
| `master_volume`, `master_pitch` | Master volume in % and master pitch in semitones. |
| `beats_per_bar`, `length` (beats), `length_bars` | Read-only. |
| `position` | Play-head position in beats (read/write). |
| `playing`, `file`, `loop` | Read-only. `loop` is `{"enabled", "start", "end"}`. |
| `params` | Automatable song parameters: `tempo`, `master_volume`, `master_pitch`, `time_sig_numerator`, `time_sig_denominator`. |
| `info()` | Everything above as a dict. |
| `new(empty=False, template=None)` | New project. `empty=True` means no tracks, 140 BPM, 4/4, no template, which is what scripts usually want. Without arguments the user's default template is loaded. |
| `open(path)` / `save(path=None) → path` | Load or save `.mmp`/`.mmpz`. `save` adds `.mmpz` when there's no extension. |
| `import_midi(path)` / `export_midi(path)` | MIDI file import and export. |
| `render(path, format=None, sample_rate=44100, bit_depth=16, loop=False, between_loop_markers=False, loop_count=1, stems=False)` | Render and **wait** until done. The format comes from the extension (`wav`, `flac`, `ogg`, `mp3`) or `format`. It returns `{"path", "seconds", "sample_rate", "peak", "peak_db", "rms_db", "clipped_samples", "silent"}`, or only `path` if LMMS can't read the format back. `stems=True` renders each track into the directory `path` and returns `{"path", "files"}`. |
| `play(start=None)`, `stop()`, `pause()` | Transport. |
| `set_loop(start, end, enabled=True)` | Song-editor loop points in beats. |
| `tracks` / `track(name_or_index)` | Song-editor tracks, top to bottom. |
| `add_instrument_track(instrument="tripleoscillator", name=None, key=None, preset=None, params=None, color=None)` | `key` picks a sub-plugin (a VST3 name for `vst3instrument`, for example). `preset` loads a `.xpf` instead. `params` sets instrument knobs. |
| `add_sample_track(name)`, `add_automation_track(name)`, `add_pattern_track(name)` | Other track types. A pattern track also creates a new pattern in the pattern editor. |
| `add_track(type, name, container="song", **kw)` | The generic form. |

### Tracks

All tracks have `name`, `muted`, `solo`, `color` (`"#rrggbb"` or `None`), `index`, `container`
(`"song"` or `"pattern"`), `clips`, `params`, and the methods `delete()`, `clone()`, `move(index)`
and `add_clip(start, length, name)`.

**`InstrumentTrack`** has these as well:

| Member | Description |
|---|---|
| `volume`, `panning`, `pitch`, `mixer_channel` | Read/write. `mixer_channel` takes an index or a `MixerChannel`. |
| `instrument` | The [`Instrument`](#plugins-instrument-effect) plugin. |
| `set_instrument(plugin, key=None, params=None, keep_name=False)` | Swap the instrument. |
| `load_preset(path)` / `save_preset(path)` | `.xpf` instrument presets. |
| `add_midi_clip(start=0, length=None, name=None, notes=None)` | Without `length` the clip grows to fit its notes, in whole bars. |
| `effects` / `add_effect(plugin, key=None, params=None)` | The track's effect chain. |
| `note_on(pitch, velocity=100)`, `note_off(pitch)`, `play_note(pitch, seconds=0.5)` | Live notes (MIDI velocity 1–127), for auditioning. |
| `pattern(index)` / `set_steps(steps, pattern=0, pitch=None, velocity=100)` | For tracks **in the pattern editor**: the clip of a pattern, and step entry. |

**`SampleTrack`**: `volume`, `panning`, `mixer_channel`, `effects`, `add_effect(...)` and
`add_sample_clip(file, start=0, length=None, name=None)`.

**`AutomationTrack`**: `add_automation_clip(param, start=0, length=None, points=None, progression=None, name=None)`.

**`PatternTrack`**: `pattern_index`. `add_clip(start, length=one bar)` places the pattern in the song.

### Clips

All clips have `name`, `start`, `length`, `end`, `muted`, `color`, `track`, and the methods
`move(start)`, `copy(start, track=None)` and `delete()`.

**`MidiClip`**

| Member | Description |
|---|---|
| `notes` | List of `Note(pitch, start, length, velocity, pan)`. Assigning to it replaces all notes. |
| `add_note(pitch, start=0, length=1, velocity=100, pan=0)` | |
| `add_notes(iterable)` | Tuples `(pitch, start, length[, velocity[, pan]])`, `Note`s or dicts, all in **one round trip**. Prefer this for many notes. |
| `add_chord(pitches, start, length, velocity)` | For example `clip.add_chord(lmms.chord("Dm7"), 0, 4)`. |
| `set_notes(notes)`, `clear()` | Replace or remove all notes. |
| `remove_notes(indices or predicate)` | For example `clip.remove_notes(lambda n: n.pitch > 72)`. |
| `transpose(semitones)`, `quantize(grid=0.25)` | |
| `set_steps(steps, pitch=None, velocity=100, total=None)` | Pattern-editor clips: enable 16th-note steps such as `[0, 4, 8, 12]`. |
| `open()` | Show the clip in the piano roll (GUI only). |

**`SampleClip`**: `file`, `gain`, `reversed`, `fade_in`, `fade_out` (all read/write) and
`sample_length` (beats at the current tempo).

**`AutomationClip`**: `points` (`[(beat, value), ...]`), `set_points(points, progression=None, replace=True)`,
`add_point(beat, value)`, `progression` (`"discrete"`, `"linear"` or `"cubic"`), `targets`,
`add_target(param)` and `open()`.

**`PatternClip`**: a placement of a pattern on a `PatternTrack`.

### Plugins (`Instrument`, `Effect`)

| Member | Description |
|---|---|
| `name`, `display_name`, `key` | `name` is the plugin id (for example `"tripleoscillator"`). `key` describes the sub-plugin for LADSPA, LV2 and VST3 plugins. |
| `params` | A [`ParamSet`](#paramset-and-param). |
| `set(**values)` | Shortcut: `synth.set(vcf_cut=0.8, vcf_res=0.5)`. |
| `get_state() → xml` / `set_state(xml)` | Complete settings, the same data a project stores. This includes a VST3 plugin's own chunk. |
| `load_file(path, keep_name=True)` | A sample for AudioFileProcessor or SlicerT, a SoundFont for Sf2Player, and so on. |
| `Instrument.track` | The track the instrument belongs to. |
| `Effect.enabled`, `Effect.wet` (0–1), `Effect.move(by)`, `Effect.delete()` | |

### ParamSet and Param

`params` works like a dict of parameter values:

```python
synth = track.instrument
print(synth.params)                    # table: key, name, value, range or options
synth.params["vcf_cut"] = 0.6          # set by key...
synth.params["VCF Resonance"] = 0.8    # ...or by display name
synth.params.update(dist=0.2, slide=1)
cutoff = synth.params.param("vcf_cut") # the Param object, needed for automation
cutoff.value, cutoff.min, cutoff.max, cutoff.default, cutoff.display, cutoff.options
```

`Param.type` is `float`, `int`, `bool` or `choice`. You can set a choice parameter by index or by
option text: `params["wavetype0"] = 2` or `fx.params["mode"] = "Low-pass"`.

### Mixer

`lmms.mixer.channels`, `.master`, `.channel(i)` and `.add_channel(name)` return `MixerChannel`
objects, which have:

* `index`, `name`, `volume` (0–2), `muted`, `solo` and `color`
* `sends`: a list of `(channel_index, amount)`
* `send_to(channel, amount=1.0)`: an amount of 0 removes the send; feedback loops are refused
* `effects`, `add_effect(plugin, key=None, params=None)` and `delete()`
* `params`: volume, muted and solo as `Param`s, for automation

New channels send to the master by default.

### Patterns (the pattern / "beat" editor)

The pattern editor has its own tracks. Each one holds **one clip per pattern**. A `PatternTrack` in
the song editor owns a pattern index and plays that pattern wherever you put its clips.

```python
beat = song.add_pattern_track("Drums")                    # create this first (see below)
kick = lmms.patterns.add_instrument_track("kicker", name="Kick")
kick.set_steps([0, 4, 8, 12], pattern=beat.pattern_index)
beat.add_clip(start=0, length=lmms.bars(4))
```

`lmms.patterns` has `count`, `tracks`, `track(name)`, `add_instrument_track(instrument="kicker", name, key, preset, params)`
and `add_sample_track(name)`. As in the GUI, if a track is added to the pattern editor while no pattern
exists, LMMS creates pattern 0 together with a pattern track. Creating your pattern track first avoids
that extra track.

---

## 4. Plugins and parameters

**Finding plugins.** `lmms.instruments()` and `lmms.effects()` list the built-in plugins. Plugins
inside a wrapper (LADSPA, LV2, VST3) are listed with `sub_plugins=True`, and you select one with
`key=`:

```python
vst = song.add_instrument_track("vst3instrument", key="Vital")      # match by name
song.track("Bass").add_effect("vst3effect", key="TDR Nova")
song.track("Bass").add_effect("ladspaeffect", key="C* AmpVTS")
# exact sub-plugin: key={"name": ..., "attributes": {...}} as listed by sub_plugins=True
```

`key` matches the sub-plugin's name case-insensitively, first exactly and then as a substring.

**Parameter keys.** A parameter's key is the name LMMS stores it under in project and preset files,
for example `vcf_cut`, `vol0` or `wet`. These keys are stable across versions and languages, and they
are the attribute names you see in `.mmp` files and in `get_state()` XML. When the same name occurs
in several places, the key gets its XML parent as a prefix. For example, the attack of the volume,
cutoff and resonance envelopes on an instrument track is `elvol.att`, `elcut.att` and `elres.att`.
Parameters that are never saved, and VST3 parameters, use a lower-case slug of their display name,
such as `chorus_mix`. Display names are always accepted as well.

**Track parameters.** `track.params` includes volume (`vol`), panning (`pan`), `pitch`, `pitchrange`,
`mixch`, the envelope and LFO knobs (`elvol.*`, `elcut.*`, `elres.*`), the filter, arpeggio and chord
settings, MIDI port settings, and more. Print it to see them all.

**Effect parameters.** Every effect has `on` (enabled), `wet` (wet/dry, −1…1) and `autoquit`
(decay) in addition to its own knobs.

**Automation.** Any `Param` can be automated:

```python
cut = bass.instrument.params.param("vcf_cut")
auto = song.add_automation_track("Cutoff")
auto.add_automation_clip(cut, start=0, length=16, points=[(0, 0.2), (16, 1.2)], progression="linear")
```

`song.params.param("tempo")` automates the tempo, and a mixer channel's `params.param("volume")`
automates a fader.

---

## 5. Recipes

**A 4-bar loop and a stats check**

```python
song.new(empty=True)
lead = song.add_instrument_track("tripleoscillator", name="Lead")
clip = lead.add_midi_clip(0, lmms.bars(4))
pent = lmms.scale("A3", "minor_pentatonic", count=8)
clip.add_notes((pent[i % 8], i * 0.5, 0.45, 90) for i in range(32))
stats = song.render("/tmp/loop.wav")
assert not stats["silent"] and stats["clipped_samples"] == 0
```

**Chord progression with voice leading by inversion**

```python
for bar, (sym, inv) in enumerate([("Cmaj7", 0), ("Am7", 1), ("Dm7", 0), ("G7", 2)]):
    clip.add_chord(lmms.chord(sym, inversion=inv), start=lmms.bars(bar), length=lmms.bars(1), velocity=70)
```

**Drums from factory samples**

```python
snare = lmms.patterns.add_instrument_track("audiofileprocessor", name="Snare")
snare.instrument.load_file(lmms.factory_sample("drums/snare01.ogg"))
snare.set_steps([4, 12], pattern=beat.pattern_index)
```

**An audio file on the timeline**

```python
vox = song.add_sample_track("Vox")
clip = vox.add_sample_clip("~/vocals.wav", start=lmms.bars(8))
clip.gain, clip.fade_in = 0.8, 0.5
```

**Bus routing**

```python
drums_bus = lmms.mixer.add_channel("Drums")
for t in lmms.patterns.tracks:
    t.mixer_channel = drums_bus
drums_bus.add_effect("compressor")
reverb = lmms.mixer.add_channel("Reverb")
reverb.add_effect("reverbsc")
drums_bus.send_to(reverb, 0.3)
```

**Copy and save a sound**

```python
xml = song.track("Lead").instrument.get_state()
other = song.add_instrument_track("tripleoscillator", name="Lead 2")
other.instrument.set_state(xml)
song.track("Lead").save_preset("~/lead.xpf")
```

**Arrange by copying clips**

```python
verse = song.track("Bass").clips[0]
for bar in (8, 16, 24):
    verse.copy(start=lmms.bars(bar))
```

**Audition while the GUI is open**

```python
song.track("Lead").play_note("C5", seconds=0.3)
song.play(start=0); import time; time.sleep(4); song.stop()
```

---

## 6. Guide for AI agents

The scripting interface is designed so an agent can compose without hearing or seeing anything.

1. **Start LMMS once and keep it running.** Use `lmms.launch()` (headless) from a Python tool, or
   `lmms --headless --script-server` plus `lmms.connect()`. If the user already has LMMS open, the
   Script Console's **Agent link** button gives the connection code for that instance, or the user
   can start LMMS with `--script-server`. Calls are cheap (~0.1 ms locally), and `add_notes` /
   `lmms.batch` send many items in one round trip.
2. **Discover before you compose.** `lmms.instruments()`, `lmms.effects()` and `print(x.params)` show
   what's available and how each knob is named. Error messages list valid names, so a wrong guess
   is cheap.
3. **Start from a clean slate** with `song.new(empty=True)`. That's deterministic and doesn't load the
   user's default template, which might contain plugins.
4. **Think in beats and bars.** Build with `lmms.bars()`, `lmms.chord()` and `lmms.scale()`.
5. **Check the result numerically.** `song.render(path)` returns `peak_db`, `rms_db`,
   `clipped_samples` and `silent`. Typical targets are a peak below −1 dBFS and an RMS around −14 to −10
   dBFS for a full mix. Render stems (`stems=True`) and `lmms.analyze()` each file to balance
   individual tracks.
6. **Look at it** when a GUI is available: `clip.open()` and `lmms.screenshot(path, "piano_roll")`
   produce images you can inspect.
7. **Save** with `song.save(path)`. The user can open the `.mmpz` in LMMS and keep working.

A complete agent session:

```python
import lmms
lmms.launch()                                   # headless LMMS, stopped when Python exits
song = lmms.song
song.new(empty=True); song.tempo = 90
keys = song.add_instrument_track("organic", name="Keys")
clip = keys.add_midi_clip(0, lmms.bars(8))
for bar, sym in enumerate(["Dm9", "G7", "Cmaj7", "Am7"] * 2):
    clip.add_chord(lmms.chord(sym), start=lmms.bars(bar), length=lmms.bars(1), velocity=65)
keys.add_effect("reverbsc")
print(song.render("/tmp/keys.wav"))             # inspect peak/RMS, adjust, render again
song.save("/tmp/keys.mmpz")
```

---

## 7. The wire protocol (non-Python clients)

The script server speaks **newline-delimited JSON over TCP on 127.0.0.1**, so any language can use
it.

1. Connect to the port from the `LMMS_SCRIPT_SERVER` line or the connection file.
2. Send `{"token": "<token>"}`. The reply is `{"ok": true, "version": "…"}`. A wrong token closes the
   connection.
3. Send requests of the form `{"id": 1, "method": "track.add_clip", "params": {…}}`. Each gets
   `{"id": 1, "result": …}` or `{"id": 1, "error": {"message": "…"}}`. Requests on a connection are
   answered in order. `song.render` replies when rendering is done.

Objects are referred to by numeric `id`s from earlier results. All units are the ones from
[section 2](#2-conventions-and-units). `api.describe` returns every method with a one-line
description, and `api.batch` runs `{"calls": [{"method", "params"}, ...]}` in one round trip.

```sh
# quick test with netcat (token and port from the connection file)
printf '{"token":"%s"}\n{"id":1,"method":"song.info"}\n' "$TOKEN" | nc -q1 127.0.0.1 "$PORT"
```

| Method | Description |
|---|---|
| `api.batch` | Run several calls at once: calls [{method, params}] → [{result} or {error}] |
| `api.describe` | All methods with a one-line description |
| `api.ping` | Returns "pong" |
| `app.analyze` | Peak/RMS statistics of an audio file: path |
| `app.message` | Show a message in the Script Console / on stdout: text |
| `app.paths` | Real directories of factory samples/presets/projects and the user's LMMS folders |
| `app.quit` | Quit LMMS: code (optional) |
| `clip.add_notes` | Add notes to a MIDI clip: id, notes [{pitch, start, length, velocity (0-200), pan (-100..100)}] or [[pitch, start, length, velocity?, pan?], ...]; pitch may be a name like "C4" |
| `clip.automation` | Points of an automation clip: {progression, points: [[beat, value], ...], targets} |
| `clip.clear_notes` | Remove all notes from a MIDI clip |
| `clip.copy` | Copy a clip to another start position (beats), optionally onto another track of the same type: id, start, track? |
| `clip.delete` | Remove a clip |
| `clip.info` | Describe a clip |
| `clip.notes` | Notes of a MIDI clip: [{pitch, start, length, velocity, pan}] (start relative to the clip) |
| `clip.open` | Open a clip in its editor (piano roll / automation editor); GUI only |
| `clip.remove_notes` | Remove notes by index (as returned by clip.notes): id, indices |
| `clip.set` | Set name, start, length, muted, color; sample clips: gain, reversed, fade_in, fade_out, file |
| `clip.set_automation` | Edit an automation clip: points [[beat, value], ...] (replace=true clears first), progression (discrete/linear/cubic), tension, target (param id to add) |
| `clip.set_notes` | Replace all notes of a MIDI clip (same format as clip.add_notes) |
| `clip.set_steps` | Pattern-editor clips: on (list of 16th-step indices), pitch?, velocity?, steps (total step count)? |
| `effect.delete` | Remove an effect from its chain |
| `effect.move` | Move an effect up (by=-1) or down (by=+1) in its chain |
| `effect.set` | Set enabled, wet (0-1) and/or params {key: value} of an effect |
| `gui.screenshot` | Save a PNG of a window: path, window (main, song_editor, piano_roll, pattern_editor, automation_editor, mixer, script_console) |
| `gui.show` | Show a window: song_editor, piano_roll, pattern_editor, automation_editor, mixer, script_console |
| `mixer.add_channel` | Add a mixer channel (name optional); returns it |
| `mixer.add_effect` | Append an effect to a mixer channel: index, plugin, key, params |
| `mixer.channel` | Describe one mixer channel: index |
| `mixer.channels` | List mixer channels (0 = master) with volume, sends and effects |
| `mixer.delete_channel` | Delete a mixer channel (tracks routed to it go to master) |
| `mixer.send` | Route channel `from` to channel `to` with amount (0-1); amount 0 removes the send |
| `mixer.set` | Set name, volume (0-2, 1 = unity), muted, solo, color of a mixer channel |
| `param.find` | Find a parameter of a track or plugin (owner = its id) by key or display name: owner, name |
| `param.get` | Describe a parameter by handle |
| `param.set` | Set a parameter by handle (number, bool, or option text for choice parameters): id, value |
| `patterns.info` | Number of patterns in the pattern editor and the current one |
| `plugin.get_state` | The plugin's full settings as XML (the same data a project stores) |
| `plugin.info` | Describe an instrument or effect |
| `plugin.load_file` | Load a file into a plugin: id, path, keep_name (default true) |
| `plugin.params` | List parameters of an instrument or effect (key, name, value, range, options) |
| `plugin.set_params` | Set parameters of an instrument or effect: id, values {key or display name: value} |
| `plugin.set_state` | Restore settings from XML produced by plugin.get_state: id, xml |
| `plugins.list` | Installed plugins: type (instrument/effect), sub_plugins=true also lists LADSPA/LV2/VST3 sub-plugins |
| `song.add_track` | Add a track: type (instrument/sample/automation/pattern), name, instrument (plugin name), key (sub-plugin), preset (.xpf), container (song/pattern), color, params {…} for the track, instrument_params {…} for the instrument |
| `song.export_midi` | Export the song as a MIDI file: path |
| `song.import` | Import a MIDI (or Hydrogen) file into the song: path |
| `song.info` | Tempo, time signature, length, playback state and file of the current project |
| `song.new` | New project: from the default template, from `template` (a file), or completely empty with empty=true |
| `song.open` | Open a project file (.mmp/.mmpz): path |
| `song.params` | Automatable song parameters (tempo, master volume/pitch, time signature) |
| `song.pause` | Pause/resume playback |
| `song.play` | Start playback (optionally `from` a beat position) |
| `song.render` | Render to an audio file (wav/flac/ogg/mp3) and return loudness statistics: path, format?, sample_rate, bit_depth (16/24/32), loop, between_loop_markers, loop_count, stems, analyze |
| `song.save` | Save the project; path is optional when the project already has a file name |
| `song.set` | Set tempo, time_signature [num, den], master_volume (0-200), master_pitch (semitones) |
| `song.set_loop` | Set the song loop: enabled, start, end (beats) |
| `song.set_position` | Move the song play head: beat |
| `song.stop` | Stop playback |
| `song.tracks` | List tracks of the song (container="song") or the pattern editor (container="pattern") |
| `track.add_clip` | Add a clip: id, start, length?, name?; sample tracks: file; automation tracks: target (param id) and points; instrument tracks: notes |
| `track.add_effect` | Append an effect to the track's chain: id, plugin, key?, params? |
| `track.clips` | List the clips of a track |
| `track.clone` | Duplicate a track with all its clips |
| `track.delete` | Remove a track |
| `track.effects` | Effects in the track's effect chain |
| `track.info` | Describe a track |
| `track.instrument` | The instrument plugin of an instrument track |
| `track.load_preset` | Load an instrument preset file (.xpf): id, path |
| `track.move` | Move a track to another index in its container: id, index |
| `track.note_off` | Release a note started with track.note_on: id, pitch |
| `track.note_on` | Play a note live: id, pitch, velocity (1-127) |
| `track.params` | The track's own parameters (volume, panning, envelopes, arpeggio, ...) |
| `track.pattern_clip` | The clip of a pattern-editor track for a pattern index: id, pattern |
| `track.save_preset` | Save an instrument track as a preset file (.xpf): id, path |
| `track.set` | Set name, muted, solo, color, volume, panning, pitch, mixer_channel or any params {key: value} |
| `track.set_instrument` | Replace the instrument: id, plugin, key?, params?, keep_name? |

---

## 8. Configuration and troubleshooting

| Setting | Purpose |
|---|---|
| `LMMS_PYTHON` environment variable, or `<scripting python="…"/>` in `.lmmsrc.xml` | The Python interpreter for scripts and the console. The default is `python3`, then `python`, from `PATH`. |
| `LMMS_SCRIPTING_DIR` | Use a different copy of `scripting/` (the directory with `python/lmms` and `examples`). |
| `LMMS_BINARY` | The executable `lmms.launch()` starts. |
| `--script-port` | Fixed server port. |

* **"Could not start Python"**: install Python 3.6 or newer, or point `LMMS_PYTHON` at it.
* **`LmmsError: no running LMMS script server found`**: LMMS is not running with `--script-server`,
  and its Script Console hasn't started a server (it starts one when it first runs code or when you
  press **Agent link**). Use `lmms.launch()` instead.
* **A GUI script seems stuck**: LMMS may be showing a dialog. A missing plugin while loading the
  default template or a project is one example. Use `song.new(empty=True)` or headless mode for
  unattended runs.
* **Security**: the server only listens on 127.0.0.1 and requires the random per-session token, which
  is stored in a file only you can read. Anyone holding the token can do anything LMMS can, including
  writing files, so don't share it.
* Scripts run in a separate Python process. A crashing or endless script can't take LMMS down:
  **Stop** in the console, or Ctrl+C in a terminal, ends it.
