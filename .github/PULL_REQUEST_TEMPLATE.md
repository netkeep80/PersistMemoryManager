## Summary

Describe the change and the user-visible effect.

## Change Contract

Keep this YAML block aligned with the intended diff so repo-guard can validate the PR.

```repo-guard-yaml
change_type: chore
scope:
  - README.md
budgets:
  max_new_files: 1
  max_new_docs: 1
  max_net_added_lines: 200
must_touch: []
must_not_touch:
  - LICENSE
expected_effects:
  - Describe the expected effect
```

## Verification

- [ ] Local checks run
