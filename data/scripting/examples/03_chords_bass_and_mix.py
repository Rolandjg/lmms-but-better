"""A small arrangement: chord pad, bass line, drums, a mixer bus, an automated filter sweep,
then a render with loudness statistics and a saved project."""
import os
import lmms

song = lmms.song
song.new(empty=True)
song.tempo = 120
out_dir = os.path.join(os.path.expanduser("~"), "lmms_script_demo")
os.makedirs(out_dir, exist_ok=True)

progression = ["Am7", "Fmaj7", "C", "G"]           # one chord per bar

# --- pad: chords
pad = song.add_instrument_track("tripleoscillator", name="Pad", color="#9b7bff")
pad.volume = 70
chords = pad.add_midi_clip(0, length=lmms.bars(len(progression)))
for bar, name in enumerate(progression):
    chords.add_chord(lmms.chord(name), start=lmms.bars(bar), length=lmms.bars(1), velocity=60)

# --- bass: root notes in eighths, an octave down
bass = song.add_instrument_track("lb302", name="Bass", color="#ff9f43")
line = bass.add_midi_clip(0, length=lmms.bars(len(progression)))
for bar, name in enumerate(progression):
    root = lmms.chord(name)[0] - 24
    line.add_notes([(root, lmms.bars(bar) + i * 0.5, 0.4, 110 if i % 2 == 0 else 80) for i in range(8)])

# --- drums
beat = song.add_pattern_track("Drums")
kick = lmms.patterns.add_instrument_track("kicker", name="Kick")
kick.set_steps([0, 4, 8, 12], pattern=beat.pattern_index)
for bar in range(len(progression)):
    beat.add_clip(lmms.bars(bar))

# --- mixer: pad and bass on a bus with a compressor
bus = lmms.mixer.add_channel("Music bus")
pad.mixer_channel = bus
bass.mixer_channel = bus
bus.add_effect("compressor")
bus.volume = 0.9

# --- automation: open the bass filter over the 4 bars
cutoff = bass.instrument.params.param("vcf_cut")
print("Automating", cutoff)
sweep = song.add_automation_track("Bass cutoff")
sweep.add_automation_clip(cutoff, start=0, length=lmms.bars(4),
                          points=[(0, 0.2), (lmms.bars(4), 1.2)], progression="linear")

# --- render and save
stats = song.render(os.path.join(out_dir, "demo.wav"))
print("Render: %.1f s, peak %.1f dBFS, RMS %.1f dBFS, clipped samples: %d"
      % (stats["seconds"], stats["peak_db"], stats["rms_db"], stats["clipped_samples"]))
if stats["peak_db"] > -0.5:
    song.master_volume = 80
    print("Too hot - lowered the master volume to 80%")
print("Saved", song.save(os.path.join(out_dir, "demo.mmpz")))
