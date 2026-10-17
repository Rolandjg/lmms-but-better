"""Generative music: a seeded random walk over a scale with a matching bass, 16 bars long.
Change SEED or MODE and run again."""
import random
import lmms

SEED = 7
MODE = "dorian"
ROOT = "D4"

random.seed(SEED)
song = lmms.song
song.new(empty=True)
song.tempo = 100

notes = lmms.scale(ROOT, MODE, octaves=2)
lead = song.add_instrument_track("organic", name="Walker")
clip = lead.add_midi_clip(0, length=lmms.bars(16))

position, index = 0.0, len(notes) // 2
melody = []
while position < lmms.bars(16):
    length = random.choice([0.5, 0.5, 1.0, 1.5])
    index = max(0, min(len(notes) - 1, index + random.choice([-2, -1, -1, 1, 1, 2])))
    if random.random() > 0.15:                       # leave some space
        melody.append((notes[index], position, length * 0.9, random.randint(70, 110)))
    position += length
clip.add_notes(melody)                               # one call for all notes

bass = song.add_instrument_track("tripleoscillator", name="Drone")
drone = bass.add_midi_clip(0, length=lmms.bars(16))
for bar in range(16):
    drone.add_note(lmms.note(ROOT) - 24 + (7 if bar % 4 == 3 else 0), start=lmms.bars(bar), length=lmms.bars(1) - 0.1, velocity=70)

print("%d melody notes in %s %s" % (len(clip.notes), ROOT, MODE))
