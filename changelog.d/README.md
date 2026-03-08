# Changelog Fragments

This directory contains changelog fragments that will be collected into `CHANGELOG.md` during releases.

## How to Add a Changelog Fragment

When making changes that should be documented in the changelog, create a fragment file:

```bash
# Create a new fragment with a timestamp-based name
touch changelog.d/$(date +%Y%m%d_%H%M%S)_description.md
```

The filename format is: `YYYYMMDD_HHMMSS_description.md`

## Fragment Format

Each fragment must include a **frontmatter section** specifying the version bump type:

```markdown
---
bump: patch
---

### Fixed
- Description of bug fix
```

### Bump Types

Use semantic versioning bump types in the frontmatter:

- **`major`**: Breaking changes (incompatible API changes)
- **`minor`**: New features (backward compatible)
- **`patch`**: Bug fixes and small improvements (backward compatible)

### Content Categories

Use these standard categories in your fragment:

```markdown
---
bump: minor
---

### Added
- Description of new feature

### Changed
- Description of change to existing functionality

### Fixed
- Description of bug fix

### Removed
- Description of removed feature

### Deprecated
- Description of deprecated feature
```

## How Fragments Are Used

During release:
1. All `*.md` fragment files (excluding `README.md`) are collected
2. The highest bump type among all fragments determines the version increment
3. All fragment contents are compiled into `CHANGELOG.md`
4. Fragment files are deleted after collection

## Important Notes

- Each pull request with code changes must include at least one changelog fragment
- Fragment files are independent — no merge conflicts between parallel PRs
- The CI pipeline validates that code-changing PRs include a changelog fragment
