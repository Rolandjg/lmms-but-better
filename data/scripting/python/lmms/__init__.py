"""Control LMMS from Python - like Blender's ``bpy``, for making music with code or AI agents.

Quick start (inside LMMS: Edit > Script Console, or ``lmms --script my_song.py``)::

    import lmms

    song = lmms.song
    song.new(empty=True)
    song.tempo = 124

    bass = song.add_instrument_track("lb302", name="Bass")
    clip = bass.add_midi_clip(start=0, length=16)
    clip.add_notes([("C2", i, 0.5) for i in range(16)])

    bass.add_effect("bitcrush", params={"rate": 22000})
    print(song.render("~/bass.wav"))          # peak/RMS statistics of the result

Units used everywhere:
    * positions and lengths are in beats (quarter notes; 1 beat = 48 LMMS ticks).
      ``lmms.bars(n)`` converts bars to beats for the current time signature.
    * pitches are MIDI note numbers (60 = C4) or names like "C4", "F#2", "Bb3".
    * note velocity is LMMS' 0-200 scale (100 = normal), pan is -100..100.
    * track volume is 0-200 (%), track panning -100..100, mixer volume 0-2 (1 = unity).

Outside LMMS (any Python process, e.g. an agent's tool):
    ``lmms.connect()`` attaches to a running LMMS started with ``--script-server``
    (or whose Script Console has "Agent link" enabled); ``lmms.launch()`` starts a
    new headless LMMS and connects to it.

Call ``lmms.help()`` for an overview, ``help(lmms.Track)`` etc. for details, and see
doc/SCRIPTING.md in the LMMS sources for the full reference.
"""

import atexit
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from collections import namedtuple

__all__ = [
    "song", "mixer", "patterns", "connect", "disconnect", "launch", "call", "batch",
    "plugins", "instruments", "effects", "note", "note_name", "chord", "scale", "bars",
    "analyze", "paths", "factory_sample", "screenshot", "show", "message", "quit", "help", "api",
    "LmmsError", "Note", "Song", "Track", "InstrumentTrack", "SampleTrack", "AutomationTrack",
    "PatternTrack", "Clip", "MidiClip", "SampleClip", "AutomationClip", "PatternClip",
    "Plugin", "Instrument", "Effect", "Param", "ParamSet", "Mixer", "MixerChannel", "Patterns",
]

TICKS_PER_BEAT = 48


class LmmsError(Exception):
    """Raised when LMMS rejects a command (the message says why)."""


# --------------------------------------------------------------------------- connection

class _Connection:
    def __init__(self, host, port, token, timeout=None):
        self.sock = socket.create_connection((host, port), timeout=10)
        self.sock.settimeout(timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.reader = self.sock.makefile("rb")
        self.lock = threading.Lock()
        self.next_id = 1
        self._send({"token": token})
        hello = self._receive()
        if not hello.get("ok"):
            raise LmmsError(hello.get("error", {}).get("message", "authentication failed"))
        self.version = hello.get("version")

    def _send(self, message):
        self.sock.sendall(json.dumps(message, separators=(",", ":")).encode("utf-8") + b"\n")

    def _receive(self):
        line = self.reader.readline()
        if not line:
            raise LmmsError("LMMS closed the connection")
        return json.loads(line)

    def call(self, method, params):
        with self.lock:
            request_id = self.next_id
            self.next_id += 1
            self._send({"id": request_id, "method": method, "params": params})
            while True:
                reply = self._receive()
                if reply.get("id") == request_id:
                    break
        if "error" in reply:
            raise LmmsError(reply["error"].get("message", "unknown error"))
        return reply.get("result")

    def close(self):
        try:
            self.reader.close()
            self.sock.close()
        except OSError:
            pass


_connection = None
_launched = None


def _connection_file():
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    candidates = []
    if runtime:
        candidates.append(os.path.join(runtime, "lmms-script-server.json"))
    if sys.platform == "darwin":
        candidates.append(os.path.expanduser("~/Library/Application Support/lmms-script-server.json"))
    if sys.platform == "win32":
        local = os.environ.get("LOCALAPPDATA", "")
        candidates += [os.path.join(local, "lmms-script-server.json"),
                       os.path.join(local, "LMMS", "lmms-script-server.json")]
    candidates.append(os.path.join(tempfile.gettempdir(), "lmms-script-server.json"))
    if hasattr(os, "getuid"):
        candidates.append("/run/user/%d/lmms-script-server.json" % os.getuid())
        candidates.append(os.path.join(tempfile.gettempdir(), "runtime-%s" % os.environ.get("USER", ""),
                                       "lmms-script-server.json"))
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


def connect(host=None, port=None, token=None, timeout=None):
    """Connect to a running LMMS.

    With no arguments this uses, in order: the environment of a script started by LMMS
    (``LMMS_SCRIPT_PORT``/``LMMS_SCRIPT_TOKEN``), then the connection file that LMMS writes
    while its script server runs (``$XDG_RUNTIME_DIR/lmms-script-server.json``).
    Start a server with ``lmms --script-server`` (add ``--headless`` for no GUI), or use
    :func:`launch`. Returns the LMMS version string.
    """
    global _connection
    disconnect()
    if port is None and os.environ.get("LMMS_SCRIPT_PORT"):
        host = host or os.environ.get("LMMS_SCRIPT_HOST", "127.0.0.1")
        port = int(os.environ["LMMS_SCRIPT_PORT"])
        token = token or os.environ.get("LMMS_SCRIPT_TOKEN")
    if port is None:
        path = _connection_file()
        if not path:
            raise LmmsError("no running LMMS script server found. Start LMMS with "
                            "'lmms --script-server' (or '--headless --script-server'), "
                            "or call lmms.launch()")
        with open(path, encoding="utf-8") as f:
            info = json.load(f)
        host, port, token = info["host"], info["port"], info["token"]
    if token is None:
        raise LmmsError("a token is required (see the LMMS script server output)")
    try:
        _connection = _Connection(host or "127.0.0.1", int(port), token, timeout)
    except OSError as e:
        raise LmmsError("cannot connect to LMMS at %s:%s (%s) - is it still running?" % (host, port, e))
    return _connection.version


def disconnect():
    """Close the connection to LMMS (it keeps running)."""
    global _connection
    if _connection is not None:
        _connection.close()
        _connection = None


def _find_lmms_binary():
    if os.environ.get("LMMS_BINARY"):
        return os.environ["LMMS_BINARY"]
    here = os.path.dirname(os.path.abspath(__file__))
    # installed: <prefix>/share/lmms/scripting/python/lmms -> <prefix>/bin/lmms
    # sources:   <src>/data/scripting/python/lmms        -> <src>/build/lmms
    root = os.path.abspath(os.path.join(here, "..", "..", "..", ".."))
    for candidate in (os.path.join(root, "..", "bin", "lmms"), os.path.join(root, "build", "lmms"),
                      os.path.join(root, "..", "lmms.exe"), os.path.join(root, "lmms.exe")):
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return os.path.abspath(candidate)
    found = shutil.which("lmms")
    if found:
        return found
    raise LmmsError("cannot find the lmms executable: set LMMS_BINARY or put lmms on PATH")


def launch(headless=True, project=None, binary=None, port=None, extra_args=(), timeout=120, verbose=False):
    """Start a new LMMS process with a script server and connect to it.

    ``headless=True`` runs without any window (rendering, saving and everything else still
    works; screenshots don't). The process is stopped when Python exits, or with
    ``lmms.quit()``. Returns the ``subprocess.Popen`` object.
    """
    global _launched
    args = [binary or _find_lmms_binary()]
    if headless:
        args.append("--headless")
    args.append("--script-server")
    if port:
        args += ["--script-port", str(port)]
    args += list(extra_args)
    if project:
        args.append(os.path.abspath(os.path.expanduser(project)))
    env = dict(os.environ)
    for key in ("LMMS_SCRIPT_HOST", "LMMS_SCRIPT_PORT", "LMMS_SCRIPT_TOKEN"):
        env.pop(key, None)
    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=None if verbose else subprocess.DEVNULL,
                               stdin=subprocess.DEVNULL, env=env)
    info = {}
    found = threading.Event()

    def pump():
        for raw in process.stdout:
            line = raw.decode("utf-8", "replace")
            if line.startswith("LMMS_SCRIPT_SERVER ") and not found.is_set():
                info.update(json.loads(line[len("LMMS_SCRIPT_SERVER "):]))
                found.set()
            elif verbose:
                sys.stdout.write("[lmms] " + line)
        found.set()

    threading.Thread(target=pump, daemon=True).start()
    if not found.wait(timeout) or not info:
        process.kill()
        raise LmmsError("LMMS did not start its script server (exit code %s)" % process.poll())
    connect(info["host"], info["port"], info["token"])
    _launched = process
    atexit.register(_shutdown_launched)
    return process


