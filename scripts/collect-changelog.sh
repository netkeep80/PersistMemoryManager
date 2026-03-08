#!/usr/bin/env bash
# collect-changelog.sh - Collect changelog fragments into CHANGELOG.md
#
# Usage: ./scripts/collect-changelog.sh [--dry-run]
#
# This script:
# 1. Reads all *.md fragments from changelog.d/ (excluding README.md)
# 2. Determines the highest version bump type (major > minor > patch)
# 3. Collects all fragment content
# 4. Updates CHANGELOG.md with new version entry
# 5. Deletes processed fragments (unless --dry-run)
# 6. Updates VERSION in CMakeLists.txt

set -euo pipefail

DRY_RUN=false
if [[ "${1:-}" == "--dry-run" ]]; then
    DRY_RUN=true
    echo "Dry-run mode: no files will be modified"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CHANGELOG_D="$REPO_ROOT/changelog.d"
CHANGELOG_MD="$REPO_ROOT/CHANGELOG.md"
CMAKE_LISTS="$REPO_ROOT/CMakeLists.txt"

# ─── Collect fragments ────────────────────────────────────────────────────────
fragments=()
while IFS= read -r -d '' f; do
    [[ "$(basename "$f")" == "README.md" ]] && continue
    fragments+=("$f")
done < <(find "$CHANGELOG_D" -maxdepth 1 -name "*.md" -print0 | sort -z)

if [[ ${#fragments[@]} -eq 0 ]]; then
    echo "No changelog fragments found in changelog.d/ — skipping release."
    exit 0
fi

echo "Found ${#fragments[@]} fragment(s):"
for f in "${fragments[@]}"; do
    echo "  - $(basename "$f")"
done

# ─── Determine bump type ─────────────────────────────────────────────────────
BUMP_TYPE="patch"
for f in "${fragments[@]}"; do
    bump=$(grep -m1 '^bump:' "$f" 2>/dev/null | awk '{print $2}' || echo "patch")
    case "$bump" in
        major) BUMP_TYPE="major"; break ;;
        minor) [[ "$BUMP_TYPE" != "major" ]] && BUMP_TYPE="minor" ;;
        patch) : ;;
    esac
done
echo "Determined bump type: $BUMP_TYPE"

# ─── Read current version from CMakeLists.txt ────────────────────────────────
current_version=$(grep -oP 'project\(PersistMemoryManager VERSION \K[0-9]+\.[0-9]+\.[0-9]+' "$CMAKE_LISTS")
echo "Current version: $current_version"

IFS='.' read -r major minor patch <<< "$current_version"
case "$BUMP_TYPE" in
    major) major=$((major + 1)); minor=0; patch=0 ;;
    minor) minor=$((minor + 1)); patch=0 ;;
    patch) patch=$((patch + 1)) ;;
esac
new_version="$major.$minor.$patch"
echo "New version: $new_version"

# ─── Collect fragment content ─────────────────────────────────────────────────
today=$(date +%Y-%m-%d)
collected_content=""
for f in "${fragments[@]}"; do
    # Strip YAML frontmatter (--- ... ---)
    content=$(awk '/^---/{p++; next} p==2{print}' "$f")
    collected_content+="$content"$'\n'
done

new_entry="## [$new_version] - $today
$collected_content"

# ─── Update CHANGELOG.md ─────────────────────────────────────────────────────
if [[ "$DRY_RUN" == "true" ]]; then
    echo "--- Would insert into CHANGELOG.md: ---"
    echo "$new_entry"
else
    # Insert after the <!-- changelog-insert-here --> marker
    tmp_file=$(mktemp)
    awk -v entry="$new_entry" '
        /<!-- changelog-insert-here -->/ {
            print
            print ""
            print entry
            next
        }
        { print }
    ' "$CHANGELOG_MD" > "$tmp_file"
    mv "$tmp_file" "$CHANGELOG_MD"
    echo "Updated CHANGELOG.md with version $new_version"
fi

# ─── Update CMakeLists.txt version ───────────────────────────────────────────
if [[ "$DRY_RUN" == "true" ]]; then
    echo "Would update CMakeLists.txt: $current_version -> $new_version"
else
    sed -i "s/VERSION $current_version/VERSION $new_version/" "$CMAKE_LISTS"
    echo "Updated CMakeLists.txt: $current_version -> $new_version"
fi

# ─── Delete processed fragments ───────────────────────────────────────────────
if [[ "$DRY_RUN" == "false" ]]; then
    for f in "${fragments[@]}"; do
        rm "$f"
        echo "Deleted fragment: $(basename "$f")"
    done
fi

echo "Done. New version: $new_version"
