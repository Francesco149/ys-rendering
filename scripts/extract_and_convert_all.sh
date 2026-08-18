#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "Running full extraction and conversion pipeline for Ys: The Oath in Felghana..."
nix develop --command python3 src/tools/extract_and_convert_all.py "$@"