def _shutdown_launched():
    global _launched
    if _launched is not None and _launched.poll() is None:
        try:
            call("app.quit")
        except Exception:
            pass
        try:
            _launched.wait(10)
        except subprocess.TimeoutExpired:
            _launched.kill()
    _launched = None


def call(method, **params):
    """Send a raw command to LMMS and return its result (see ``lmms.api()`` for the list)."""
    if _connection is None:
        connect()
    return _connection.call(method, params)


def batch(calls):
    """Run several raw calls in one round trip: ``batch([("song.info", {}), ...])``.

    Returns a list with the result of each call. LMMS stops at the first failing call; the error says
    which one failed and how many before it were applied (use ``song.checkpoint()`` first to be able
    to undo them)."""
    calls = list(calls)
    results = call("api.batch", calls=[{"method": m, "params": p} for m, p in calls], stop_on_error=True)
    out = []
    for i, r in enumerate(results):
        if "error" in r:
            raise LmmsError("batch call %d of %d (%s) failed: %s; the %d call(s) before it were applied, "
                            "the rest were not run" % (i + 1, len(calls), calls[i][0], r["error"], i))
        out.append(r.get("result"))
    return out


def api():
    """Dict of every raw command LMMS understands with a one-line description."""
    return call("api.describe")["methods"]


# --------------------------------------------------------------------------- music helpers

_NOTE_RE = re.compile(r"^\s*([A-Ga-g])([#b]?)(-?\d+)\s*$")
_NOTE_BASE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note(name):
    """MIDI number for a note name: ``note("C4") == 60``, ``note("F#2") == 42``. Numbers pass through."""
    if isinstance(name, (int, float)):
        return int(name)
    m = _NOTE_RE.match(str(name))
    if not m:
        raise ValueError("not a note name: %r (expected e.g. C4, F#2, Bb3)" % (name,))
    semis = _NOTE_BASE[m.group(1).upper()] + {"#": 1, "b": -1, "": 0}[m.group(2)]
    return (int(m.group(3)) + 1) * 12 + semis


