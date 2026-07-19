#!/usr/bin/env bash
# validate_sources.sh
#
# Verifies that every .c source file listed in CMakeLists.txt actually exists
# in the Lexbor source tree.
#
# Usage:
#   ./validate_sources.sh [lexbor_root]
#
# Default lexbor_root: ../lexbor (relative to this script's directory)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LEXBOR_ROOT="${1:-${SCRIPT_DIR}/../lexbor}"
LEXBOR_SRC="${LEXBOR_ROOT}/source/lexbor"
CMAKE="${SCRIPT_DIR}/CMakeLists.txt"

if [[ ! -f "$CMAKE" ]]; then
    echo "ERROR: CMakeLists.txt not found at $CMAKE"
    exit 1
fi

if [[ ! -d "$LEXBOR_SRC" ]]; then
    echo "ERROR: Lexbor source not found at $LEXBOR_SRC"
    echo "Pass the lexbor root as: $0 /path/to/lexbor"
    exit 1
fi

echo "Validating source files..."
echo "  CMakeLists.txt : $CMAKE"
echo "  Lexbor source  : $LEXBOR_SRC"
echo ""

MISSING=0
FOUND=0

# Extract all ${LEXBOR_SRC}/... paths from CMakeLists.txt
# Replace the CMake variable with the actual path, then check each file.
while IFS= read -r line; do
    # Strip leading whitespace
    line="${line#"${line%%[![:space:]]*}"}"
    # Skip comments and non-source lines
    [[ "$line" =~ ^\# ]] && continue
    [[ "$line" =~ \$\{LEXBOR_SRC\} ]] || continue

    # Expand ${LEXBOR_SRC} to actual path
    filepath="${line/\$\{LEXBOR_SRC\}/${LEXBOR_SRC}}"
    # Strip trailing CMake syntax (e.g., trailing #comments)
    filepath="${filepath%%#*}"
    # Strip trailing whitespace
    filepath="${filepath%"${filepath##*[![:space:]]}"}"

    if [[ -f "$filepath" ]]; then
        FOUND=$((FOUND + 1))
    else
        echo "MISSING: $filepath"
        MISSING=$((MISSING + 1))
    fi
done < "$CMAKE"

echo ""
echo "Results: $FOUND files found, $MISSING missing."
if [[ $MISSING -gt 0 ]]; then
    echo "FAIL: Some source files are missing. Fix CMakeLists.txt."
    exit 1
else
    echo "OK: All source files present."
fi
