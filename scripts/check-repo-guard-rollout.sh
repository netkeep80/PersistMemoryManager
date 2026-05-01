#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workflow="$repo_root/.github/workflows/repo-guard.yml"
policy="$repo_root/repo-policy.json"
issue_template="$repo_root/.github/ISSUE_TEMPLATE/change-contract.yml"
pr_template="$repo_root/.github/PULL_REQUEST_TEMPLATE.md"

python3 - "$workflow" "$policy" "$issue_template" "$pr_template" <<'PY'
import json
import pathlib
import re
import sys

workflow_path = pathlib.Path(sys.argv[1])
policy_path = pathlib.Path(sys.argv[2])
issue_template_path = pathlib.Path(sys.argv[3])
pr_template_path = pathlib.Path(sys.argv[4])

workflow = workflow_path.read_text(encoding="utf-8")
policy = json.loads(policy_path.read_text(encoding="utf-8"))
issue_template = issue_template_path.read_text(encoding="utf-8")
pr_template = pr_template_path.read_text(encoding="utf-8")
expected_action_ref = "6c81bb1050c7dca93de1a13108e0a024fe095298"
expected_action = f"netkeep80/repo-guard@{expected_action_ref}"
old_action_refs = {
    "7ab5ca2f2d9859b4ffa2c423f05e951d4971be84",
    "99bf716da62c5d01070aa0d7e4d4f8031b43a351",
}
expected_profiles = {
    "governance",
    "docs-comments-cleanup",
    "kernel-compaction",
    "kernel-hardening",
    "extraction-prep",
    "generated-refresh",
    "release",
}
expected_size_rules = [
    ("kernel-subtree-max-bytes", "directory", "bytes", "include/**", 270000),
]
required_governance_paths = {
    ".github/workflows/repo-guard.yml",
    ".github/workflows/docs-consistency.yml",
    ".github/PULL_REQUEST_TEMPLATE.md",
    ".github/ISSUE_TEMPLATE/change-contract.yml",
    "scripts/check-repo-guard-rollout.sh",
}

checks = []
def require(ok, message):
    checks.append((bool(ok), message))
def as_set(value):
    return set(value if isinstance(value, list) else [])
def profile(name):
    profiles = policy.get("change_profiles", {})
    return profiles.get(name, {}) if isinstance(profiles, dict) else {}
def profile_new_files(name):
    return profile(name).get("new_files", {})
def profile_budgets(name):
    return profile(name).get("budgets", {})
def require_contains(container, expected, message):
    require(expected <= as_set(container), message)

action_refs = re.findall(r"uses:\s*netkeep80/repo-guard@([^\s#]+)", workflow)
require(action_refs, "repo-guard workflow must use the reusable netkeep80/repo-guard Action")
if action_refs:
    action_ref = action_refs[0]
    require(
        action_ref == expected_action_ref,
        f"repo-guard workflow must use the pinned reusable Action {expected_action}",
    )
    require(
        re.fullmatch(r"[0-9a-f]{40}", action_ref) or re.fullmatch(r"v\d+\.\d+\.\d+", action_ref),
        "repo-guard workflow must use a pinned commit SHA or pinned release tag",
    )
for old_action_ref in old_action_refs:
    require(old_action_ref not in workflow, f"repo-guard workflow must not use old Action pin {old_action_ref}")
require("mode: check-pr" in workflow, "repo-guard workflow must run check-pr mode")
require("enforcement: advisory" in workflow, "repo-guard workflow must remain advisory in this stage")
require("fetch-depth: 0" in workflow, "repo-guard workflow must use full checkout history")
require("contents: read" in workflow, "repo-guard workflow must keep contents read-only permission")
require("issues: read" in workflow, "repo-guard workflow must keep issues read-only permission")
require("pull-requests: read" in workflow, "repo-guard workflow must keep pull-requests read-only permission")
require("continue-on-error" not in workflow, "repo-guard workflow must not mask failures with continue-on-error")
for forbidden in (
    "git clone https://github.com/netkeep80/repo-guard.git",
    "npm ci",
    "npm install",
    "node /tmp/repo-guard",
    "/tmp/repo-guard/src/repo-guard.mjs",
):
    require(forbidden not in workflow, f"repo-guard workflow must not use legacy manual integration pattern: {forbidden}")

policy_mode = policy.get("enforcement", {}).get("mode")
require(policy_mode == "advisory", "repo-policy.json must default to advisory enforcement")

for removed in ("change_classes", "new_file_rules", "change_type_rules"):
    require(removed not in policy, f"repo-policy.json must not use legacy {removed}")

profiles = policy.get("change_profiles", {})
require(isinstance(profiles, dict) and profiles, "repo-policy.json must define change_profiles")
if isinstance(profiles, dict):
    require(
        set(profiles) == expected_profiles,
        "repo-policy.json change_profiles must preserve exactly the PMM taxonomy",
    )

governance = profile("governance")
require_contains(
    governance.get("forbid_surfaces"),
    {"kernel", "generated"},
    "governance profile must forbid kernel and generated surfaces",
)
require(profile_budgets("governance").get("max_net_added_lines") == 80, "governance profile must keep the 80-line budget")
require(profile_budgets("governance").get("max_new_files") == 1, "governance profile must keep max_new_files=1")

