#!/usr/bin/env bash
set -euo pipefail

# release-owned invariants: all three canonical version sources must agree.
#
# Sources of truth:
#   - CMakeLists.txt       (project(... VERSION X.Y.Z ...))
#   - README.md            (version badge)
#   - CHANGELOG.md         (latest ## [X.Y.Z] heading)
#
# This check is intentionally NOT part of docs-consistency so that a docs-only
# PR never gets blocked by a stale README badge. The badge is bumped by the
# release workflow, not by contributors writing docs.

FAILED=0

cmake_version=$(grep -oP 'project\(PersistMemoryManager VERSION \K[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt) || {
  echo "FAIL: could not extract version from CMakeLists.txt"
  exit 1
}

readme_version=$(grep -oP 'badge/version-\K[0-9]+\.[0-9]+\.[0-9]+' README.md) || {
  echo "FAIL: could not extract version badge from README.md"
  exit 1
}

changelog_version=$(grep -oP '^\#\# \[\K[0-9]+\.[0-9]+\.[0-9]+' CHANGELOG.md | head -1) || {
  echo "FAIL: could not extract latest version from CHANGELOG.md"
  exit 1
}

echo "CMakeLists.txt version: $cmake_version"
echo "README.md badge version: $readme_version"
echo "CHANGELOG.md latest version: $changelog_version"

if [ "$cmake_version" != "$readme_version" ]; then
  echo "FAIL: README badge version ($readme_version) does not match CMakeLists.txt ($cmake_version)"
  FAILED=1
fi

if [ "$cmake_version" != "$changelog_version" ]; then
  echo "FAIL: CHANGELOG latest version ($changelog_version) does not match CMakeLists.txt ($cmake_version)"
  FAILED=1
fi

if [ "$FAILED" -eq 0 ]; then
  echo "OK: version-consistency checks passed"
else
  echo ""
  echo "Version-consistency checks failed. These are release-owned — typically"
  echo "fixed by running the release workflow, not by hand-editing docs."
fi

exit $FAILED
