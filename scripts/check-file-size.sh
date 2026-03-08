#!/usr/bin/env bash
# check-file-size.sh - Check that source files do not exceed 1500 lines
#
# Usage: ./scripts/check-file-size.sh [files...]
#        (pre-commit passes file paths as arguments)

set -euo pipefail

MAX_LINES=1500
FAILED=0

for f in "$@"; do
    lines=$(wc -l < "$f")
    if [ "$lines" -gt "$MAX_LINES" ]; then
        echo "FAIL: $f has $lines lines (max $MAX_LINES)"
        FAILED=1
    fi
done

exit $FAILED
