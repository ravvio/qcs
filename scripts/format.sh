#!/bin/sh
# Format the project's C sources and headers with clang-format.
#
#   scripts/format.sh          format in place
#   scripts/format.sh --check  only report, fail if something isn't formatted

set -eu

cd "$(dirname "$0")/.."

# Tracked and new (not ignored) files, without tracked ones deleted since
files=$(git ls-files --cached --others --exclude-standard '*.c' '*.h' |
  while read -r f; do
    if [ -f "$f" ]; then echo "$f"; fi
  done)

if [ "${1:-}" = "--check" ]; then
  clang-format --dry-run --Werror $files
else
  clang-format -i $files
fi
