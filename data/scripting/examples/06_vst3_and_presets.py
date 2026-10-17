"""VST3 plugins and presets.

VST3 instruments/effects are loaded through the "vst3instrument"/"vst3effect" wrappers with the
plugin's name as key. Their parameters show up like any other plugin's (normalized 0..1).
"""
import lmms

lmms.song.new(empty=True)

vst3 = [k["name"] for p in lmms.instruments(sub_plugins=True) if p["name"] == "vst3instrument"
        for k in p.get("sub_plugins", []) if k["attributes"].get("uid")]
print("VST3 instruments found:", vst3 or "none")

if vst3:
    synth = lmms.song.add_instrument_track("vst3instrument", key=vst3[0], name=vst3[0])
    params = synth.instrument.params
    print("%s has %d parameters, e.g.:" % (vst3[0], len(params)))
    for p in params.list()[:10]:
        print("  ", p)
    # whole plugin state (including the plugin's own chunk) can be stored and restored:
    state = synth.instrument.get_state()
    print("state is %d characters of XML" % len(state))

# LMMS presets (.xpf) load into any instrument track
import os, glob
presets = sorted(glob.glob(os.path.join(lmms.paths()["factory_presets"], "*", "*.xpf")))
if presets:
    track = lmms.song.add_instrument_track(preset=presets[0])
    print("Loaded preset", os.path.basename(presets[0]), "->", track, track.instrument)
