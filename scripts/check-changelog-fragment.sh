#!/usr/bin/env bash
# check-changelog-fragment.sh - Verify a changelog fragment was added in the current PR
#
# Usage: GITHUB_BASE_REF=main ./scripts/check-changelog-fragment.sh
#
# This script checks that:
# 1. If source files (*.cpp, *.h, CMakeLists.txt, scripts/) were changed
# 2. Then at least one changelog fragment (changelog.d/*.md, not README.md) must be added
#
# Exit codes:
#   0 - Check passed (fragment present or no source changes)
#   1 - Check failed (source changes without a changelog fragment)

set -euo pipefail

BASE_REF="${GITHUB_BASE_REF:-main}"
echo "Checking changelog fragment against origin/$BASE_REF..."

# ─── Get changed files in this PR ────────────────────────────────────────────
changed_files=$(git diff --name-only "origin/$BASE_REF...HEAD" 2>/dev/null || git diff --name-only HEAD~1 2>/dev/null || echo "")

if [[ -z "$changed_files" ]]; then
    echo "No changed files detected — skipping check."
    exit 0
fi

echo "Changed files:"
echo "$changed_files" | sed 's/^/  /'

# ─── Check if any source files changed ───────────────────────────────────────
source_changed=false
while IFS= read -r file; do
    if [[ "$file" =~ \.(cpp|h)$ ]] || \
       [[ "$file" == "CMakeLists.txt" ]] || \
       [[ "$file" =~ ^tests/ ]] || \
       [[ "$file" =~ ^scripts/ ]] || \
       [[ "$file" =~ ^include/ ]] || \
       [[ "$file" =~ ^demo/ ]] || \
       [[ "$file" =~ ^examples/ ]]; then
        source_changed=true
        break
    fi
done <<< "$changed_files"

if [[ "$source_changed" == "false" ]]; then
    echo "No source files changed (only docs/config/etc.) — changelog fragment not required."
    exit 0
fi

echo "Source files were changed — checking for changelog fragment..."

# ─── Check if a changelog fragment was added in this PR ──────────────────────
fragment_added=false
while IFS= read -r file; do
    if [[ "$file" =~ ^changelog\.d/.+\.md$ ]] && \
       [[ "$(basename "$file")" != "README.md" ]]; then
        fragment_added=true
        echo "Found changelog fragment: $file"
        break
    fi
done <<< "$changed_files"

if [[ "$fragment_added" == "true" ]]; then
    echo "Changelog fragment check passed."
    exit 0
else
    echo ""
    echo "ERROR: Source files were changed but no changelog fragment was added!"
    echo ""
    echo "Please add a changelog fragment to changelog.d/ describing your changes:"
    echo ""
    echo "  touch changelog.d/\$(date +%Y%m%d_%H%M%S)_your_change.md"
    echo ""
    echo "Fragment format:"
    echo "  ---"
    echo "  bump: patch   # or minor / major"
    echo "  ---"
    echo ""
    echo "  ### Fixed"
    echo "  - Description of your change"
    echo ""
    echo "See changelog.d/README.md for details."
    exit 1
fi
