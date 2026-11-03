gippity wrote this
# Glide curves with external synths

Piano Roll pitch-bend curves now send MIDI pitch bend to MIDI-based instruments
(including VeSTige and VST3) and the track's MIDI output. Native instruments keep
their existing per-note detuning behavior.

## Quick start with Vital

1. Load Vital on an instrument track and open a MIDI clip in the Piano Roll.
2. Set Vital's **Pitch Bend Range** and **Glide → External synth bend range…**
   to the same number of semitones. For octave glides, use 12 in both places.
   LMMS also sends pitch-bend sensitivity to synths that support MIDI RPN 0.
3. Turn on **Glide → MPE: bend each note separately** (or the MPE box in the
   track's MIDI tab) so a glide never bends other notes. Vital needs no extra
   setting for this: it bends each MIDI channel separately on its own.
4. Select a note, enter Pitch Bend mode with **Shift+T**, and draw points on the
   curve. Curve heights are semitones relative to the note's original pitch.

## Editing

- Click a note to edit its curve, then click or drag to add and move points.
  Points snap to the grid once they move sideways; a point placed off the grid
  keeps its exact time while you only drag it up or down. Right-click or
  right-drag erases points. The floating readout shows the bend and the pitch
  it lands on, e.g. `+7 semitones → G4`.
- Drag the round handles beside a point to shape the curve like a Bézier
  editor. Paired handles move together; hold **Alt** to adjust one side, or
  right-click a handle to restore its automatic tangent. Handles keep a fixed
  length on screen, so even very steep tangents stay within reach. Points and
  handles highlight under the mouse.
- Hold **Alt** to bypass the time grid, or **Ctrl** for pitch between semitones.
- Select several notes before entering Pitch Bend mode to edit their curves
  together. Selected curves with the same number of points are kept identical
  while you edit; curves with a different point count are left unchanged.
- Piano keys and the velocity/panning lane keep working in Pitch Bend mode.
- The arrow next to the Pitch Bend tool (or **Shift+right-click**) opens the
  glide menu: curve shape for the selected notes (smooth curves, straight lines
  or steps; handles only show on smooth curves), reset, the external synth
  bend range and MPE mode.
- **Shift+left-click** a note opens its full Automation Editor.

## MIDI behavior and limits

The track's pitch control is added to the glide. The combined bend is limited to
the configured bend range, so use a range large enough for both. Match the range
inside the synth manually when it does not honor MIDI pitch-bend sensitivity.

Without MPE, standard MIDI pitch bend affects every sounding note on the
channel. The newest active glide controls that channel; when it ends, the
previous active glide is restored, or the track pitch if none remains. This also
affects release tails.

### MPE mode

With MPE on, the track sends each note on its own member channel (MIDI channels
2–16 of an MPE lower zone), and every note sets its channel's bend right before
its note-on, so a curve that ends off pitch can not transpose later notes. The
track pitch is added to every note's own bend, and the master channel (1)
carries no bend. Release tails keep their final bend: a released channel is
reused only after all others, and a new note on it sends its own bend first.

- **CHANS** in the MIDI tab sets how many member channels notes are spread over
  (1–15). With more simultaneous notes than channels, the oldest note shares its
  channel with the new one and bends with it.
- On enabling, LMMS sends the MPE Configuration Message (RPN 6) on channel 1 and
  pitch-bend sensitivity (RPN 0, the track's bend range) on every member channel.
  Synths that ignore RPN 0, such as Vital, still need their own bend range set to
  the track's. Changing the bend range later resends only RPN 0, since a
  configuration message resets the zone and can cut sounding notes. Changing
  CHANS resends the configuration.
- On disabling, notes already playing keep their member channels and bends. Once
  the last of them ends, LMMS ends the zone (an MPE configuration with no members),
  recenters the member channels and moves the track pitch back to the output channel.
- The synth must apply pitch bend per channel. Vital does so even with its own
  MPE switch off. Other synths may need their MPE mode turned on.
- Some VST3 synths, such as Serum 2, map pitch bend of every channel to one
  parameter, so channel bends would move all notes. When such a plugin supports
  the VST3 tuning note expression, the VST3 host turns member-channel bends into
  per-note tuning instead, using the track's bend range (the synth's own bend
  range does not matter). It then also holds back the MPE configuration message,
  because Serum 2 ignores per-note tuning in its own MIDI MPE mode. Leave MPE off
  in Serum's settings.
- MIDI output forwards all member channels in MPE mode, and the output channel
  setting is ignored. Track-wide controllers (MIDI CC knobs) and program changes
  go to the master channel.

Curves are saved in the existing note-detuning project format. MIDI-file export
of these curves is not included; the new routing applies during playback and
audio rendering.

## Regression checks

Build with tests enabled, then run `ctest --test-dir build/tests --output-on-failure`.
`MidiNotePitchTest` checks center, range limits, channel isolation and overlapping
glide ownership. `MpeChannelAllocatorTest` checks member channel allocation, reuse
and stealing. `Vst3HostTest::noteGlideThroughInstrument` checks actual host
routing, frame offsets, track pitch composition and reset before the next note.
`Vst3HostTest::mpeGlidesAreIndependent` checks that MPE notes bend separately, that
a note reusing the channel of a glide ending off pitch starts unbent, and the MPE
configuration messages. `Vst3HostTest::mpeBendsBecomeNoteTuning` checks the same for a
plugin with one shared bend parameter and VST3 per-note tuning.
`Vst3HostTest::mpeToggleWithHeldNotes` checks that turning MPE off leaves held notes
bent until they end. `Vst3HostTest::bendRangePrecedesFirstNote` checks that a VST3
plugin gets the RPN selection, its data entry and the first note on separate frames
in that order, also for notes at the start of a period. VST3 delivers controllers
as parameter changes with no defined order within a frame, so the host holds the
RPN selection back until the data entry and then gives each its own frame.

The VST3 test executable also accepts optional integration checks:

```sh
QT_QPA_PLATFORM=offscreen LMMS_PLUGIN_DIR="$PWD/build/plugins" LMMS_DATA_DIR="$PWD/data" \
  LMMS_TEST_VITAL=/usr/lib/vst3/Vital.vst3 build/tests/Vst3HostTest installedVitalGlide installedVitalMpeGlide

QT_QPA_PLATFORM=offscreen LMMS_PLUGIN_DIR="$PWD/build/plugins" LMMS_DATA_DIR="$PWD/data" \
  LMMS_TEST_SERUM=~/.vst3/yabridge/Serum2.vst3 build/tests/Vst3HostTest installedSerumMpeGlide

QT_QPA_PLATFORM=offscreen LMMS_PLUGIN_DIR="$PWD/build/plugins" LMMS_DATA_DIR="$PWD/data" \
  LMMS_TEST_GLIDE_GUI=1 build/tests/Vst3HostTest glideEditor
```

The Vital checks measure the output frequency before and after an octave glide.
The MPE checks for Vital and Serum measure a glide ending two semitones down, the
glide while a plain note plays on another channel, and a plain note reusing its channel.
The editor check exercises tangent handles, Undo, smooth interpolation and project
serialization. Set `LMMS_TEST_GLIDE_SCREENSHOT` to save an editor preview.
