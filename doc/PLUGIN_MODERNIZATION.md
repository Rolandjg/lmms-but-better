# Native plugin modernization

This document records how the bundled plugins compare with the equivalent devices in FL Studio and
Ableton Live, what has been upgraded so far, and what is still left to do.

## Shared problems across the legacy plugins

| Problem | Why it matters | FL / Ableton equivalent |
|---|---|---|
| Fixed-size bitmap UIs (`artwork.png/svg`, pixmap knobs, absolute `move()` positioning) | They look dated, don't scale, and every visual change means editing artwork | Vector UIs with a shared visual language |
| No visual feedback (curves, meters, scopes) | Users set delays, filters and reverbs blind | Echo shows taps, Auto Filter and EQ Eight show curves, Utility shows width |
| Linear frequency knobs (DualFilter cutoff: 1–20000 Hz on a linear knob) | The whole bass range fits in a few degrees of knob travel | Frequency controls are always logarithmic |
| Integer-sample delay reads (Delay, Flanger) | Modulated delays step from sample to sample, which sounds zippery and grainy | Interpolated, smoothed delay lines |
| Missing "standard" controls (mix, ping-pong, feedback filters, pre-delay, width, bass mono, unison…) | Users need extra plugins for things every modern DAW device does | See per-plugin sections below |

## Shared building blocks (new)

* **`KnobType::Modern`, `ModernSmall`, `ModernLarge`** (`include/Knob.h`) use the theme's standard
  knob artwork and arc/pointer colors, like `Bright26`. Unlike `Bright26`, they draw bipolar
  ranges from zero and render their own compact label. `TempoSyncKnob` and `VolumeKnob` work with
  them unchanged.
