#!/bin/sh
# Compatibility entry point; the runner itself is shared with native Windows shells.
set -eu
cd "$(dirname "$0")/.."
for python in python3 python; do
    if command -v "$python" >/dev/null 2>&1 &&
        "$python" -c 'import sys; sys.exit(sys.version_info.major != 3)' 2>/dev/null; then
        exec "$python" scripts/check.py "$@"
    fi
done
printf '%s\n' 'error: Python 3 not found on PATH' >&2
exit 1
