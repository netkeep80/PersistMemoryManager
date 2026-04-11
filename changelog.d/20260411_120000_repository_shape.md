---
bump: patch
---

### Added
- `docs/repository_shape.md` — target repository shape with full root-level inventory
- `docs/deletion_policy.md` — formal rules for keep / move / archive / delete decisions

### Changed
- Moved `demo.bat` and `test.bat` to `scripts/` per target shape rules
- Moved `demo.md` to `docs/demo.md` as documentation
- Added `imgui.ini` to `.gitignore` to prevent re-adding generated GUI state

### Removed
- `.gitkeep` placeholder file
- `imgui.ini` generated ImGui layout state file