* **`include/ModernWidgets.h`** provides layout-based building blocks styled after the default
  LMMS theme: `#262b30` background, recessed panels, `#111314` graph screens, the LMMS green for
  values and curves, and flat grey buttons that show green text on dark green when active (like
  the Compressor's mode buttons).
  * `ModernSection`: a titled panel with a grid layout.
  * `ModernToggle`: a flat checkable button for `BoolModel`s.
  * `ModernSegmented`: a segmented selector for any `IntModel` or `ComboBoxModel`. It supports text
    or custom-painted glyphs, per-item tooltips and double-click.
  * `ModernMeter`: a stereo peak meter using the mixer fader colors.
  * `ModernStereoScope` + `ScopeBuffer`: a goniometer with a phase-correlation readout.
  * `modern::paintDisplay` and the palette: `accentColor()` (LMMS green) is used everywhere;
    `secondaryColor()` only where a display must tell two signals apart (right channel,
    filter 2); `warningColor()` for exceptional states such as freeze.
* **`include/ModernDsp.h`** provides header-only DSP: a TPT one-pole / two-pole filter, a
  parameter smoother, an envelope follower, a Hermite-interpolated delay line, soft saturation and
  M/S width.
* **Core fix:** `InstrumentTrackWindow` no longer clips instrument views that are taller than
  254 px, because the tab bar height is now accounted for.

The plugins keep the look of native LMMS plugins. Only the layout is new (layouts instead of absolute positions on bitmap artwork), plus displays that show what the controls do.

## Upgraded plugins

Every new parameter defaults to the old behavior, so existing projects load and sound the same.
The `legacy:` mode of the snapshot tool checks this (see *Testing* below).

### Delay (vs. Ableton Echo / FL Fruity Delay 3)
Gaps: mono-summed identical L/R repeats only, no ping-pong, no feedback tone shaping, integer
delay reads (zipper noise under LFO), no ducking, and a UI that was an XY dot on a bitmap.

Changes:
* Stereo / Ping-pong / Mono modes and a right-channel **stereo offset**.
* Feedback-loop **low cut and high cut** (12 dB/oct) and **drive** saturation, for tape/analog-style
  repeats.
* **Freeze** (infinite hold, input muted), **ducking** (the echoes dip while the input plays) and
  **width**.
* Hermite-interpolated reads with a 60 ms time glide, so delay-time changes repitch smoothly like
  tape instead of clicking. The LFO is now quadrature (L/R 90° apart).
* Fixed a bug where the automated LFO rate was used as a frequency instead of a period.
* UI: an **echo-tap display** showing when each repeat arrives, at what level and on which channel.
  Drag it horizontally to change time and vertically to change feedback. Also added an output
  meter and grouped sections.

### ReverbSC (vs. Ableton Reverb / FL Fruity Reeverb 2)
Gaps: only size/color/in/out, no pre-delay, no input filtering, no width, no freeze, and no
indication of decay time.

Changes: **pre-delay** (0–500 ms), input **low cut**, **modulation depth**, **width**,
**freeze**, **ducking**, and a log-scaled color knob. UI: a **decay display** plots the estimated
RT60 for lows and for damped highs, with a text readout of the decay time. Also added an output
meter.

### Amplifier → utility (vs. Ableton Utility / FL Fruity Balance + Stereo Shaper)
Gaps: gain/pan/L/R only.

Changes: **channel mode** (Stereo/Left/Right/Swap), **phase invert L/R**, M/S **width** (0–400 %),
**bass mono** with an adjustable crossover (a complementary split that reconstructs perfectly),
and a **DC filter**. UI: a **goniometer with a correlation meter**, and an output meter.

### StereoEnhancer → stereo imager (vs. FL Stereo Shaper / Ozone Imager)
Gaps: a single Haas-delay "width" knob measured in samples.

Changes: a **3-band imager** with per-band width and two movable crossovers (a complementary split,
so it is transparent at 100 %). The Haas widener is kept. UI: an interactive band display (drag
bands up/down for width, drag crossover lines, double-click to reset), plus a goniometer and a meter.

### Flanger (vs. Ableton Chorus-Ensemble / FL Fruity Flangus)
Gaps: integer delay reads (the sweep stepped audibly), **no dry/wet mix inside the effect** (at the
default 100 % wet rack setting it produced vibrato rather than flanging), sine LFO only, and no
visualization.

Changes: interpolated sweep, a **mix** control (legacy projects load at 100 % to keep their sound),
**sine/triangle** LFO, and cross-channel mode clarified as "CROSS L/R". UI: a **live comb-filter
response** animated by the actual sweep position.

### DualFilter (vs. Ableton Auto Filter / FL Fruity Love Philter)
Gaps: linear cutoff knobs, parallel-only routing, no drive, and no response curve.

Changes: **log cutoff**, **Parallel/Serial routing**, and input **drive**. UI: a
**frequency-response display** measured from GUI-side copies of the actual `BasicFilters`, so all
22 filter types are exact. It shows each filter plus the combined output, including phase
interaction in parallel mode. Draggable handles set cutoff (x) and resonance (y).

### TripleOscillator (vs. FL 3xOsc / Ableton Operator / Serum-style unison)
Gaps: no unison, tiny pixmap UI (8 wave icons of 15 px, modulation modes shown as unlabeled images).

Changes: a **unison engine**: 1–8 voices per note, detune spread (cents) and stereo spread. It uses
equal-power voice panning with 1/√N gain, so stacking keeps the same loudness (verified: 1 voice
vs. 5 voices differ by < 0.5 dB). Each voice duplicates the full modulation chain, so PM/AM/FM/sync
still work in unison. Voices use free-running random phases. UI: the original artwork and buttons
are kept unchanged. The unison controls (a voices LCD plus detune and spread knobs in the
original knob style) sit on a strip below the artwork, filled with the artwork's own grey.

### BassBooster, Bitcrush
UI converted to the modern kit (sections, modern knobs, pill toggles). DSP unchanged.

## Round 2: modernizing while keeping each plugin's character

These plugins keep their original artwork and visual identity. New controls and displays are drawn
in each plugin's own palette, and where more room was needed the artwork is extended by
continuing its own texture.

### AudioFileProcessor (vs. Ableton Simpler / FL Sampler)
* **Warp**: stretches the sample to the song tempo without changing its pitch. The sample tempo is
  detected from the file name ("... 140 BPM") or from its length (assuming whole 4/4 bars), and is
  shown on an LCD. Stretching uses a new dependency-free WSOLA stretcher (`include/TimeStretch.h`),
  runs on the GUI thread, is cached per tempo ratio, and is swapped in safely.
* **Loop crossfade** (0–500 ms): an equal-power crossfade is baked into the loop end, which makes
  forward loops seamless. It is not applied to reversed samples or ping-pong loops.
* Projects still save the original audio; warp and crossfade are re-applied on load.

### Kicker
Adds a live preview of the kick, rendered offline with the same oscillator the instrument plays:
the waveform in Kicker's blue with the pitch sweep drawn over it, on an engraved screen between
the knobs and the logo.

### WaveShaper, DynamicsProcessor
* WaveShaper gets **1x/2x/4x/8x oversampling** (the same polyphase filters as SlewDistortion).
  Old projects load at 1x; new instances default to 2x.
* DynamicsProcessor gets **lookahead** (off / 1 / 3 / 5 / 10 ms). The detector sees the signal
  early while the audio (wet and dry) is delayed.
* Both get a glowing **level marker on the transfer curve** showing where the signal currently is
  (`GraphLevelIndicator`). The new rows extend the slate panels and use outline-style buttons that
  match the plugins' +1dB/RESET buttons.

### Eq
* **Channel mode**: L+R, MID or SIDE, so the bands can process only the mid or only the side
  signal (for example, cutting lows from the side).
* **Band solo**: Alt/Option-click a band's handle to hear only the region that band affects, for
  as long as the button is held.

### TripleOscillator, BitInvader, Watsyn: unison
All three share `dsp::unisonVoice()`: an even detune and stereo spread, equal-power panning and
1/√N gain. Each voice starts at a random point in time rather than at a random phase per
oscillator, so the phase relationships between oscillators (which MIX, AM, PM and sync depend on)
are preserved. A single voice is bit-identical to before. BitInvader's knobs sit next to its
Length knob with a matching "Unison" caption. Watsyn gets a row in its frosted texture, with
printed-style knob wells and labels.

### LB302: 303-style step sequencer
A 16-step pattern with gate, pitch (±12 semitones), accent and slide per step, 1/8, 1/16 or 1/32
note steps, a pattern length of 1–16 steps and an accent amount. Holding a key plays the pattern
from that root note, synced to the song tempo:
* Slide ties into the next step and glides without retriggering.
* Accent boosts volume and the filter envelope.
* Plain notes gate at 55 % of a step.

It starts with a default acid pattern and is off by default, so old projects are unchanged. The
grid shows pitch bars, a red LED on the running step and A/S switches, in a section drawn in the
plugin's grey-and-pink splatter style. The pitch sequence was checked by rendering and measuring
each step.

### MultitapEcho
Adds **per-tap panning** (a third bar graph, drawn like the amplitude and filter graphs) and
**feedback**, which repeats the whole tap pattern. The feedback loop gain is divided by the
pattern's total gain, so it stays stable for any tap levels. The overlap between the control
labels and "Swap inputs" is also fixed.

### CrossoverEQ
Adds per-band **solo** (the theme's mixer solo buttons) and per-band **stereo width**. The SVG
panel was extended to hold them.

### Sf2Player
Adds previous/next preset arrows (in the plugin's magenta) inside the patch field, for quick
browsing without opening the patch dialog.

### Core
* `ModernSegmented` gained an Outline style, plus `GraphLevelIndicator`, `dsp::Biquad` and
  `dsp::unisonVoice`.
* `LeftRightNav.h` is now self-contained and exported.

## Round 3: displays for the knob-only effects

These effects were a row of knobs with no feedback. Each now has a display that shows what the
settings do, drawn with the shared modern kit. Their DSP is unchanged except where noted.

### Bitcrush
A scope of the incoming signal (grey) with the effect's sample-and-hold and quantizer re-run on the
same samples (green staircase), so Rate and Levels are visible on the real material. Faint lines
mark the quantization steps when they are coarse enough to see. Quiet signals zoom in by octaves,
and a test sine is shown while the input is silent. The readout gives the rate with its
downsampling factor and the levels as effective bits. Also added an output meter.

### BassBooster
* **The FREQ knob is now in Hz.** It used to set a raw filter coefficient: higher values gave a
  *lower* corner (100 ≈ 69 Hz, 200 ≈ 35 Hz) while the hint said "Hz". The new `cutoff` attribute
  (20–250 Hz, log) is converted from the old `freq` on load, and `freq` is still written so older
  versions open new projects with the same sound. The coefficient is now computed for the actual
  sample rate.
* A response curve with a handle: drag it sideways for frequency and up or down for boost, or
  double-click to reset. A readout shows "+x dB below y Hz", and there is an output meter.

### Peak Controller
A scrolling history of the measured input level (cyan), the controller output after attack and
release (green), the base level and the threshold. Drag the threshold line to set it. A bar
shows the current controller value. The 0–1 knob values are translated into real units:
threshold in dB, and attack and release as time constants. These show that the
attack and release knobs only cover about 0.1–110 ms, with most of their travel under 3 ms. The
output curve follows the same smoothing as `PeakController`, so it shows what connected knobs will
do even before anything is linked. The bitmap background was replaced by the modern kit.

### Crossover EQ
A band display shows the four bands on a frequency axis. The curve is the combined response of
the actual Linkwitz-Riley tree, including the phase dip between the 2/3 and 3/4 splits at equal
gains (the plugin has no allpass compensation) and the notches left by muted bands. Drag the
crossover lines sideways and the bands up or down for gain, or double-click a band to reset it.
Live band levels run along the bottom edge and drive each fader's built-in level bars. The panel
was rebuilt with the modern kit as per-band sections (fader, mute and solo, width) next to a Split
section.

### Dispersion
Two displays: the **group delay per frequency** of the whole all-pass chain, including feedback
(for example "255 Hz delayed 271 ms"), and the **impulse response**, which shows the chirp that a
click turns into. Both are computed from the same coefficients and filter structure as the effect,
and are recalculated only when a parameter changes.

### Core and fixes
* **Instrument windows no longer tile their artwork.** The *Chord stacking & arpeggio* tab needed
  286 px, so every 250 px instrument (Organic, sfxr, BitInvader, Vibed…) was stretched and its
  background pixmap repeated at the bottom. The tab's spacing was tightened to fit 250 px.
* `GroupBox::ledButtonShown()` uses `!isHidden()` so the caption doesn't slide under the LED when
  the group box is rendered before being shown.
* TripleOscillator's unison labels are positioned by baseline and are no longer clipped.
* `PluginSnapshot`: `PLUGINSNAPSHOT_TAB=<n>` also saves the instrument window with tab *n* active.

## Round 4: AudioFileProcessor rebuilt as a sampler

The bitmap UI (a logo taking a third of the window and a 75 px waveform) was replaced by a flat
560 px editor built around a large waveform, in the modern kit's colors without boxed sections.

* **Sample editor**: zoom with the wheel (around the mouse) or by dragging up and down, pan by
  dragging sideways or with Shift+wheel, and use the overview strip underneath to jump around.
  Double-click to show the whole sample. Start, loop and end are lines with grips. The region
  outside them is dimmed, the loop range is tinted, amplification that would clip is marked red,
  and a time ruler and a playback cursor are shown. The cursor is polled from the audio thread
  instead of a signal per note per period, and the "AFP animation" setting still turns it off.
* **Fade in / fade out**: square handles at the top of the waveform (like the sample clip fades),
  plus log-scaled knobs. The fades are equal-power and are computed per note from the playback
  rate, so they line up exactly with the sample. Fade-out only applies when the sample isn't
  looping.
* **Slice mode** (Simpler's Slice, which also covers SlicerT's job): **AUTO** (transients, with a
  sensitivity knob), **GRID** (1/16 to 1 bar of the sample tempo, anchored at the start point) or
  **MANUAL**. Each key from the base note up plays the next slice at its original pitch. Slices
  get short declick fades and loop individually when looping is on. Double-click the waveform to
  add a marker, drag markers to move them, double-click (or right-click) one to remove it. Editing
  by hand switches to MANUAL. The numbered pads above the waveform play their slice while held.
  **COPY MIDI** copies one note per slice in the original rhythm for the piano roll. The transient
  detector is dependency-free: it measures full-band and high-boosted energy per ~6 ms hop, picks
  peaks, refines onsets to 32-frame windows and backs up to a zero crossing.
* **One-shot**: a note plays to the end of the sample or slice however short it is. It works by
  extending the release (`desiredReleaseTimeMs`) and replacing the linear release fade with a 3 ms
  declick. **Choke**: a new note fades out the notes still playing in 4 ms (lock-free, by note
  sequence number).
* **Pitch**: transpose (±48 semitones) and fine tune (±100 cents). **DETECT KEY** runs YIN on the
  region and sets the track's base note and the fine tune so the keyboard plays the sample in tune
  (piano01 → C4, bass01 → C2; synthetic tones within 2 cents up to 1.5 kHz).
* **Normalize** (button and context menu), **zero-crossing snap** for points and markers (Shift
  bypasses it), **previous/next sample** in the same folder, and dropping files from a file manager.
* Toggling reverse now keeps the region and the slices over the same audio in the model rather than
  in the view, so it also works with the window closed.

Every new setting defaults to the old behavior. Old projects load unchanged (checked by loading
attribute-only XML), and the snapshot tool's classic-mode render has the same energy as before. Tests:
`AudioFileProcessorViewTest` drives the editor with the mouse (point and fade dragging, adding,
moving and removing slices, pads, zoom). `PluginSnapshot` accepts `PLUGINSNAPSHOT_KEY=<key>` to play
a key other than A3, for slice mode.

## Remaining gaps (roadmap)

| Plugin | Gap vs. FL / Ableton | Suggested work |
|---|---|---|
| AudioFileProcessor | Warp uses WSOLA, which is fine for loops but smears sustained chords; no per-slice settings (gain, pitch, reverse) | Optionally use Rubber Band when it is available at build time; per-slice parameters |
| SlicerT | Its BPM sync resamples, so pitch changes with tempo | Reuse `dsp::timeStretch` for pitch-preserving sync |
| Monstro, Organic | Very dense bitmap UIs | They are already feature-rich; only readability tweaks (larger labels, tooltips) would help without losing their look |
| GigPlayer | Same missing preset stepping as Sf2Player had | Port the Sf2Player arrows |
| Eq | No dynamic bands | Per-band dynamic gain |

Corrections to the first version of this document: core's `Rubberband.h` is the selection
rubber-band widget, not the Rubber Band time-stretch library (hence the new WSOLA stretcher).
Sf2Player already had a searchable patch dialog.

## Testing

`tests/plugins/PluginSnapshot.cpp` (`make PluginSnapshot`, not part of ctest) is a developer tool
that starts the real GUI offscreen. It does three things:

* `effect:<name>[:param=value,...]` runs 3 s of plucked test signal through the effect, fails on
  NaN/Inf or runaway gain, and saves the effect dialog as a PNG.
* `instrument:<name>[:param=value,...]` plays a note through the instrument (including
  single-streamed ones such as LB302), reports energy and side (stereo) energy, and saves the full
  instrument window. A parameter value of `!` removes that setting, which simulates an older
  project. With `PLUGINSNAPSHOT_PITCH=1` it also prints the detected pitch of every 1/16 step.
* `legacy:<name>:attr=value,...` loads settings that contain only the given (old) attributes and
  prints the resulting saved state, to check backward compatibility.

```sh
QT_QPA_PLATFORM=offscreen LMMS_PLUGIN_DIR=build/plugins LMMS_DATA_DIR=data \
  build/tests/PluginSnapshot /tmp/shots effect:delay:Mode=1,FeebackAmount=0.6 instrument:tripleoscillator
```

Note: `Knob` gained a member, so all plugins must be rebuilt together with core. Mixing old plugin
binaries with the new core crashes.
