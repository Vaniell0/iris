#!/usr/bin/env bash
# Creates demo files — including ones that break bash but not irsh.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)/files"
mkdir -p "$DIR"
cd "$DIR"

dd if=/dev/urandom bs=1024 count=8   of=kernel.log      2>/dev/null
dd if=/dev/urandom bs=1024 count=3   of=auth.log        2>/dev/null
dd if=/dev/urandom bs=1024 count=12  of=access.log      2>/dev/null
dd if=/dev/urandom bs=1024 count=512 of=core.dump       2>/dev/null
dd if=/dev/urandom bs=1024 count=1   of=config.toml     2>/dev/null
dd if=/dev/urandom bs=512  count=1   of=readme.txt      2>/dev/null
# The filename that breaks bash
dd if=/dev/urandom bs=1024 count=6   of="my report 2026.csv" 2>/dev/null
dd if=/dev/urandom bs=1024 count=2   of="deploy (backup).sh" 2>/dev/null

echo "Files created in $DIR"
ls -lh "$DIR"