def note_name(pitch):
    """Name of a MIDI number: ``note_name(61) == "C#4"``."""
    pitch = int(pitch)
    return "%s%d" % (_NAMES[pitch % 12], pitch // 12 - 1)


CHORDS = {
    "maj": [0, 4, 7], "": [0, 4, 7], "M": [0, 4, 7], "min": [0, 3, 7], "m": [0, 3, 7],
    "dim": [0, 3, 6], "aug": [0, 4, 8], "sus2": [0, 2, 7], "sus4": [0, 5, 7], "5": [0, 7],
    "6": [0, 4, 7, 9], "m6": [0, 3, 7, 9], "7": [0, 4, 7, 10], "maj7": [0, 4, 7, 11],
    "m7": [0, 3, 7, 10], "min7": [0, 3, 7, 10], "mmaj7": [0, 3, 7, 11], "dim7": [0, 3, 6, 9],
    "m7b5": [0, 3, 6, 10], "9": [0, 4, 7, 10, 14], "maj9": [0, 4, 7, 11, 14], "m9": [0, 3, 7, 10, 14],
    "add9": [0, 4, 7, 14], "11": [0, 4, 7, 10, 14, 17], "13": [0, 4, 7, 10, 14, 21],
}

SCALES = {
    "major": [0, 2, 4, 5, 7, 9, 11], "ionian": [0, 2, 4, 5, 7, 9, 11],
    "minor": [0, 2, 3, 5, 7, 8, 10], "aeolian": [0, 2, 3, 5, 7, 8, 10],
    "harmonic_minor": [0, 2, 3, 5, 7, 8, 11], "melodic_minor": [0, 2, 3, 5, 7, 9, 11],
    "dorian": [0, 2, 3, 5, 7, 9, 10], "phrygian": [0, 1, 3, 5, 7, 8, 10], "lydian": [0, 2, 4, 6, 7, 9, 11],
    "mixolydian": [0, 2, 4, 5, 7, 9, 10], "locrian": [0, 1, 3, 5, 6, 8, 10],
    "major_pentatonic": [0, 2, 4, 7, 9], "minor_pentatonic": [0, 3, 5, 7, 10],
    "blues": [0, 3, 5, 6, 7, 10], "chromatic": list(range(12)), "whole_tone": [0, 2, 4, 6, 8, 10],
}


def chord(root, quality="maj", inversion=0):
    """Pitches of a chord: ``chord("C4", "maj7") == [60, 64, 67, 71]``. Also accepts "Am7", "F#dim".

    Qualities: %s"""
    if isinstance(root, str) and quality == "maj":
        m = re.match(r"^([A-Ga-g][#b]?)(-?\d+)?(.*)$", root.strip())
        if m and m.group(3) in CHORDS:
            root, quality = m.group(1) + (m.group(2) or "4"), m.group(3)
    base = note(root)
    if quality not in CHORDS:
        raise ValueError("unknown chord quality %r (known: %s)" % (quality, ", ".join(k for k in CHORDS if k)))
    pitches = [base + i for i in CHORDS[quality]]
    for _ in range(inversion % len(pitches)):
        pitches = pitches[1:] + [pitches[0] + 12]
    return pitches


chord.__doc__ = chord.__doc__ % ", ".join(k for k in CHORDS if k)


def scale(root, mode="major", count=None, octaves=1):
    """Pitches of a scale starting at root: ``scale("A3", "minor")``; ``count`` notes, or ``octaves`` octaves.

    Modes: %s"""
    if mode not in SCALES:
        raise ValueError("unknown scale %r (known: %s)" % (mode, ", ".join(SCALES)))
    steps = SCALES[mode]
    total = count if count is not None else len(steps) * octaves + 1
    base = note(root)
    return [base + steps[i % len(steps)] + 12 * (i // len(steps)) for i in range(total)]


scale.__doc__ = scale.__doc__ % ", ".join(SCALES)


def bars(n):
    """Beats in ``n`` bars of the current time signature (4/4: ``bars(2) == 8``)."""
    return n * song.beats_per_bar


class Note(namedtuple("Note", "pitch start length velocity pan")):
    """A note of a MIDI clip. ``start`` is relative to the clip, in beats."""
    __slots__ = ()

    def __new__(cls, pitch, start=0.0, length=1.0, velocity=100, pan=0):
        return super().__new__(cls, note(pitch), start, length, velocity, pan)

    @property
    def name(self):
        return note_name(self.pitch)

    @property
    def end(self):
        return self.start + self.length

    def __repr__(self):
        return "Note(%s, start=%g, length=%g, velocity=%g, pan=%g)" % (
            self.name, self.start, self.length, self.velocity, self.pan)


def _note_payload(n):
    if isinstance(n, dict):
        return n
    if isinstance(n, Note):
        return [n.pitch, n.start, n.length, n.velocity, n.pan]
    n = list(n)
    if n and isinstance(n[0], str):
        n[0] = note(n[0])
    return n


# --------------------------------------------------------------------------- objects

class _Handle:
    """Base for everything that lives inside LMMS and is referred to by a numeric id."""
    _info_method = None

    def __init__(self, desc):
        self._desc = desc
        self.id = desc["id"]

    def info(self):
        """Fresh description of this object as a dict."""
        self._desc = call(self._info_method, id=self.id)
        return self._desc

    def _get(self, key):
        return self.info().get(key)

    def __eq__(self, other):
        return type(self) is type(other) and self.id == other.id

    def __hash__(self):
        return hash((type(self).__name__, self.id))


class Param:
    """One automatable parameter (knob, slider, switch or choice) of a track, plugin or the song.

    ``value`` reads/writes it; choice parameters also accept the option text. Pass a Param to
    ``AutomationTrack.add_automation_clip`` to automate it."""

    def __init__(self, desc):
        self._desc = desc
        self.id = desc["id"]
        self.key = desc.get("key") or ""
        self.name = desc.get("name", "")
        self.type = desc.get("type", "float")
        self.min = desc.get("min")
        self.max = desc.get("max")
        self.step = desc.get("step")
        self.default = desc.get("default")
        self.options = desc.get("options")

    def info(self):
        self._desc = call("param.get", id=self.id)
        return self._desc

    @property
    def value(self):
        return self.info()["value"]

    @value.setter
    def value(self, v):
        call("param.set", id=self.id, value=v)

    @property
    def display(self):
        """The value as LMMS shows it (with unit)."""
        return self.info().get("display")

    @property
    def text(self):
        """Current option text of a choice parameter."""
        return self.info().get("text")

    def __repr__(self):
        d = self._desc
        extra = (" options=%s" % d["options"]) if d.get("options") else (" range=%g..%g" % (d["min"], d["max"]))
        return "<Param %s (%s) = %r%s>" % (self.key, self.name, d.get("value"), extra)


class ParamSet:
    """The parameters of an owner, used like a dict: ``params["cutoff"] = 0.7``.

    Keys are the names LMMS saves the parameter under (the same ones presets use). Display
    names work too (``params["Cutoff frequency"]``). ``params.param(key)`` returns the
    :class:`Param` object (needed for automation), ``print(params)`` shows a table."""

    def __init__(self, list_method, owner_id, items=None):
        self._list_method = list_method
        self._owner = owner_id
        self._items = items

    def list(self):
        """All parameters as :class:`Param` objects."""
        if self._items is not None:
            return [Param(d) for d in self._items()]
        return [Param(d) for d in call(self._list_method, id=self._owner)]

    def keys(self):
        return [p.key for p in self.list()]

    def items(self):
        return [(p.key, p._desc["value"]) for p in self.list()]

    def to_dict(self):
        return dict(self.items())

    def param(self, key):
        for p in self.list():
            if p.key == key:
                return p
        if self._items is not None:
            raise KeyError(key)
        return Param(call("param.find", owner=self._owner, name=key))

    def __getitem__(self, key):
        return self.param(key).value

    def __setitem__(self, key, value):
        self.param(key).value = value

    def update(self, values=None, **kwargs):
        """Set several parameters at once."""
        values = dict(values or {}, **kwargs)
        for k, v in values.items():
            self[k] = v

    def __contains__(self, key):
        return key in self.keys()

    def __iter__(self):
        return iter(self.keys())

    def __len__(self):
        return len(self.list())

    def __repr__(self):
        rows = []
        for p in self.list():
            d = p._desc
            rng = "|".join(d["options"]) if d.get("options") else "%g..%g" % (d["min"], d["max"])
            rows.append("  %-24s %-28s %-10s %s" % (p.key, d["name"][:28], _fmt(d["value"]), rng))
        return "Parameters:\n  %-24s %-28s %-10s %s\n%s" % ("key", "name", "value", "range", "\n".join(rows))


def _fmt(v):
    return ("%.4g" % v) if isinstance(v, float) else str(v)


class Plugin(_Handle):
    """An instrument or effect plugin. ``params`` gives access to all of its knobs."""
    _info_method = "plugin.info"

    @property
    def name(self):
        """Plugin identifier, e.g. "tripleoscillator"."""
        return self._desc["name"]

    @property
    def display_name(self):
        return self._get("display_name")

    @property
    def key(self):
        """Sub-plugin key (for LADSPA/LV2/VST3 plugins) or None."""
        return self._desc.get("key")

    @property
    def params(self):
        return ParamSet("plugin.params", self.id)

    def set(self, **values):
        """Set parameters by key: ``synth.set(vol=80, cutoff=0.4)``."""
        return call("plugin.set_params", id=self.id, values=values)

    def get_state(self):
        """Complete settings as an XML string (what a project file stores for this plugin)."""
        return call("plugin.get_state", id=self.id)

    def set_state(self, xml):
        """Restore settings from :meth:`get_state` output."""
        call("plugin.set_state", id=self.id, xml=xml)

    def load_file(self, path, keep_name=True):
        """Load a file into the plugin, e.g. a sample (AudioFileProcessor) or SoundFont (Sf2Player).

        Some instruments rename their track after the file; ``keep_name=False`` allows that."""
        call("plugin.load_file", id=self.id, path=_path(path), keep_name=keep_name)

    def __repr__(self):
        return "<%s %s (%s)>" % (type(self).__name__, self._desc.get("name"), self._desc.get("display_name"))


class Instrument(Plugin):
    """The instrument plugin of an :class:`InstrumentTrack`."""

    @property
    def track(self):
        return _wrap(call("track.info", id=self._desc["track"]))


class Effect(Plugin):
    """An effect in a track's or mixer channel's effect chain."""

    @property
    def enabled(self):
        return self._get("enabled")

    @enabled.setter
    def enabled(self, value):
        call("effect.set", id=self.id, enabled=bool(value))

    @property
    def wet(self):
        """Wet/dry mix, 0..1."""
        return self._get("wet")

    @wet.setter
    def wet(self, value):
        call("effect.set", id=self.id, wet=value)

    def move(self, by):
        """Move up (negative) or down (positive) in the chain."""
        call("effect.move", id=self.id, by=by)

    def delete(self):
        call("effect.delete", id=self.id)


# --------------------------------------------------------------------------- clips

class Clip(_Handle):
    """A clip on a track. ``start``/``length`` are in beats."""
    _info_method = "clip.info"

    type = property(lambda self: self._desc["type"])

    def _set(self, **kw):
        self._desc = call("clip.set", id=self.id, **kw)

    name = property(lambda self: self._get("name"), lambda self, v: self._set(name=v))
    start = property(lambda self: self._get("start"), lambda self, v: self._set(start=v),
                     doc="Position on the timeline in beats")
    length = property(lambda self: self._get("length"), lambda self, v: self._set(length=v))
    muted = property(lambda self: self._get("muted"), lambda self, v: self._set(muted=v))
    color = property(lambda self: self._get("color"), lambda self, v: self._set(color=v),
                     doc="'#rrggbb' or None")

    @property
    def end(self):
        return self._get("end")

    @property
    def track(self):
        return _wrap(call("track.info", id=self._desc["track"]))

    def move(self, start):
        self.start = start
        return self

    def copy(self, start, track=None):
        """Copy this clip to ``start`` (beats), optionally onto another track of the same type."""
        params = {"id": self.id, "start": start}
        if track is not None:
            params["track"] = track.id
        return _wrap(call("clip.copy", **params))

    def delete(self):
        call("clip.delete", id=self.id)

    def __repr__(self):
        d = self._desc
        return "<%s %r start=%g length=%g>" % (type(self).__name__, d.get("name") or "", d["start"], d["length"])


class MidiClip(Clip):
    """A clip with notes (instrument tracks). Note positions are relative to the clip start."""

    @property
    def notes(self):
        """List of :class:`Note` (pitch, start, length, velocity, pan)."""
        return [Note(n["pitch"], n["start"], n["length"], n["velocity"], n["pan"])
                for n in call("clip.notes", id=self.id)]

    @notes.setter
    def notes(self, notes):
        self.set_notes(notes)

    def add_note(self, pitch, start=0.0, length=1.0, velocity=100, pan=0):
        """Add one note. ``pitch`` is a MIDI number or a name like "C4"."""
        self._desc = call("clip.add_notes", id=self.id, notes=[[note(pitch), start, length, velocity, pan]])
        return self

    def add_notes(self, notes):
        """Add many notes at once: tuples ``(pitch, start, length[, velocity[, pan]])``,
        :class:`Note` objects or dicts."""
        self._desc = call("clip.add_notes", id=self.id, notes=[_note_payload(n) for n in notes])
        return self

    def add_chord(self, pitches, start=0.0, length=1.0, velocity=100):
        """Add several pitches at the same time (e.g. ``lmms.chord("Am7")``)."""
        return self.add_notes([(p, start, length, velocity) for p in pitches])

    def set_notes(self, notes):
        """Replace all notes."""
        self._desc = call("clip.set_notes", id=self.id, notes=[_note_payload(n) for n in notes])
        return self

    def clear(self):
        """Remove all notes."""
        self._desc = call("clip.clear_notes", id=self.id)
        return self

    def remove_notes(self, which):
        """Remove notes by index list, or by predicate: ``clip.remove_notes(lambda n: n.pitch > 72)``."""
        if callable(which):
            which = [i for i, n in enumerate(self.notes) if which(n)]
        self._desc = call("clip.remove_notes", id=self.id, indices=list(which))
        return self

    def transpose(self, semitones):
        """Shift every note by ``semitones``."""
        return self.set_notes([n._replace(pitch=n.pitch + semitones) for n in self.notes])

    def quantize(self, grid=0.25):
        """Snap note starts to a grid in beats (0.25 = 16th notes)."""
        return self.set_notes([n._replace(start=round(n.start / grid) * grid) for n in self.notes])

    def set_steps(self, steps, pitch=None, velocity=100, total=None):
        """Pattern-editor clips: switch on the given 16th steps, e.g. ``[0, 4, 8, 12]``."""
        params = {"id": self.id, "on": list(steps), "velocity": velocity}
        if pitch is not None:
            params["pitch"] = note(pitch)
        if total is not None:
            params["steps"] = total
        self._desc = call("clip.set_steps", **params)
        return self

    def open(self):
        """Show this clip in the piano roll (GUI only)."""
        call("clip.open", id=self.id)


class SampleClip(Clip):
    """An audio clip on a sample track."""
    file = property(lambda self: self._get("file"), lambda self, v: self._set(file=_path(v)))
    gain = property(lambda self: self._get("gain"), lambda self, v: self._set(gain=v), doc="Linear gain (1 = unity)")
    reversed = property(lambda self: self._get("reversed"), lambda self, v: self._set(reversed=v))
    fade_in = property(lambda self: self._get("fade_in"), lambda self, v: self._set(fade_in=v), doc="Beats")
    fade_out = property(lambda self: self._get("fade_out"), lambda self, v: self._set(fade_out=v), doc="Beats")

    @property
    def sample_length(self):
        """Length of the audio file in beats at the current tempo."""
        return self._get("sample_length")


_CURVE_PRESETS = {
    "up": [(0, 0), (1, 1)],
    "down": [(0, 1), (1, 0)],
    "hill": [(0, 0), (0.5, 1), (1, 0)],
    "dip": [(0, 1), (0.5, 0), (1, 1)],
    "flat": [],
}


def _curve(graph):
    """A graph for AutomationClip.generate: a preset name or a list of (x, y) pairs."""
    if isinstance(graph, str):
        if graph not in _CURVE_PRESETS:
            raise ValueError("graph must be a list of (x, y) or one of: " + ", ".join(_CURVE_PRESETS))
        graph = _CURVE_PRESETS[graph]
    return [list(p) for p in graph]


class AutomationClip(Clip):
    """A curve that drives one or more parameters over time."""

    @property
    def points(self):
        """List of ``(beat, value)`` pairs (beats relative to the clip start)."""
        return [tuple(p) for p in call("clip.automation", id=self.id)["points"]]

    @points.setter
    def points(self, points):
        self.set_points(points)

    def set_points(self, points, progression=None, replace=True):
        """Set the curve: ``[(0, 0.0), (8, 1.0)]``. ``progression`` is discrete, linear or cubic."""
        params = {"id": self.id, "points": [list(p) for p in points], "replace": replace}
        if progression:
            params["progression"] = progression
        self._desc = call("clip.set_automation", **params)
        return self

    def add_point(self, beat, value):
        return self.set_points([(beat, value)], replace=False)

    def generate(self, shape="sine", cycles=4, *, expression=None, amplitude=None, frequency=None,
                 low=None, high=None, progression=None, **options):
        """Replace the clip's curve with a generated wave (the automation editor's function generator).

        ``shape``: ``sine``, ``triangle``, ``saw_up``, ``saw_down``, ``square``, ``noise`` or ``smooth_noise``.
        ``cycles`` is the total number of cycles over the clip, whatever its length.

        ``amplitude`` and ``frequency`` shape the wave over the length of the clip. Each is a graph
        ``[(x, y), ...]`` with x = 0 at the start and 1 at the end, or one of ``"up"``, ``"down"``,
        ``"hill"``, ``"dip"``. Amplitude y is the swing (1 = full, 0 = flat); frequency y is the speed
        (0.5 = unchanged, 1 = ``frequency_range`` octaves faster, 0 = that much slower)::

            clip.generate("sine", cycles=8, amplitude="up")                       # fade in
            clip.generate("saw_down", cycles=6, frequency="down")                 # slowing down
            clip.generate("sine", cycles=5, amplitude=[(0, 0), (.5, 1), (1, 0)])  # swell and fade

        ``expression`` replaces the shape with a formula in ``p`` (position in cycles), ``t`` (0 to 1
        over the clip) and ``n`` (cycle number), returning -1 to 1::

            clip.generate(expression="sin(tau * p) * (1 - t)", cycles=6)

        Other options: ``phase`` (0-1), ``symmetry``, ``bend`` (-1 to 1), ``steps`` (quantise to this
        many levels), ``cells`` and ``seed`` (noise), ``detail`` (nodes per cycle), ``invert``,
        ``frequency_range``. ``low`` / ``high`` default to the parameter's whole range."""
        params = {"id": self.id, "cycles": cycles}
        if expression is not None:
            params["expression"] = expression
        elif shape is not None:
            params["shape"] = shape
        for key, graph in (("amplitude", amplitude), ("frequency", frequency)):
            if graph is not None:
                params[key] = _curve(graph)
        for key, value in (("low", low), ("high", high), ("progression", progression)):
            if value is not None:
                params[key] = value
        params.update(options)
        self._desc = call("clip.generate", **params)
        return self

    progression = property(lambda self: self._get("progression"),
                           lambda self, v: call("clip.set_automation", id=self.id, progression=v))

    @property
    def targets(self):
        """Parameters this clip controls."""
        return [Param(call("param.get", id=t)) for t in self._get("targets")]

    def add_target(self, param):
        call("clip.set_automation", id=self.id, target=_param_id(param))
        return self

    def open(self):
        """Show this clip in the automation editor (GUI only)."""
        call("clip.open", id=self.id)


class PatternClip(Clip):
    """A clip on a pattern track: plays that track's pattern from the pattern editor."""


# --------------------------------------------------------------------------- tracks

class Track(_Handle):
    """Base class for all tracks. Use ``song.tracks`` / ``song.track(name)`` to get them."""
    _info_method = "track.info"

    type = property(lambda self: self._desc["type"], doc="instrument, sample, automation or pattern")

    def _set(self, **kw):
        self._desc = call("track.set", id=self.id, **kw)

    name = property(lambda self: self._get("name"), lambda self, v: self._set(name=v))
    muted = property(lambda self: self._get("muted"), lambda self, v: self._set(muted=v))
    solo = property(lambda self: self._get("solo"), lambda self, v: self._set(solo=v))
    color = property(lambda self: self._get("color"), lambda self, v: self._set(color=v), doc="'#rrggbb' or None")

    @property
    def index(self):
        return self._get("index")

    @property
    def container(self):
        """"song" or "pattern" (the pattern editor)."""
        return self._desc["container"]

    @property
    def clips(self):
        return [_wrap(c) for c in call("track.clips", id=self.id)]

    @property
    def params(self):
        """The track's own parameters (volume, panning, envelopes, arpeggio, ...)."""
        return ParamSet("track.params", self.id)

    def delete(self):
        call("track.delete", id=self.id)

    def clone(self):
        """Duplicate the track including its clips."""
        return _wrap(call("track.clone", id=self.id))

    def move(self, index):
        self._desc = call("track.move", id=self.id, index=index)
        return self

    def add_clip(self, start=0.0, length=None, name=None, **kw):
        params = dict(id=self.id, start=start, **kw)
        if length is not None:
            params["length"] = length
        if name is not None:
            params["name"] = name
        return _wrap(call("track.add_clip", **params))

    def __repr__(self):
        return "<%s %r>" % (type(self).__name__, self._desc.get("name"))


class _AudioTrackMixin:
    volume = property(lambda self: self._get("volume"), lambda self, v: self._set(volume=v), doc="0..200 (%)")
    panning = property(lambda self: self._get("panning"), lambda self, v: self._set(panning=v), doc="-100..100")
    mixer_channel = property(lambda self: self._get("mixer_channel"),
                             lambda self, v: self._set(mixer_channel=_channel_index(v)),
                             doc="Mixer channel index (0 = master)")

    @property
    def effects(self):
        return [_wrap(e) for e in call("track.effects", id=self.id)]

    def add_effect(self, plugin, key=None, params=None):
        """Append an effect, e.g. ``track.add_effect("reverbsc")``. ``key`` selects a
        LADSPA/LV2/VST3 sub-plugin by name. ``params`` sets knobs right away."""
        args = {"id": self.id, "plugin": plugin}
        if key is not None:
            args["key"] = key
        if params:
            args["params"] = params
        return _wrap(call("track.add_effect", **args))


class InstrumentTrack(_AudioTrackMixin, Track):
    """A track that plays notes through an instrument plugin."""
    pitch = property(lambda self: self._get("pitch"), lambda self, v: self._set(pitch=v), doc="Cents")

    @property
    def instrument(self):
        d = call("track.instrument", id=self.id)
        return _wrap(d) if d else None

    def set_instrument(self, plugin, key=None, params=None, keep_name=False):
        """Replace the instrument plugin (``plugin`` as in ``lmms.instruments()``)."""
        args = {"id": self.id, "plugin": plugin, "keep_name": keep_name}
        if key is not None:
            args["key"] = key
        if params:
            args["params"] = params
        return _wrap(call("track.set_instrument", **args))

    def load_preset(self, path):
        """Load an instrument preset (.xpf)."""
        self._desc = call("track.load_preset", id=self.id, path=_path(path))
        return self

    def save_preset(self, path):
        return call("track.save_preset", id=self.id, path=_path(path))

    def add_midi_clip(self, start=0.0, length=None, name=None, notes=None):
        """Add a MIDI clip at ``start`` beats. Without ``length`` it grows to fit its notes."""
        kw = {}
        if notes is not None:
            kw["notes"] = [_note_payload(n) for n in notes]
        return self.add_clip(start, length, name, **kw)

    def pattern(self, index=0):
        """For tracks in the pattern editor: the clip holding pattern ``index``."""
        return _wrap(call("track.pattern_clip", id=self.id, pattern=index))

    def set_steps(self, steps, pattern=0, pitch=None, velocity=100):
        """For tracks in the pattern editor: set the drum steps of a pattern."""
        return self.pattern(pattern).set_steps(steps, pitch=pitch, velocity=velocity)

    def note_on(self, pitch, velocity=100):
        """Start a note live (like pressing a key). Velocity is MIDI 1..127."""
        call("track.note_on", id=self.id, pitch=note(pitch), velocity=velocity)

    def note_off(self, pitch):
        call("track.note_off", id=self.id, pitch=note(pitch))

    def play_note(self, pitch, seconds=0.5, velocity=100):
        """Play a note live for ``seconds`` (blocks meanwhile)."""
        self.note_on(pitch, velocity)
        time.sleep(seconds)
        self.note_off(pitch)


class SampleTrack(_AudioTrackMixin, Track):
    """A track that plays audio files."""

    def add_sample_clip(self, file, start=0.0, length=None, name=None):
        """Place an audio file at ``start`` beats (length defaults to the file's length)."""
        return self.add_clip(start, length, name, file=_path(file))


class AutomationTrack(Track):
    """A track holding automation clips."""

    def add_automation_clip(self, param, start=0.0, length=None, points=None, progression=None, name=None,
                            generate=None):
        """Automate ``param`` (a :class:`Param`, e.g. ``synth.params.param("cutoff")``).

        ``points`` are ``(beat, value)`` pairs relative to the clip start. ``generate`` is a dict of
        :meth:`AutomationClip.generate` arguments to fill the clip with a wave instead::

            track.add_automation_clip(cutoff, 0, 16, generate=dict(shape="sine", cycles=8, amplitude="up", low=0.2, high=0.9))"""
        kw = {"target": _param_id(param)}
        if points:
            kw["points"] = [list(p) for p in points]
        clip = self.add_clip(start, length, name, **kw)
        if progression:
            clip.progression = progression
        if generate:
            clip.generate(**generate)
        return clip


class PatternTrack(Track):
    """A song-editor track that plays a pattern from the pattern editor ("beat/bassline")."""

    @property
    def pattern_index(self):
        return self._desc["pattern_index"]

    def add_clip(self, start=0.0, length=None, name=None, **kw):
        """Play this pattern at ``start`` beats for ``length`` beats (default: one bar)."""
        return super().add_clip(start, length if length is not None else bars(1), name, **kw)


_TRACK_CLASSES = {"instrument": InstrumentTrack, "sample": SampleTrack, "automation": AutomationTrack,
                  "pattern": PatternTrack}
_CLIP_CLASSES = {"midi": MidiClip, "sample": SampleClip, "automation": AutomationClip, "pattern": PatternClip}


def _wrap(desc):
    kind = desc.get("kind")
    if kind == "track":
        return _TRACK_CLASSES.get(desc["type"], Track)(desc)
    if kind == "clip":
        return _CLIP_CLASSES.get(desc["type"], Clip)(desc)
    if kind == "instrument":
        return Instrument(desc)
    if kind == "effect":
        return Effect(desc)
    if kind == "param":
        return Param(desc)
    return desc


def _path(p):
    return os.path.abspath(os.path.expanduser(str(p)))


def _param_id(param):
    if isinstance(param, Param):
        return param.id
    if isinstance(param, int):
        return param
    raise TypeError("expected a Param (e.g. track.params.param('vol')), got %r" % (param,))


def _channel_index(ch):
    return ch.index if isinstance(ch, MixerChannel) else int(ch)


# --------------------------------------------------------------------------- song, mixer, patterns

class Song:
    """The current project (``lmms.song``)."""

    def info(self):
        """Everything about the song in one dict."""
        return call("song.info")

    def _set(self, **kw):
        return call("song.set", **kw)

    tempo = property(lambda self: self.info()["tempo"], lambda self, v: self._set(tempo=v), doc="BPM")
    master_volume = property(lambda self: self.info()["master_volume"], lambda self, v: self._set(master_volume=v),
                             doc="0..200 (%)")
    master_pitch = property(lambda self: self.info()["master_pitch"], lambda self, v: self._set(master_pitch=v),
                            doc="Semitones, -12..12")

    @property
    def time_signature(self):
        return tuple(self.info()["time_signature"])

    @time_signature.setter
    def time_signature(self, value):
        self._set(time_signature=list(value))

    @property
    def beats_per_bar(self):
        """Quarter-note beats per bar (4 in 4/4, 3 in 6/8)."""
        return self.info()["beats_per_bar"]

    @property
    def length(self):
        """Length of the song in beats."""
        return self.info()["length_beats"]

    @property
    def length_bars(self):
        return self.info()["length_bars"]

    @property
    def playing(self):
        return self.info()["playing"]

    @property
    def position(self):
        """Play-head position in beats."""
        return self.info()["position"]

    @position.setter
    def position(self, beat):
        call("song.set_position", beat=beat)

    @property
    def file(self):
        return self.info()["file"]

    @property
    def loop(self):
        """``{"enabled", "start", "end"}`` of the song loop."""
        return self.info()["loop"]

    def set_loop(self, start=None, end=None, enabled=True):
        params = {"enabled": enabled}
        if start is not None:
            params["start"] = start
        if end is not None:
            params["end"] = end
        call("song.set_loop", **params)

    @property
    def params(self):
        """Automatable song parameters (tempo, master_volume, master_pitch, ...)."""
        return ParamSet(None, None, items=lambda: call("song.params"))

    # project files
    def new(self, empty=False, template=None):
        """Start a new project: from the user's default template, from ``template`` (a .mpt/.mmp
        file), or with ``empty=True`` completely empty (no tracks, 140 BPM, 4/4) - best for scripts."""
        if template:
            call("song.new", template=_path(template))
        else:
            call("song.new", empty=empty)

    def open(self, path):
        call("song.open", path=_path(path))

    def save(self, path=None):
        """Save (as .mmpz unless the name ends in .mmp). Returns the path."""
        return call("song.save", **({"path": _path(path)} if path else {}))

    def checkpoint(self):
        """Remember the project as it is now (the project file is not touched). Returns an id for
        ``restore_checkpoint``. The Script Console takes one before every run ("Undo run")."""
        return call("song.checkpoint")

    def restore_checkpoint(self, id=None):
        """Put the project back to a checkpoint (default: the latest). Track/clip/plugin objects from
        before the restore are no longer valid; look them up again."""
        call("song.restore_checkpoint", **({"id": id} if id is not None else {}))

    def import_midi(self, path):
        """Import a MIDI file as new tracks."""
        call("song.import", path=_path(path))

    def export_midi(self, path):
        return call("song.export_midi", path=_path(path))

    def render(self, path, format=None, sample_rate=44100, bit_depth=16, loop=False,
               between_loop_markers=False, loop_count=1, stems=False):
        """Render to an audio file (wav/flac/ogg/mp3 by extension or ``format``) and wait.

        Returns a dict with ``path``, ``seconds``, ``peak_db``, ``rms_db``, ``clipped_samples`` and
        ``silent`` - handy for checking a mix without listening.
        ``stems=True`` renders every track into the directory ``path``."""
        params = dict(path=_path(path), sample_rate=sample_rate, bit_depth=bit_depth, loop=loop,
                      between_loop_markers=between_loop_markers, loop_count=loop_count, stems=stems)
        if format:
            params["format"] = format
        return call("song.render", **params)

    # transport
    def play(self, start=None):
        """Start playback (from ``start`` beats if given)."""
        call("song.play", **({"from": start} if start is not None else {}))

    def stop(self):
        call("song.stop")

    def pause(self):
        call("song.pause")

    # tracks
    @property
    def tracks(self):
        """All tracks of the song editor, top to bottom."""
        return [_wrap(t) for t in call("song.tracks", container="song")]

    def track(self, name_or_index):
        """Find a track by name (or index)."""
        tracks = self.tracks
        if isinstance(name_or_index, int):
            return tracks[name_or_index]
        for t in tracks:
            if t._desc["name"] == name_or_index:
                return t
        raise KeyError("no track named %r (tracks: %s)" % (name_or_index, ", ".join(t._desc["name"] for t in tracks)))

    def add_track(self, type="instrument", name=None, container="song", **kw):
        params = dict(type=type, container=container, **kw)
        if name is not None:
            params["name"] = name
        return _wrap(call("song.add_track", **params))

    def add_instrument_track(self, instrument="tripleoscillator", name=None, key=None, preset=None,
                             params=None, color=None):
        """Add an instrument track. ``instrument`` is a plugin name (see ``lmms.instruments()``),
        ``key`` picks a sub-plugin (e.g. a VST3 by name for "vst3instrument"), ``preset`` loads a
        .xpf file instead, ``params`` sets instrument knobs."""
        kw = {"instrument": instrument}
        if key is not None:
            kw["key"] = key
        if preset is not None:
            kw["preset"] = _path(preset)
        if params:
            kw["instrument_params"] = params
        if color:
            kw["color"] = color
        return self.add_track("instrument", name, **kw)

    def add_sample_track(self, name=None):
        return self.add_track("sample", name)

    def add_automation_track(self, name=None):
        return self.add_track("automation", name)

    def add_pattern_track(self, name=None):
        """Add a pattern track (and a new pattern in the pattern editor)."""
        return self.add_track("pattern", name)

    def __repr__(self):
        try:
            i = self.info()
            return "<Song %s %g BPM %d/%d, %d bars>" % (i["file"] or "(unsaved)", i["tempo"],
                                                        i["time_signature"][0], i["time_signature"][1], i["length_bars"])
        except Exception:
            return "<Song (not connected)>"


class MixerChannel:
    """A mixer channel (0 = master)."""

    def __init__(self, desc):
        self._desc = desc
        self.index = desc["index"]

    def info(self):
        self._desc = call("mixer.channel", index=self.index)
        return self._desc

    def _set(self, **kw):
        self._desc = call("mixer.set", index=self.index, **kw)

    name = property(lambda self: self.info()["name"], lambda self, v: self._set(name=v))
    volume = property(lambda self: self.info()["volume"], lambda self, v: self._set(volume=v), doc="0..2 (1 = unity)")
    muted = property(lambda self: self.info()["muted"], lambda self, v: self._set(muted=v))
    solo = property(lambda self: self.info()["solo"], lambda self, v: self._set(solo=v))
    color = property(lambda self: self.info()["color"], lambda self, v: self._set(color=v))

    @property
    def sends(self):
        """List of ``(channel_index, amount)``."""
        return [(s["to"], s["amount"]) for s in self.info()["sends"]]

    def send_to(self, channel, amount=1.0):
        """Route this channel into another (amount 0 removes the send)."""
        self._desc = call("mixer.send", **{"from": self.index, "to": _channel_index(channel), "amount": amount})

    @property
    def effects(self):
        return [_wrap(e) for e in self.info()["effects"]]

    def add_effect(self, plugin, key=None, params=None):
        args = {"index": self.index, "plugin": plugin}
        if key is not None:
            args["key"] = key
        if params:
            args["params"] = params
        return _wrap(call("mixer.add_effect", **args))

    @property
    def params(self):
        """volume / muted / solo as automatable :class:`Param` objects."""
        handles = self.info()["params"]
        return ParamSet(None, None, items=lambda: [dict(call("param.get", id=h), key=k) for k, h in handles.items()])

    def delete(self):
        call("mixer.delete_channel", index=self.index)

    def __repr__(self):
        return "<MixerChannel %d %r>" % (self.index, self._desc.get("name"))


class Mixer:
    """The mixer (``lmms.mixer``)."""

    @property
    def channels(self):
        return [MixerChannel(d) for d in call("mixer.channels")]

    @property
    def master(self):
        return self.channel(0)

    def channel(self, index):
        return MixerChannel(call("mixer.channel", index=index))

    def add_channel(self, name=None):
        return MixerChannel(call("mixer.add_channel", **({"name": name} if name else {})))

    def __len__(self):
        return len(call("mixer.channels"))


class Patterns:
    """The pattern editor ("beat/bassline editor", ``lmms.patterns``).

    Its tracks hold one clip per pattern; song-editor :class:`PatternTrack` clips play them."""

    @property
    def count(self):
        return call("patterns.info")["count"]

    @property
    def tracks(self):
        return [_wrap(t) for t in call("song.tracks", container="pattern")]

    def track(self, name):
        for t in self.tracks:
            if t._desc["name"] == name:
                return t
        raise KeyError(name)

    def add_instrument_track(self, instrument="kicker", name=None, key=None, preset=None, params=None):
        kw = {"instrument": instrument}
        if key is not None:
            kw["key"] = key
        if preset is not None:
            kw["preset"] = _path(preset)
        if params:
            kw["instrument_params"] = params
        return song.add_track("instrument", name, container="pattern", **kw)

    def add_sample_track(self, name=None):
        return song.add_track("sample", name, container="pattern")


song = Song()
mixer = Mixer()
patterns = Patterns()


# --------------------------------------------------------------------------- misc

def plugins(type=None, sub_plugins=False):
    """Installed plugins as dicts (name, display_name, description, type).

    ``sub_plugins=True`` also lists the plugins inside LADSPA/LV2/VST3 wrappers."""
    params = {"sub_plugins": sub_plugins}
    if type:
        params["type"] = type
    return call("plugins.list", **params)


def instruments(sub_plugins=False):
    """Names of the available instruments (use with ``song.add_instrument_track``)."""
    return plugins("instrument", sub_plugins)


def effects(sub_plugins=False):
    """Available effects (use with ``track.add_effect``)."""
    return plugins("effect", sub_plugins)


def paths():
    """Directories as a dict: factory_samples, factory_presets, factory_projects, user_dir,
    user_samples, user_presets, user_projects."""
    return call("app.paths")


def factory_sample(name):
    """Path of a sample shipped with LMMS, e.g. ``factory_sample("drums/kick01.ogg")``."""
    return os.path.join(paths()["factory_samples"], name)


def analyze(path):
    """Peak/RMS statistics of an audio file."""
    return call("app.analyze", path=_path(path))


def screenshot(path, window="main"):
    """Save a PNG of a window (main, song_editor, piano_roll, pattern_editor, automation_editor, mixer)."""
    return call("gui.screenshot", path=_path(path), window=window)


def show(window):
    """Bring an editor window to the front (song_editor, piano_roll, pattern_editor, automation_editor, mixer)."""
    call("gui.show", window=window)


def message(text):
    """Print a line in the LMMS Script Console (or LMMS' stdout when headless)."""
    call("app.message", text=str(text))


def quit(code=0):
    """Quit LMMS."""
    call("app.quit", code=code)
    disconnect()


def help(topic=None):
    """Print an overview of the API (or the help of an object: ``lmms.help(lmms.MidiClip)``)."""
    if topic is not None:
        import builtins
        return builtins.help(topic)
    print(_HELP)


_HELP = """LMMS scripting - quick reference (positions/lengths in beats, pitches as MIDI numbers or "C4")

  song = lmms.song                       the project
    .tempo .time_signature .master_volume .length .position .playing .loop
    .new(empty=True) .open(p) .save(p) .import_midi(p) .export_midi(p)
    .render("out.wav") -> {peak_db, rms_db, seconds, ...}
    .play(start=None) .stop() .pause() .set_loop(start, end)
    .tracks  .track("name")
    .add_instrument_track("tripleoscillator", name=, key=, preset=, params=)
    .add_sample_track() .add_automation_track() .add_pattern_track()
  track: .name .muted .solo .color .volume .panning .mixer_channel .clips .params
    .delete() .clone() .move(i)
    instrument tracks: .instrument .set_instrument(p) .load_preset(f) .add_midi_clip(start, length)
                       .add_effect("reverbsc") .effects .play_note("C4")
    sample tracks: .add_sample_clip(file, start)
    automation tracks: .add_automation_clip(param, start, length, points=[(0, 0), (8, 1)])
    automation clip: .points .set_points() .generate("sine", cycles=8, amplitude="up", frequency="down")
                     .generate(expression="sin(tau*p)*(1-t)", cycles=6)
    pattern tracks: .add_clip(start, length)
  midi clip: .notes .add_note(p, start, length, velocity) .add_notes([...]) .add_chord(lmms.chord("Am7"))
             .set_notes() .clear() .transpose(n) .quantize(0.25) .remove_notes(fn)
  plugin (instrument/effect): .params  (dict-like: params["cutoff"] = 0.5; print(params) lists all)
             .set(key=value) .get_state() .set_state(xml) .load_file(p)
             effects: .enabled .wet .move(by) .delete()
  lmms.mixer: .channels .channel(i) .add_channel(name) -> .volume .send_to(ch) .add_effect(p)
  lmms.patterns: .add_instrument_track("kicker") -> .set_steps([0, 4, 8, 12], pattern=0)
  lmms.instruments() lmms.effects() lmms.note("C4") lmms.chord("C4", "maj7") lmms.scale("A3", "minor")
  lmms.bars(n) lmms.analyze(file) lmms.screenshot(png) lmms.show(window) lmms.message(t) lmms.quit()
  lmms.paths() lmms.factory_sample("drums/kick01.ogg")
  lmms.call("method", **params) for raw commands; lmms.api() lists them.
"""
