#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

for candidate in python3.13 python3.12 python3.11 python3.10 python3; do
    if command -v "$candidate" >/dev/null 2>&1 && \
       "$candidate" -c 'import tkinter; raise SystemExit(tkinter.TkVersion < 8.6)' >/dev/null 2>&1; then
        exec "$candidate" "$ROOT/simulator/run.py" "$@"
    fi
done

echo "Python with Tk 8.6 or newer is required (Tk 9 recommended)." >&2
echo "Install a current Python 3 release, then run this launcher again." >&2
exit 1
