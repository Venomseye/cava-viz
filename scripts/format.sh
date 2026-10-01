#!/usr/bin/env bash
# Format every C++ source with the repo's .clang-format.
#
#   scripts/format.sh           rewrite files in place
#   scripts/format.sh --check   exit non-zero if any file would change
#
# CI uses clang-format-18.  A different local version can format slightly
# differently, so if CI still complains after a local run, use the
# "Format code" workflow (Actions tab -> Run workflow) instead: it runs the
# exact same binary as the check.
set -euo pipefail
cd "$(dirname "$0")/.."

CF="${CLANG_FORMAT:-}"
if [ -z "$CF" ]; then
    for c in clang-format-18 clang-format; do
        if command -v "$c" >/dev/null 2>&1; then CF="$c"; break; fi
    done
fi
if [ -z "$CF" ]; then
    echo "clang-format not found. Install clang-format-18 or set CLANG_FORMAT=/path/to/clang-format" >&2
    exit 2
fi

if [ "${1:-}" = "--check" ]; then
    find src tests \( -name '*.cpp' -o -name '*.h' \) -print0 \
        | xargs -0 "$CF" --style=file --dry-run -Werror
else
    find src tests \( -name '*.cpp' -o -name '*.h' \) -print0 \
        | xargs -0 "$CF" --style=file -i
fi
