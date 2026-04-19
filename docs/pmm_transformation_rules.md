# PMM Transformation Rules

## Document status

This is the **canonical operational rulebook** that governs how `PersistMemoryManager`
may be transformed. It is normative: every issue, PR, review decision, and future
`repo-guard` policy must be consistent with it.

It does not restate the target model — that lives in
[pmm_target_model.md](pmm_target_model.md). This document adds only the
operational discipline that keeps PMM evolving toward that target.

If a rule here conflicts with a weaker convention elsewhere, this document wins.

## 1. Allowed issue types

PMM accepts only the following issue types:

- **kernel-hardening** — stronger invariants, validation, recovery, verify/repair;
- **kernel-compaction** — fewer files, fewer primitives, fewer code paths;
- **extraction-prep** — clean seams toward future separation of concerns;
- **governance/repo-guard** — rules, contracts, policy, review semantics;
- **docs-comments-cleanup** — removal of noise from docs and comments.

Any issue that does not fit one of these types is out of scope for PMM.

## 2. Atomic issue rule

- One issue = one engineering intent.
- One PR = one reason for change.

A PR that cannot be summarized by a single intent must be split before merge.

## 3. No mixed PR rule

A single PR must not combine changes from more than one of the following buckets:

- kernel-hardening;
- docs / comments cleanup;
- packaging / build / release;
- generated surface updates (e.g. `single_include/**`);
- governance / repo-guard.

Mixing these buckets in one PR is an automatic review rejection, even if every
individual change is correct in isolation.

## 4. Extraction-first rule

If a capability does not belong to PMM as a persistent storage kernel, it must
either:

- stay outside PMM, or
- be prepared for extraction (clean seams, no new coupling),

but it must not grow into PMM. PMM does not absorb upper-layer concerns.

Reference boundary: [pmm_target_model.md § 2–3](pmm_target_model.md).

## 5. Surface compression rule

- Every issue must aim for a **non-positive surface delta** by default
  (new files, net added lines, comments, docs).
- A temporary surface increase is allowed only as explicitly declared
  **surface debt**, recorded in the issue and the PR description.
- Surface debt must name the issue that will repay it.

Convenience surface is not a valid justification for growth.

## 6. Source / generated separation rule

- Generated surface (`single_include/**` and comparable artifacts) must not be
  updated in the same PR as kernel or governance changes.
- Regeneration PRs are their own change type and must be minimal and isolated,
  except when the generated diff is the mechanically required closure of an
  allowed canonical source change in the same PR.
- Hand-edits of generated surface are forbidden.
- Closure means the source diff is primary, the generated diff is directly
  derivable from it, and no independent logic or unrelated churn appears in the
  generated layer.
- Issue contracts that mark generated surface `must_not_touch` still allow this
  minimal source -> generated closure unless they explicitly forbid it.

## 7. Text discipline rule

- Documents and comments must not accumulate as process-noise.
- Every new document must have a **canonical place** and a **clear purpose**,
  and must be listed in [index.md](index.md).
- Historical notes, phase logs, and design diaries do not belong in `docs/`.
  They belong in Git history, issues, and pull requests.
- Comments follow [comment_policy.md](comment_policy.md); if they do not fit
  one of its allowed types, they are removed.

## 8. PR review semantics

A PR is first assessed on **contract conformance**, not on local code quality:

- does it match the declared issue type;
- does it stay inside the declared scope;
- does it respect the declared surface budget;
- does it avoid forbidden scope;
- does it avoid scope creep beyond the issue intent.

A technically correct PR that violates its issue contract is rejected.
A minimal PR that respects its contract is preferred over a broader one that
"also fixes a few other things".
For generated closure, reviewers check that the generated diff is mechanically
explained by the allowed source diff and is not a disguised mixed change.

## Success criterion

These rules are working if, over time, PMM issues become:

- smaller in scope;
- cleaner in intent;
- safer for repository surface;
- oriented toward collapsing PMM, not expanding it.

If this document itself starts to grow into a long guide, that growth violates
rule 5 and must be reversed.
