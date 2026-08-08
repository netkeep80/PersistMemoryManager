#!/usr/bin/env bash
# check-source-loc-budget.sh - Issue #375 production LOC budget guard.
#
# This script enforces the simplifying-refactor LOC budget set by issue #375:
# the generated single-header `single_include/pmm/pmm.h` must not exceed the
# committed baseline. It also fails on a small set of forbidden patterns that
# would re-introduce the duplicate layers issue #375 removed.
#
# Usage:  ./scripts/check-source-loc-budget.sh
# Exit:   0 on success, 1 on budget breach or forbidden pattern.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BASELINE_FILE="scripts/source-loc-baseline.txt"
SINGLE_HEADER="single_include/pmm/pmm.h"

if [[ ! -f "$BASELINE_FILE" ]]; then
    echo "FAIL: missing baseline file $BASELINE_FILE"
    exit 1
fi

BASELINE=$(< "$BASELINE_FILE")
ACTUAL=$(wc -l < "$SINGLE_HEADER")

echo "production LOC: $SINGLE_HEADER = $ACTUAL (baseline $BASELINE)"

FAILED=0
if (( ACTUAL > BASELINE )); then
    echo "FAIL: $SINGLE_HEADER has $ACTUAL lines, exceeds baseline $BASELINE"
    echo "Issue #375 budgets simplifying refactors: production LOC must not grow."
    FAILED=1
fi

# ─── Forbidden patterns (production code only) ────────────────────────────────
PROD_PATHS=(include/pmm)

scan_forbidden() {
    local pattern="$1"
    local description="$2"
    local matches
    if matches=$(grep -RIn -E "$pattern" "${PROD_PATHS[@]}" 2>/dev/null); then
        echo "FAIL: $description"
        echo "$matches"
        FAILED=1
    fi
}

scan_forbidden 'thread_local[[:space:]]+BlockHeader' \
    "thread_local BlockHeader sentinel found — issue #375 removed this pattern"

scan_forbidden 'static[[:space:]]+BlockHeader<[^>]+>&[[:space:]]+tree_node\(' \
    "ref-returning ManagerT::tree_node(pptr) found — replaced by try_tree_node/tree_node_unchecked"

# pptr::tree_node() ref-returning method is gone; only try_tree_node/tree_node_unchecked remain.
if grep -RIn -E '\<tree_node\([[:space:]]*\)' "${PROD_PATHS[@]}" 2>/dev/null \
        | grep -v 'tree_node_unchecked\|try_tree_node' >/tmp/_pmm_legacy_tree_node || true; then
    if [[ -s /tmp/_pmm_legacy_tree_node ]]; then
        echo "FAIL: legacy tree_node() reference-returning method found:"
        cat /tmp/_pmm_legacy_tree_node
        FAILED=1
    fi
fi
rm -f /tmp/_pmm_legacy_tree_node

# Manual ensure_capacity allocate/memcpy/deallocate paths are forbidden in
# parray/pstring; reallocate_typed is the canonical path. Extract the body of
# `ensure_capacity(...) noexcept { ... }` regardless of return type and require
# reallocate_typed, while forbidding allocate/deallocate/memcpy/memmove inside it.
check_ensure_capacity_body() {
    local f="$1"
    local body
    body=$(awk '
        /ensure_capacity\([^;]*\)[[:space:]]*noexcept/ { in_fn = 1; depth = 0 }
        in_fn {
            print
            for ( i = 1; i <= length( $0 ); i++ ) {
                c = substr( $0, i, 1 )
                if ( c == "{" ) depth++
                else if ( c == "}" ) {
                    depth--
                    if ( depth == 0 ) { in_fn = 0; exit }
                }
            }
        }
    ' "$f")
    if [[ -z "$body" ]]; then
        echo "FAIL: $f has no ensure_capacity body to scan"
        FAILED=1
        return
    fi
    if ! grep -qE 'reallocate_typed' <<<"$body"; then
        echo "FAIL: $f::ensure_capacity must use ManagerT::reallocate_typed"
        FAILED=1
    fi
    local forbidden
    if forbidden=$(grep -nE 'ManagerT::(allocate|deallocate)\(|\bstd::(memcpy|memmove)\(|\bmemcpy\(|\bmemmove\(' \
        <<<"$body"); then
        echo "FAIL: $f::ensure_capacity reintroduces forbidden manual relocation:"
        echo "$forbidden"
        FAILED=1
    fi
}
check_ensure_capacity_body include/pmm/parray.h
check_ensure_capacity_body include/pmm/pstring.h

exit $FAILED
