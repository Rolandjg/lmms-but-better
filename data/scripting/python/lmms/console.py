"""Interpreter behind LMMS' Script Console (run as ``python -m lmms.console``).

Reads JSON lines ``{"code": "..."}`` from stdin, runs each in one persistent namespace and
prints a marker line after every block so LMMS knows when it finished. Like the interactive
interpreter, the value of a trailing expression is printed.
"""

import ast
import json
import sys
import traceback

import lmms

DONE = "\x1eLMMS_DONE:"


def _run(source, namespace):
    tree = ast.parse(source, "<console>", "exec")
    last = None
    if tree.body and isinstance(tree.body[-1], ast.Expr):
        last = ast.Expression(tree.body.pop().value)
    exec(compile(tree, "<console>", "exec"), namespace)
    if last is not None:
        value = eval(compile(last, "<console>", "eval"), namespace)
        if value is not None:
            namespace["_"] = value
            print(repr(value))


def _print_error():
    etype, value, tb = sys.exc_info()
    # hide this module's frames: show only the user's code
    while tb is not None and tb.tb_frame.f_code.co_filename == __file__:
        tb = tb.tb_next
    if isinstance(value, lmms.LmmsError):
        print("LmmsError: %s" % value, file=sys.stderr)
        frames = [f for f in traceback.extract_tb(tb) if f.filename == "<console>"]
        if frames:
            print("  (line %d of the script)" % frames[-1].lineno, file=sys.stderr)
    else:
        traceback.print_exception(etype, value, tb)


def main():
    namespace = {"__name__": "__main__", "lmms": lmms, "song": lmms.song, "mixer": lmms.mixer,
                 "patterns": lmms.patterns, "note": lmms.note, "chord": lmms.chord, "scale": lmms.scale}
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        try:
            source = json.loads(line)["code"]
        except (ValueError, KeyError):
            continue
        ok = True
        try:
            _run(source, namespace)
        except SystemExit:
            pass
        except BaseException:
            ok = False
            _print_error()
        sys.stdout.flush()
        sys.stderr.flush()
        sys.stdout.write(DONE + ("ok" if ok else "error") + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
