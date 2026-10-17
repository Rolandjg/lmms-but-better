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
3. Select a note, enter Pitch Bend mode with **Shift+T**, and draw points on the
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
  or steps; handles only show on smooth curves), reset, and the external synth
  bend range.
- **Shift+left-click** a note opens its full Automation Editor.

## MIDI behavior and limits

The track's pitch control is added to the glide. The combined bend is limited to
the configured bend range, so use a range large enough for both. Match the range
inside the synth manually when it does not honor MIDI pitch-bend sensitivity.

Standard MIDI pitch bend affects every sounding note on the channel. The newest
active glide controls that channel; when it ends, the previous active glide is
restored, or the track pitch if none remains. This also affects release tails.
Use separate tracks/channels for independently bending parts. This change does
not add MPE channel allocation or independent polyphonic note expression.

Curves are saved in the existing note-detuning project format. MIDI-file export
of these curves is not included; the new routing applies during playback and
audio rendering.

## Regression checks

Build with tests enabled, then run `ctest --test-dir build/tests --output-on-failure`.
`MidiNotePitchTest` checks center, range limits, channel isolation and overlapping
glide ownership. `Vst3HostTest::noteGlideThroughInstrument` checks actual host
routing, frame offsets, track pitch composition and reset before the next note.

The VST3 test executable also accepts optional integration checks:

```sh
QT_QPA_PLATFORM=offscreen LMMS_PLUGIN_DIR="$PWD/build/plugins" LMMS_DATA_DIR="$PWD/data" \
  LMMS_TEST_VITAL=/usr/lib/vst3/Vital.vst3 build/tests/Vst3HostTest installedVitalGlide

QT_QPA_PLATFORM=offscreen LMMS_PLUGIN_DIR="$PWD/build/plugins" LMMS_DATA_DIR="$PWD/data" \
  LMMS_TEST_GLIDE_GUI=1 build/tests/Vst3HostTest glideEditor
```

The Vital check measures the output frequency before and after an octave glide.
The editor check exercises tangent handles, Undo, smooth interpolation and project
serialization. Set `LMMS_TEST_GLIDE_SCREENSHOT` to save an editor preview.
