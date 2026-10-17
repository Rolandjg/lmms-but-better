"""A first script: one synth, one melody, rendered to a WAV file.

Run it from the Script Console (Examples menu), or from a terminal:
    lmms script 01_hello_melody.py            # no GUI, exits when done
    lmms --script 01_hello_melody.py          # opens LMMS with the result
"""
import os
import lmms

song = lmms.song
song.new(empty=True)            # no tracks, 140 BPM, 4/4
song.tempo = 110

lead = song.add_instrument_track("tripleoscillator", name="Lead", color="#4fb3ff")
clip = lead.add_midi_clip(start=0, length=lmms.bars(2))

# (pitch, start, length[, velocity]) - beats, MIDI numbers or note names
melody = ["E4", "G4", "A4", "B4", "D5", "B4", "A4", "G4"]
for i, pitch in enumerate(melody):
    clip.add_note(pitch, start=i, length=0.9, velocity=90)

lead.add_effect("reverbsc", params={"size": 0.7})

out = os.path.join(os.path.expanduser("~"), "lmms_hello_melody.wav")
stats = song.render(out)
print("Rendered %.1f s to %s (peak %.1f dBFS, RMS %.1f dBFS)"
      % (stats["seconds"], stats["path"], stats["peak_db"], stats["rms_db"]))
