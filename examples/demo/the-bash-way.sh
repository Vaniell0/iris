#!/usr/bin/env bash
# Find files over 4KB, sort by size desc, write report.
#
# Known fragility:
#   - for f in $(ls) breaks on "my report 2026.csv" → splits on spaces
#   - stat flag is --format on Linux, -f on macOS — not portable
#   - awk column split unreliable when name contains tabs
#   - no types: size is a string until (( )) coerces it; wrong locale → wrong sort
#   - three external processes per file: stat, printf, sort

set -euo pipefail
THRESHOLD=4096
OUTPUT="report.txt"
SRCDIR="${1:-.}"

tmpfile=$(mktemp)
trap 'rm -f "$tmpfile" "$tmpfile.s"' EXIT

# Step 1: collect — breaks on filenames with spaces
for f in $(ls "$SRCDIR"); do                    # ← BROKEN: "my report 2026.csv"
    size=$(stat --format="%s" "$SRCDIR/$f" 2>/dev/null) || continue
    if (( size > THRESHOLD )); then
        printf '%s\t%s\n' "$size" "$f" >> "$tmpfile"
    fi
done

# Step 2: sort numerically descending
sort -t$'\t' -k1 -rn "$tmpfile" > "$tmpfile.s"

# Step 3: format — awk because printf can't align columns without it
awk -F'\t' '{
    size = $1 + 0
    name = $2
    if (size > 1048576)
        printf "%-36s %8.1f MB\n", name, size/1048576
    else
        printf "%-36s %8.1f KB\n", name, size/1024
}' "$tmpfile.s" > "$OUTPUT"

echo "$(wc -l < "$OUTPUT") files written to $OUTPUT"
