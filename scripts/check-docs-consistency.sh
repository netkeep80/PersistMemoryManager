#!/usr/bin/env bash
set -euo pipefail

# docs-owned invariants only.
#
# Scope: governance/docs coherence that must hold for ANY PR touching docs.
# Out of scope: version/release coherence (README badge, CHANGELOG, CMakeLists
# version). Those are release-owned and live in scripts/check-version-consistency.sh
# so that docs-only PRs do not get blocked on a forced README version bump.

FAILED=0

# --- Canonical docs listed in repo-policy.json must exist ---

if [ -f repo-policy.json ]; then
  in_canonical=false
  while IFS= read -r line; do
    if echo "$line" | grep -q '"canonical_docs"'; then
      in_canonical=true
      continue
    fi
    if $in_canonical && echo "$line" | grep -q '^\s*\]'; then
      in_canonical=false
      continue
    fi
    if $in_canonical; then
      doc_path=$(echo "$line" | grep -oP '"[^"]*\.md"' | tr -d '"' || true)
      if [ -n "$doc_path" ] && [ ! -f "$doc_path" ]; then
        echo "FAIL: canonical doc listed in repo-policy.json does not exist: $doc_path"
        FAILED=1
      fi
    fi
  done < repo-policy.json
fi

# --- Canonical docs in docs/index.md must match repo-policy.json ---

if [ -f repo-policy.json ] && [ -f docs/index.md ]; then
  policy_docs=$(mktemp)
  index_docs=$(mktemp)
  trap 'rm -f "$policy_docs" "$index_docs"' EXIT

  sed -n '/"canonical_docs"/,/\]/p' repo-policy.json \
    | grep -oP '"docs/[^"]*\.md"' | tr -d '"' > "$policy_docs" || true

  sed -n '/^## Canonical Documents/,/^## /p' docs/index.md \
    | grep -oP '\]\([^)]+\.md\)' \
    | sed -E 's/.*\]\(([^)]+)\)/docs\/\1/' > "$index_docs" || true

  if ! diff -u <(sort "$policy_docs") <(sort "$index_docs"); then
    echo "FAIL: docs/index.md canonical documents differ from repo-policy.json paths.canonical_docs"
    FAILED=1
  fi
fi

if [ "$FAILED" -eq 0 ]; then
  echo "OK: all docs-consistency checks passed"
else
  echo ""
  echo "Docs-consistency checks failed. Please fix the inconsistencies above."
fi

exit $FAILED