docs_cleanup = profile("docs-comments-cleanup")
require(
    as_set(docs_cleanup.get("allow_surfaces")) == {"docs"},
    "docs-comments-cleanup profile must stay docs-only",
)
require(profile_new_files("docs-comments-cleanup").get("allow_classes") == [], "docs-comments-cleanup must forbid new files")
require(profile_budgets("docs-comments-cleanup").get("max_new_docs") == 0, "docs-comments-cleanup must forbid new docs")

for change_type in ("kernel-compaction", "kernel-hardening", "extraction-prep"):
    require_contains(
        profile(change_type).get("forbid_surfaces"),
        {"governance", "release"},
        f"{change_type} profile must forbid governance and release drift",
    )

require_contains(
    profile("kernel-compaction").get("forbid_surfaces"),
    {"docs"},
    "kernel-compaction profile must forbid docs drift",
)
require_contains(
    profile("kernel-hardening").get("forbid_surfaces"),
    {"docs"},
    "kernel-hardening profile must forbid docs drift",
)
require(
    as_set(profile("kernel-hardening").get("require_surfaces")) == {"tests"},
    "kernel-hardening profile must require tests",
)
for change_type in ("kernel-compaction", "extraction-prep"):
    new_files = profile_new_files(change_type)
    require_contains(
        new_files.get("allow_classes"),
        {"test", "kernel_module"},
        f"{change_type} profile must allow tests and one kernel module file",
    )
    require(
        new_files.get("max_per_class", {}).get("kernel_module") == 1,
        f"{change_type} profile must allow exactly one kernel_module new file",
    )

generated_refresh = profile("generated-refresh")
require(
    as_set(generated_refresh.get("allow_surfaces")) == {"generated", "release"},
    "generated-refresh profile must allow only generated and release surfaces",
)
require_contains(
    profile_new_files("generated-refresh").get("allow_classes"),
    {"generated", "release"},
    "generated-refresh profile must allow generated and release new-file classes",
)

release = profile("release")
require(as_set(release.get("allow_surfaces")) == {"release"}, "release profile must allow only release surface")
require(as_set(profile_new_files("release").get("allow_classes")) == {"release"}, "release profile must allow only release new files")

size_rules = policy.get("size_rules", [])
require(isinstance(size_rules, list) and size_rules, "repo-policy.json must define size_rules")
rules_by_id = {
    rule.get("id"): rule
    for rule in size_rules
    if isinstance(rule, dict) and isinstance(rule.get("id"), str)
}
for rule_id, scope, metric, glob, max_size in expected_size_rules:
    rule = rules_by_id.get(rule_id)
    require(rule is not None, f"size_rules must include {rule_id}")
    if not rule:
        continue
    for field, expected_value in {
        "scope": scope,
        "metric": metric,
        "glob": glob,
        "max": max_size,
    }.items():
        require(rule.get(field) == expected_value, f"{rule_id} must set {field}={expected_value!r}")
    require(
        rule.get("count", "all_tracked") == "all_tracked",
        f"{rule_id} must measure all tracked files, not only changed files",
    )
    require(
        rule.get("level", "blocking") == "blocking",
        f"{rule_id} must remain blocking-ready for the later enforcement switch",
    )
require(
    not any("single_include/" in str(rule.get("glob", "")) for rule in size_rules if isinstance(rule, dict)),
    "size_rules must target canonical include/pmm/**, not generated single_include/**",
)

governance_paths = set(policy.get("paths", {}).get("governance_paths", []))
missing_governance = sorted(required_governance_paths - governance_paths)
require(
    not missing_governance,
    "repo-policy.json governance_paths must include: " + ", ".join(missing_governance),
)

expected_surfaces = {"kernel", "tests", "docs", "governance", "release", "generated"}
surfaces = set(policy.get("surfaces", {}))
require(expected_surfaces <= surfaces, "repo-policy.json surfaces must include PMM v1 surfaces")

new_file_classes = policy.get("new_file_classes", {})
require(
    "kernel_module" in new_file_classes,
    "new_file_classes must distinguish legitimate kernel modules from forbidden shard files",
)
require(
    "kernel_shard" in new_file_classes,
    "new_file_classes must keep forbidden include shard files classified",
)

registry_rules = policy.get("registry_rules", [])
require(
    any(rule.get("id") == "canonical-docs-sync" for rule in registry_rules if isinstance(rule, dict)),
    "repo-policy.json registry_rules must include canonical-docs-sync",
)

require(
    "authorized_governance_paths" in issue_template,
    "issue template must expose authorized_governance_paths",
)
require(
    "issue body" in issue_template.lower() and "only when" in issue_template.lower(),
    "issue template must explain issue-body-only governance authorization",
)
require(
    "authorized_governance_paths" in pr_template,
    "PR template must mention authorized_governance_paths",
)
require(
    "linked issue body" in pr_template.lower() and "ignored" in pr_template.lower(),
    "PR template must explain linked-issue-only governance authorization",
)

missing_files = [
    path for path in required_governance_paths if not (policy_path.parent / path).exists()
]
require(
    not missing_files,
    "governed repo-guard rollout files must exist: " + ", ".join(sorted(missing_files)),
)

failures = [message for ok, message in checks if not ok]
if failures:
    print("Repo-guard rollout check failed:")
    for failure in failures:
        print(f"  - {failure}")
    sys.exit(1)

print("Repo-guard rollout wiring is current.")
PY
