"""Drums in the pattern editor: a kick/snare/hat beat placed four times in the song.

Pattern-editor ("beat") tracks hold one clip per pattern; a pattern track in the song editor plays it.
Create the pattern track first - adding a track to an empty pattern editor would create one for you.
"""
import lmms

song = lmms.song
song.new(empty=True)
song.tempo = 96

beat = song.add_pattern_track("Beat")
pattern = beat.pattern_index

def drum(name, sample, steps, velocity=100):
    track = lmms.patterns.add_instrument_track("audiofileprocessor", name=name)
    track.instrument.load_file(lmms.factory_sample("drums/" + sample))
    track.set_steps(steps, pattern=pattern, velocity=velocity)
    return track

drum("Kick", "kick01.ogg", [0, 7, 8, 10])
drum("Snare", "snare01.ogg", [4, 12])
drum("Hat", "hihat_closed01.ogg", range(0, 16, 2), velocity=60)

for bar in range(4):
    beat.add_clip(start=lmms.bars(bar), length=lmms.bars(1))

print("Pattern tracks:", lmms.patterns.tracks)
print("Song length: %d bars" % song.length_bars)
