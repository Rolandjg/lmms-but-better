**What this fork changes**
--------------
* Windows native VST3 support through [yabridge.](https://github.com/robbert-vdh/yabridge)
* MPE pitch gliding support in the piano roll.
* An option to bounce a track to audio, creating a sample track below the original.
* Piano roll UX changes: 
  	- Better note duplication with CTRL+D: the copy lands right after the selection on the grid (on whole bars
  	  for phrases longer than half a bar) and the view follows it.
  	- Better pitch bending directly in the piano roll: grab points and Bézier handles right on the curve, with
  	  smooth / straight / stepped curve shapes and a readout of the pitch you land on. See
  	  [doc/glide-notes.md](doc/glide-notes.md).
  	- **Humanize** (Shift+H): randomize velocity, timing and length by an amount you choose, with a live preview.
* **Command palette** (Ctrl+K or Ctrl+Shift+P, or the search field in the toolbar): fuzzy-search every menu
  entry, editor tool, instrument, effect, track and mixer channel. Type `tempo 128` to set the tempo.
  Recently used commands come first.
* **MIDI Capture** (Ctrl+Shift+R, or the circular-arrow button in the toolbar): LMMS always listens to MIDI input,
  so you can turn what you just played into a clip after the fact, even if you weren't recording. Takes played during
  playback land where you played them; free playing lands at the song position marker. Notes played on a Pattern
  Editor instrument go into the current pattern. The button's tooltip says what it would recover.
* **Pumper** effect: tempo-locked volume shaping for sidechain-style pumping, with an editable curve
  (depth, attack, hold, release, curve, offset), rates from 1/1 to 1/16 including triplets, and optional
  ducking of only the lows or highs.
* **Loudness Meter** effect: EBU R128 / ITU-R BS.1770 momentary, short-term and integrated LUFS, loudness
  range and 4x oversampled true peak, with a 60 second history and delivery targets (streaming, Apple,
  broadcast, club, podcast).
* **Limiter** effect: transparent lookahead brickwall limiter for mastering, with 4x oversampled true-peak
  detection, gain, ceiling, release, lookahead and stereo link, plus a scrolling input / output / gain
  reduction display. Drag the display up to push the level, or drag the ceiling line.
* **Formant Shifter** effect with two engines:
  	- *Spectral*: moves a sound's own formants (±24 st) without changing its pitch, pitch-shifts while
  	  keeping the formants (or letting them follow, for chipmunk / slowed-tape sounds), and imprints vowels
  	  onto any sound like a talkbox. It analyses with a true spectral envelope and shifts pitch with a
  	  phase-locked vocoder, and adds ~43 ms of latency, which LMMS compensates.
  	- *Resonant*: the classic zero-latency formant filter (five resonant band-passes).
  	- Vowel morph A–E–I–O–U over sung formant tables (bass to soprano), stereo spread, drive, an LFO
  	  (7 shapes, free or tempo-synced to the song position) and an envelope follower on the vowel and the
  	  formants, and 26 presets (Helium, Monster, Male to Female, Talkbox Vowel Sweep, Yoi Bass, Wobble Talk,
  	  Auto-Wah Vowels, Throat Singer...). Drag the spectrum display sideways to shift the formants and scroll
  	  it to morph the vowel. Every control can be automated.
* **Audio clip fades and clip gain**: hover an audio clip in the Song Editor and drag the handles on its top
  edge. The corner handles set equal-power fade in / fade out, and the middle handle sets clip gain (drag up
  or down, Shift for fine steps). Double-click a handle to reset it. The context menu adds *Normalize clip
  gain*, *Reset clip gain* and *Remove fades*. Fades stay at the outer edges when a clip is split.
* **Arrangement markers**: name the sections of your song (Intro, Verse, Chorus, Drop...) on a colored
  marker lane above the Song Editor timeline. Double-click the lane or press M to add a marker, drag to move,
  click to jump, and right-click for presets, colors, *Loop this section* and removal. Alt+Left / Alt+Right
  jump between markers, and the command palette lists every marker ("Go to marker: Chorus").
* **Python scripting** (like Blender's `bpy`), built so AI agents can make music in LMMS: the `lmms` module
  creates tracks, notes, chords, patterns, automation and mixer routing, loads instruments and effects
  (VST3 included), sets any plugin parameter, and renders with peak/RMS stats. You can run scripts in the
  **Script Console** (Ctrl+8), with `lmms --script song.py`, headless with `lmms script song.py`, or from any
  Python process via `lmms --script-server` and `lmms.connect()` / `lmms.launch()`. See
  [doc/SCRIPTING.md](doc/SCRIPTING.md) and the examples in `data/scripting/examples`.

**Contributing to THIS fork**
--------------
Go crazy with any pull requests.

If you use an LLM in your PR, name the LLM.

<div align="center">
	<h1>
	<img src="https://raw.githubusercontent.com/LMMS/artwork/master/Icon%20%26%20Mimetypes/lmms-64x64.svg" alt="LMMS Logo"><br>LMMS
	</h1>
	<p>Cross-platform music production software</p>
	<p>
		<a href="https://lmms.io/">Website</a>
		⦁︎
		<a href="https://github.com/LMMS/lmms/releases">Releases</a>
		⦁︎
		<a href="https://lmms.io/documentation">User manual</a>
		⦁︎
		<a href="https://lmms.io/showcase/">Showcase</a>
		⦁︎
		<a href="https://lmms.io/lsp/">Sharing platform</a>
		⦁︎
		<a href="https://github.com/LMMS/lmms/wiki">Developer wiki</a>
		⦁︎
		<a href="https://lmms.github.io/lmms">Internal documentation</a>
	</p>
	<p>
		<a href="https://github.com/LMMS/lmms/actions/workflows/build.yml"><img src="https://github.com/LMMS/lmms/actions/workflows/build.yml/badge.svg" alt="Build status"></a>
		<a href="https://lmms.io/download"><img src="https://img.shields.io/github/release/LMMS/lmms.svg?maxAge=3600" 	alt="Latest stable release"></a>
		<a href="https://github.com/LMMS/lmms/releases"><img src="https://img.shields.io/github/downloads/LMMS/lmms/total.svg?maxAge=3600" alt="Overall downloads on Github"></a>
		<a href="https://discord.gg/3sc5su7"><img src="https://img.shields.io/badge/chat-on%20discord-7289DA.svg" alt="Join the chat at Discord"></a>
		<a href="https://www.transifex.com/lmms/lmms/"><img src="https://img.shields.io/badge/localise-on_transifex-green.svg"></a>
	</p>
</div>

What is LMMS?
--------------

LMMS is an open-source cross-platform digital audio workstation designed for music production. It includes an advanced Piano Roll, Beat Sequencer, Song Editor, and Mixer for composing, arranging, and mixing music. It comes with 15+ synthesizer plugins by default, along with VST2 and SoundFont2 support.

Features
---------

* Song-Editor for arranging melodies, samples, patterns, and automation
* Pattern-Editor for creating beats and patterns
* An easy-to-use Piano-Roll for editing patterns and melodies
* A Mixer with unlimited mixer channels and arbitrary number of effects
* Many powerful instrument and effect-plugins out of the box
* Full user-defined track-based automation and computer-controlled automation sources
* Compatible with many standards such as SoundFont2, VST2 (instruments and effects), LADSPA, LV2, GUS Patches, and full MIDI support
* MIDI file importing and exporting

Building
---------

See [Compiling LMMS](https://github.com/LMMS/lmms/wiki/Compiling)

**Running the tests:** after building, run `QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -j4`
(the GUI tests need the offscreen platform when no display is available).

**Missing instruments (SF2 Player, VeSTige, ...)?** The instruments panel only lists plugins that were built.
If `PLUGIN_LIST` is set or `LMMS_MINIMAL` is `ON` in your CMake cache (e.g. left over from a quick dev build),
every plugin not on the list, including SF2 Player, VeSTige, GIG Player, Sid and ZynAddSubFx, is silently
skipped (`LMMS_MINIMAL` builds only AudioFileProcessor, Kicker and TripleOscillator). Check with
`grep -E "PLUGIN_LIST|LMMS_MINIMAL" build/CMakeCache.txt`, then reset both and rebuild:

```
cd build && cmake -DLMMS_MINIMAL=OFF -DPLUGIN_LIST="" .. && ninja
```

SF2 Player also needs the FluidSynth development package (2.2.0 or newer), and the Windows VST hosts need
Wine; the `cmake` summary lists what was found.

Join LMMS-development
----------------------

If you are interested in LMMS, its programming, artwork, testing, writing demo songs, (and improving this README...) or something like that, you're welcome to participate in the development of LMMS!

Information about what you can do and how can be found in the [wiki](https://github.com/LMMS/lmms/wiki).

Before coding a new big feature, please _always_ [file an issue](https://github.com/LMMS/lmms/issues/new) for your idea and suggestions about your feature and about the intended implementation on GitHub, or ask in one of the tech channels on Discord and wait for replies! Maybe there are different ideas, improvements, or hints, or maybe your feature is not welcome/needed at the moment.
