"""Discover what is installed and which knobs each plugin has - useful before writing a song
(or for an AI agent deciding what to use)."""
import lmms

lmms.song.new(empty=True)

print("Instruments:")
for p in lmms.instruments():
    print("  %-20s %s" % (p["name"], p["description"]))

print("\nEffects:")
for p in lmms.effects():
    print("  %-20s %s" % (p["name"], p["description"]))

# Parameters of an instrument: keys are what params[...] / set(...) accept
probe = lmms.song.add_instrument_track("lb302", name="probe")
print("\nlb302 parameters:")
print(probe.instrument.params)

# Effects work the same way
fx = probe.add_effect("bitcrush")
print("\nbitcrush parameters:")
print(fx.params)

# VST3 / LADSPA / LV2 plugins are "sub-plugins" of a wrapper; list their names with:
#   lmms.instruments(sub_plugins=True)  /  lmms.effects(sub_plugins=True)
probe.delete()
