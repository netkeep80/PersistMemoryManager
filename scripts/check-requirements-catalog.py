#!/usr/bin/env python3
"""Extended catalog validator for the PMM requirements set.

Complements ``scripts/check-requirements-traceability.py`` by enforcing the
contract documented in ``req/templates/`` and ``req/README.md``:

1. Catalog hygiene
   - every ``## <id>`` is a valid lowercase ``ttt-xxx`` slug;
   - the file-name prefix matches the requirement type (``br`` -> ``01_*``);
   - the requirement is unique within the file.

2. Field-level invariants
   - mandatory primary field (``Требование`` / ``Характеристика`` / ``Правило`` /
     ``Ограничение`` / ``Критерий``) is present;
   - ``Приоритет`` belongs to the enum ``{Must, Should, Could, Won't}``;
   - ``Статус`` belongs to the enum
     ``{Draft, Active, Recovered, Deprecated, Superseded}`` — the legacy
     ``Issue #NNN`` form is rejected and must be migrated to the canonical
     enum plus a separate ``Tracking issue`` field;
   - the body is non-trivial (at least one sentence beyond the headers);
   - the ``— анкер`` text-format anchor reference is forbidden;
   - if the requirement opts in to the verbose template (``Формулировка`` is
     present), every type-specific mandatory field from
     ``req/templates/<type>.md`` must be filled.

3. Outgoing links
   - every Markdown link ``[..](path#anchor)`` resolves to an existing file
     and, when the target is an ``include/pmm/**`` source file, the listed
     anchor exists as a PMM block-comment heading in that file;
   - for ``req/*.md`` and ``docs/*.md`` targets the anchor must match a
     ``## <id>`` heading or its GitHub-style slug.

4. Bidirectional traceability (errors)
   - if a requirement lists ``[name](../include/pmm/<file>#anchor)`` under a
     traceability field, the corresponding source anchor must contain a
     ``req: <id>`` annotation pointing back to the requirement; helper
     anchors that intentionally do not trace can be allow-listed in
     ``req/.catalog-allowlist.json``;
   - every ``req:`` annotation in ``include/pmm/**`` and ``tests/**`` points
     to an existing requirement;
   - every requirement is reachable from at least one peer:
     either it is a top-level type (``br``) or another requirement (or an
     ``ac``, or a source anchor's ``req:`` line) references it.

5. Test traceability
   - every test file referenced from ``Проверяется в:`` / ``Тесты:``
     contains at least one ``req:`` line that lists the originating
     requirement (or a parent / acceptance criterion of it).

6. Supporting catalog files
   - ``req/templates/*.md``, ``req/README.md`` and
     ``req/13_traceability_matrix.md`` are scanned for broken Markdown
     links. Links inside fenced code blocks or inline code spans are
     skipped because templates contain sample snippets whose paths are
     relative to ``req/*.md``, not to the template file itself.

The script exits with status 1 if any violation is found, prints a summary
line otherwise. Allowlist entries can be added to
``req/.catalog-allowlist.json``::

    {
      "anchors_without_req_trace": [
        "include/pmm/free_block_tree.h#pmm-avlfreetree-find_best_fit"
      ],
      "requirements_without_incoming": [
        "br-001"
      ],
      "requirements_without_outgoing": [],
      "test_traceability_exceptions": [
        "fr-019:tests/test_typed_guard.cpp"
      ],
      "verbose_template_exceptions": [
        "fr-001"
      ]
    }
"""

from __future__ import annotations

import json
import pathlib
import re
import sys
from collections import defaultdict

ROOT = pathlib.Path(__file__).resolve().parent.parent
REQ_DIR = ROOT / "req"
INC_DIR = ROOT / "include" / "pmm"
TESTS_DIR = ROOT / "tests"
DOCS_DIR = ROOT / "docs"
ALLOWLIST = REQ_DIR / ".catalog-allowlist.json"
TEMPLATES_DIR = REQ_DIR / "templates"

NON_REQ_FILES = {"README.md", "13_traceability_matrix.md"}

# Type prefix -> requirement file. asm and dep coexist in the same file.
TYPE_FILE = {
    "br": "01_business_requirements.md",
    "rule": "02_business_rules.md",
    "ur": "03_user_requirements.md",
    "feat": "04_features.md",
    "fr": "05_functional_requirements.md",
    "dr": "06_data_requirements.md",
    "if": "07_external_interfaces.md",
    "qa": "08_quality_attributes.md",
    "con": "09_constraints.md",
    "sys": "10_system_requirements.md",
    "asm": "11_assumptions_dependencies.md",
    "dep": "11_assumptions_dependencies.md",
    "ac": "12_acceptance_criteria.md",
}

# Each requirement type has a primary "statement" field. We accept several
# legacy names because the catalog grew organically; the templates in
# ``req/templates/`` document ``Формулировка`` as the canonical name and a
# normalization pass can later collapse the synonyms.
PRIMARY_FIELDS = {
    "br": ("Требование", "Формулировка"),
    "rule": ("Правило", "Формулировка"),
    "ur": ("Требование", "Формулировка"),
    "feat": ("Характеристика", "Формулировка"),
    "fr": ("Требование", "Формулировка"),
    "dr": ("Требование", "Формулировка"),
    "if": ("Требование", "Формулировка"),
    "qa": ("Атрибут", "Требование", "Формулировка"),
    "con": ("Ограничение", "Формулировка"),
    "sys": ("Требование", "Формулировка"),
    "asm": ("Предположение", "Требование", "Формулировка"),
    "dep": ("Зависимость", "Требование", "Формулировка"),
    "ac": ("Критерий", "Формулировка"),
}

# Type-specific mandatory fields from ``req/templates/<type>.md``. Enforced
# only when the requirement opts in to the verbose template format by
# providing ``Формулировка`` (i.e., it stops using the legacy short form).
# Common required fields (``Тип``, ``Название``, ``Приоритет``, ``Статус``,
# ``Источник``, ``Формулировка``, ``Контекст и обоснование``) are listed
# explicitly below per type to keep the schema declarative.
COMMON_VERBOSE_FIELDS = (
    "Тип", "Название", "Приоритет", "Статус", "Источник",
    "Формулировка", "Контекст и обоснование",
)
REQUIRED_FIELDS_BY_TYPE = {
    "br": COMMON_VERBOSE_FIELDS + (
        "Бизнес-цель", "Ожидаемая ценность", "Границы",
        "Реализуется в", "Проверяется в",
    ),
    "rule": COMMON_VERBOSE_FIELDS + (
        "Правило", "Область применения", "Запрещённые состояния",
        "Последствия нарушения", "Связанные требования",
    ),
    "ur": COMMON_VERBOSE_FIELDS + (
        "Роль пользователя", "Пользовательская цель", "Сценарий",
        "Ожидаемый результат", "Реализуется в", "Проверяется в",
    ),
    "feat": COMMON_VERBOSE_FIELDS + (
        "Описание возможности", "Пользовательский или системный эффект",
        "Включённое поведение", "Исключённое поведение",
        "Реализует", "Реализуется в", "Проверяется в",
    ),
    "fr": COMMON_VERBOSE_FIELDS + (
        "Триггер/условие", "Поведение системы", "Результат", "Ошибки/отказы",
        "Реализует", "Реализуется в", "Проверяется в",
    ),
    "dr": COMMON_VERBOSE_FIELDS + (
        "Структура данных", "Инварианты", "Lifetime/persistence",
        "Допустимые значения",
        "Реализует", "Реализуется в", "Проверяется в",
    ),
    "if": COMMON_VERBOSE_FIELDS + (
        "Внешний API/интерфейс", "Сигнатуры или contract-level описание",
        "Preconditions", "Postconditions", "Ошибки", "Compatibility notes",
        "Реализует", "Реализуется в", "Проверяется в",
    ),
    "qa": COMMON_VERBOSE_FIELDS + (
        "Категория", "Атрибут качества", "Измеримая характеристика",
        "Сценарий проверки", "Допустимый порог",
        "Связанное функциональное поведение",
        "Реализуется в", "Проверяется в",
    ),
    "con": COMMON_VERBOSE_FIELDS + (
        "Ограничение", "Причина ограничения", "Область действия",
        "Последствия для реализации", "Связанные требования",
    ),
    "sys": COMMON_VERBOSE_FIELDS + (
        "Системный аспект", "Runtime/build-time условия",
        "Зависимости от платформы/компилятора", "Связь с CI/test matrix",
    ),
    "asm": COMMON_VERBOSE_FIELDS + (
        "Предположение", "Где используется", "Риск при нарушении",
        "Fallback/mitigation", "Связано с",
    ),
    "dep": COMMON_VERBOSE_FIELDS + (
        "Зависимость", "Тип зависимости", "Версия/условие",
        "Влияние на build/runtime", "Fallback/mitigation", "Связано с",
    ),
    "ac": COMMON_VERBOSE_FIELDS + (
        "Проверяемое поведение", "Шаги или сценарий проверки",
        "Expected result", "Проверяет",
    ),
}

PRIORITY_ENUM = {"Must", "Should", "Could", "Won't"}
STATUS_ENUM = {"Draft", "Active", "Recovered", "Deprecated", "Superseded"}

# Heading and link regexes
H2_RE = re.compile(r"^## ([^\n]+)$", re.MULTILINE)
HEADING_RE = re.compile(r"^(#{1,6}) +([^\n]+?)\s*$", re.MULTILINE)
LINK_RE = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")
FIELD_RE = re.compile(r"^- \*\*([^:*]+):\*\*\s*(.*)$", re.MULTILINE)
REQ_ID_RE = re.compile(r"^[a-z]+(?:-[a-z0-9]+)+$")
PMM_ID_REF_RE = re.compile(
    r"\(((?:\.\./)+include/pmm/[A-Za-z0-9_./-]+\.(?:h|inc))#([A-Za-z0-9_~-]+)\)"
)
ANCHOR_HEADING_RE = re.compile(r"^/\*\n(#+) ([^\n]+)\n((?:req:[^\n]+\n)*)\*/$",
                               re.MULTILINE)
REQ_LINE_RE = re.compile(
    r"^req:\s*([a-z][a-z0-9-]*(?:\s*,\s*[a-z][a-z0-9-]*)*)\s*$"
)
BANNED_ANCHOR_TEXT_RE = re.compile(r"`[^`]*\.(?:h|inc)`\s*—\s*анкер")
TRACKING_ISSUE_RE = re.compile(r"^#\d+(?:\s*,\s*#\d+)*\s*$")


def github_slug(heading: str) -> str:
    """Approximate GitHub-style anchor slug for a heading."""
    s = heading.strip().lower()
    # Drop punctuation that GitHub strips, keep word chars, hyphens, spaces
    s = re.sub(r"[^\w\s-]", "", s, flags=re.UNICODE)
    s = re.sub(r"\s+", "-", s)
    return s


def load_allowlist() -> dict:
    if not ALLOWLIST.exists():
        return {}
    try:
        return json.loads(ALLOWLIST.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        print(f"FAIL: invalid JSON in {ALLOWLIST.relative_to(ROOT)}: {exc}",
              file=sys.stderr)
        sys.exit(1)


def section_blocks(text: str) -> list[tuple[str, str, int]]:
    matches = list(H2_RE.finditer(text))
    blocks: list[tuple[str, str, int]] = []
    for i, m in enumerate(matches):
        heading = m.group(1).strip()
        start = m.end()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        line_no = text.count("\n", 0, m.start()) + 1
        blocks.append((heading, text[start:end], line_no))
    return blocks


def parse_fields(body: str) -> dict[str, str]:
    """Return a mapping ``field -> first-line value`` for ``- **Field:** ...``."""
    fields: dict[str, str] = {}
    for m in FIELD_RE.finditer(body):
        name = m.group(1).strip()
        value = m.group(2).strip()
        fields.setdefault(name, value)
    return fields


def expected_file_for(req_id: str) -> str | None:
    parts = req_id.split("-")
    return TYPE_FILE.get(parts[0]) if parts else None


def collect_anchor_index() -> tuple[dict[str, set[str]], dict[str, dict[str, set[str]]]]:
    """Index source anchors and their req: annotations.

    Returns:
        (file_anchors, req_back_refs)
        file_anchors: ``"include/pmm/foo.h" -> {anchor1, anchor2, ...}``
        req_back_refs: ``"<requirement-id>" -> {"<rel_path>#<anchor>", ...}``
    """
    file_anchors: dict[str, set[str]] = defaultdict(set)
    req_back_refs: dict[str, set[str]] = defaultdict(set)
    for path in sorted(list(INC_DIR.rglob("*.h")) + list(INC_DIR.rglob("*.inc"))):
        rel = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding="utf-8")
        for m in ANCHOR_HEADING_RE.finditer(text):
            anchor = m.group(2).strip()
            file_anchors[rel].add(anchor)
            req_block = m.group(3)
            for line in req_block.strip().splitlines():
                rm = REQ_LINE_RE.match(line.strip())
                if not rm:
                    continue
                ids = [x.strip() for x in rm.group(1).split(",") if x.strip()]
                for rid in ids:
                    req_back_refs[rid].add(f"{rel}#{anchor}")
    return file_anchors, req_back_refs


def collect_test_req_index() -> dict[str, set[str]]:
    """``test_file_relpath -> {req-id, ...}`` from ``req:`` annotations."""
    test_refs: dict[str, set[str]] = defaultdict(set)
    if not TESTS_DIR.exists():
        return test_refs
    for path in sorted(TESTS_DIR.rglob("*.cpp")):
        rel = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding="utf-8")
        for m in ANCHOR_HEADING_RE.finditer(text):
            req_block = m.group(3)
            for line in req_block.strip().splitlines():
                rm = REQ_LINE_RE.match(line.strip())
                if not rm:
                    continue
                for rid in [x.strip() for x in rm.group(1).split(",")
                            if x.strip()]:
                    test_refs[rel].add(rid)
    return test_refs


def collect_md_anchor_index() -> dict[str, set[str]]:
    """All H2 anchors in ``req/*.md`` and slug-style anchors in ``docs/*.md``."""
    md_anchors: dict[str, set[str]] = {}
    for md in sorted(REQ_DIR.glob("*.md")):
        text = md.read_text(encoding="utf-8")
        md_anchors[md.name] = {m.group(1).strip()
                               for m in H2_RE.finditer(text)}
    return md_anchors


def collect_docs_anchor_index() -> dict[str, set[str]]:
    """All heading slugs in ``docs/**/*.md`` and ``req/**/*.md``."""
    docs_anchors: dict[str, set[str]] = {}
    for base in (DOCS_DIR, REQ_DIR):
        if not base.exists():
            continue
        for md in sorted(base.rglob("*.md")):
            text = md.read_text(encoding="utf-8")
            anchors: set[str] = set()
            for m in HEADING_RE.finditer(text):
                anchors.add(github_slug(m.group(2)))
            rel = md.relative_to(ROOT).as_posix()
            docs_anchors[rel] = anchors
    return docs_anchors


INLINE_CODE_RE = re.compile(r"``[^`]*``|`[^`]*`")
# A real code fence is a line that begins with ``` followed only by an
# optional info string (language identifier). Lines like
# ```` ``` `code-span` ```` are inline-code escapes, not fences, and must
# not be treated as opening/closing a fence.
FENCE_LINE_RE = re.compile(r"^\s*```[A-Za-z0-9_-]*\s*$")


def strip_fenced_code(text: str) -> str:
    """Replace fenced code blocks and inline code spans with blank
    placeholders so link/regex scans do not treat sample snippets as
    real links. Lines are preserved so line numbers stay accurate for
    diagnostics."""
    out: list[str] = []
    in_fence = False
    for line in text.splitlines():
        if FENCE_LINE_RE.match(line):
            in_fence = not in_fence
            out.append("")
            continue
        if in_fence:
            out.append("")
            continue
        # Strip inline code: replace ``...`` and `...` with spaces of the
        # same length so column positions of preceding text are preserved.
        out.append(INLINE_CODE_RE.sub(lambda m: " " * len(m.group(0)), line))
    return "\n".join(out)


def validate_md_links(md_path: pathlib.Path,
                      md_anchors: dict[str, set[str]],
                      docs_anchors: dict[str, set[str]],
                      src_anchors: dict[str, set[str]],
                      errors: list[str]) -> None:
    """Validate every Markdown link in ``md_path`` resolves to a real target.

    Used for ``req/templates/*.md``, ``req/README.md`` and
    ``req/13_traceability_matrix.md`` — these are part of the requirements
    contract but not requirement entries themselves, so they are not parsed
    as ``## <id>`` blocks but their links must still resolve.

    Links inside fenced code blocks are skipped: templates contain sample
    snippets whose paths are relative to ``req/*.md``, not to the template
    file itself.
    """
    raw_text = md_path.read_text(encoding="utf-8")
    text = strip_fenced_code(raw_text)
    rel_md = md_path.relative_to(ROOT).as_posix()
    for line_no, line in enumerate(text.splitlines(), start=1):
        for m in LINK_RE.finditer(line):
            target = m.group(2)
            if target.startswith("http") or target.startswith("#"):
                continue
            file_part, _, anchor_part = target.partition("#")
            if not file_part:
                continue
            abs_path = (md_path.parent / file_part).resolve()
            try:
                rel_to_root = abs_path.relative_to(ROOT).as_posix()
            except ValueError:
                continue
            # Existence check
            if not abs_path.exists():
                errors.append(
                    f"{rel_md}:{line_no}: link to non-existent path "
                    f"'{target}'"
                )
                continue
            if not anchor_part:
                continue
            # Anchor resolution
            if rel_to_root.startswith("include/pmm/"):
                if anchor_part not in src_anchors.get(rel_to_root, set()):
                    errors.append(
                        f"{rel_md}:{line_no}: link to missing source anchor "
                        f"'{rel_to_root}#{anchor_part}'"
                    )
            elif rel_to_root.endswith(".md"):
                # Try req/*.md ## <id> anchors first (file-name scoped),
                # then fall back to GitHub-style heading slugs.
                base = pathlib.Path(rel_to_root).name
                if (base in md_anchors
                        and anchor_part in md_anchors[base]):
                    continue
                if anchor_part in docs_anchors.get(rel_to_root, set()):
                    continue
                errors.append(
                    f"{rel_md}:{line_no}: link to missing markdown anchor "
                    f"'{rel_to_root}#{anchor_part}'"
                )


def main() -> int:
    allowlist = load_allowlist()
    md_anchors = collect_md_anchor_index()
    docs_anchors = collect_docs_anchor_index()
    src_anchors, req_back_refs = collect_anchor_index()
    test_req_refs = collect_test_req_index()

    known_req_ids: set[str] = set()
    for md_name, ids in md_anchors.items():
        if md_name in NON_REQ_FILES:
            continue
        for rid in ids:
            known_req_ids.add(rid)

    errors: list[str] = []
    warnings: list[str] = []

    # Outgoing references collected per-requirement for the bidirectional
    # traceability check.
    outgoing_req: dict[str, set[str]] = defaultdict(set)
    outgoing_src: dict[str, set[str]] = defaultdict(set)
    outgoing_tests: dict[str, set[str]] = defaultdict(set)

    for md_path in sorted(REQ_DIR.glob("*.md")):
        if md_path.name in NON_REQ_FILES:
            continue
        text = md_path.read_text(encoding="utf-8")

        # File-level: forbid the textual ``— анкер`` form.
        for line_no, line in enumerate(text.splitlines(), start=1):
            if BANNED_ANCHOR_TEXT_RE.search(line):
                errors.append(
                    f"{md_path.name}:{line_no}: banned text-form anchor "
                    f"reference (use Markdown link)"
                )

        seen_in_file: dict[str, int] = {}
        for heading, body, line_no in section_blocks(text):
            # 1. Catalog hygiene
            if not REQ_ID_RE.match(heading):
                errors.append(
                    f"{md_path.name}:{line_no}: heading '## {heading}' is not "
                    f"a valid requirement id"
                )
                continue
            if heading in seen_in_file:
                errors.append(
                    f"{md_path.name}:{line_no}: duplicate id '{heading}' "
                    f"(first at line {seen_in_file[heading]})"
                )
            seen_in_file[heading] = line_no

            expected = expected_file_for(heading)
            if expected and expected != md_path.name:
                errors.append(
                    f"{md_path.name}:{line_no}: id '{heading}' belongs in "
                    f"'{expected}'"
                )

            # 2. Field-level invariants
            prefix = heading.split("-")[0]
            fields = parse_fields(body)

            primary_options = PRIMARY_FIELDS.get(prefix, ())
            if primary_options and not any(p in fields for p in primary_options):
                errors.append(
                    f"{md_path.name}:{line_no}: '{heading}' is missing the "
                    f"primary field (one of: {', '.join(primary_options)})"
                )

            priority = fields.get("Приоритет")
            if priority is None:
                errors.append(
                    f"{md_path.name}:{line_no}: '{heading}' is missing "
                    f"'Приоритет'"
                )
            elif priority not in PRIORITY_ENUM:
                errors.append(
                    f"{md_path.name}:{line_no}: '{heading}' has invalid "
                    f"'Приоритет: {priority}' (allowed: "
                    f"{sorted(PRIORITY_ENUM)})"
                )

            status = fields.get("Статус")
            if status is None:
                errors.append(
                    f"{md_path.name}:{line_no}: '{heading}' is missing "
                    f"'Статус'"
                )
            elif status not in STATUS_ENUM:
                # Legacy ``Issue #NNN`` form is no longer accepted: it
                # conflated tracking with status. Use a real enum value
                # plus an explicit ``Tracking issue`` field.
                if status.startswith("Issue #"):
                    errors.append(
                        f"{md_path.name}:{line_no}: '{heading}' uses legacy "
                        f"status '{status}'; replace with one of "
                        f"{sorted(STATUS_ENUM)} and add a separate "
                        f"'**Tracking issue:** #NNN' field"
                    )
                else:
                    errors.append(
                        f"{md_path.name}:{line_no}: '{heading}' has invalid "
                        f"'Статус: {status}' (allowed: {sorted(STATUS_ENUM)})"
                    )

            tracking_issue = fields.get("Tracking issue")
            if tracking_issue is not None and not TRACKING_ISSUE_RE.match(
                    tracking_issue):
                errors.append(
                    f"{md_path.name}:{line_no}: '{heading}' has invalid "
                    f"'Tracking issue: {tracking_issue}' (expected '#NNN' "
                    f"or comma-separated list)"
                )

            # Body must contain something beyond a single header line.
            non_blank = [ln for ln in body.strip().splitlines() if ln.strip()]
            if len(non_blank) < 3:
                errors.append(
                    f"{md_path.name}:{line_no}: '{heading}' body is too "
                    f"short ({len(non_blank)} lines); requirements must be "
                    f"described in detail"
                )

            # Type-specific verbose-template enforcement: triggered when the
            # requirement opts in to the canonical name 'Формулировка'.
            verbose_exceptions = set(
                allowlist.get("verbose_template_exceptions", []))
            if ("Формулировка" in fields
                    and heading not in verbose_exceptions):
                required = REQUIRED_FIELDS_BY_TYPE.get(prefix, ())
                missing = [f for f in required if f not in fields]
                if missing:
                    errors.append(
                        f"{md_path.name}:{line_no}: '{heading}' uses verbose "
                        f"template (Формулировка) but is missing required "
                        f"fields: {', '.join(missing)}"
                    )

            # 3. Outgoing links
            for link_text, target in LINK_RE.findall(body):
                if target.startswith("http"):
                    continue
                file_part, _, anchor_part = target.partition("#")
                if not file_part:
                    continue

                # Internal req anchor link
                if file_part.endswith(".md") and "/" not in file_part:
                    if file_part not in md_anchors:
                        errors.append(
                            f"{md_path.name}:{line_no}: '{heading}' links to "
                            f"unknown file '{file_part}'"
                        )
                    elif anchor_part and anchor_part not in md_anchors[file_part]:
                        errors.append(
                            f"{md_path.name}:{line_no}: '{heading}' links to "
                            f"missing anchor '{file_part}#{anchor_part}'"
                        )
                    elif anchor_part and anchor_part in known_req_ids:
                        outgoing_req[heading].add(anchor_part)
                    continue

                # Source-tree link (relative paths)
                if file_part.startswith("../") or file_part.startswith("./"):
                    abs_path = (md_path.parent / file_part).resolve()
                    try:
                        rel_to_root = abs_path.relative_to(ROOT).as_posix()
                    except ValueError:
                        warnings.append(
                            f"{md_path.name}:{line_no}: '{heading}' links to "
                            f"'{target}' outside the repository"
                        )
                        continue
                    if not abs_path.exists():
                        # Allow trailing slash directory references to exist.
                        if not (abs_path.is_dir()
                                or (abs_path.parent.exists()
                                    and abs_path.name == "")):
                            errors.append(
                                f"{md_path.name}:{line_no}: '{heading}' "
                                f"links to non-existent path '{target}'"
                            )
                            continue
                    if anchor_part:
                        if rel_to_root.startswith("include/pmm/"):
                            if anchor_part not in src_anchors.get(
                                    rel_to_root, set()):
                                errors.append(
                                    f"{md_path.name}:{line_no}: '{heading}' "
                                    f"links to missing source anchor "
                                    f"'{rel_to_root}#{anchor_part}'"
                                )
                            else:
                                outgoing_src[heading].add(
                                    f"{rel_to_root}#{anchor_part}")
                        elif rel_to_root.endswith(".md"):
                            # Markdown heading anchor in docs/ or other md.
                            anchors_in_target = docs_anchors.get(
                                rel_to_root, set())
                            if rel_to_root not in docs_anchors:
                                # e.g. ../README.md — best effort: skip
                                # unknown files; existence already verified.
                                pass
                            elif anchor_part not in anchors_in_target:
                                errors.append(
                                    f"{md_path.name}:{line_no}: '{heading}' "
                                    f"links to missing markdown anchor "
                                    f"'{rel_to_root}#{anchor_part}'"
                                )

                    # Track test references for test-traceability check.
                    if rel_to_root.startswith("tests/"):
                        outgoing_tests[heading].add(rel_to_root)

    # 4. Bidirectional traceability
    # 4a. Source anchors referenced from requirements should trace back via
    #     ``req:``. With the catalog now back-filled this is enforced as an
    #     error; ``anchors_without_req_trace`` allowlist still applies for
    #     anchors that intentionally do not trace.
    for rid, refs in outgoing_src.items():
        for ref in refs:
            if ref in allowlist.get("anchors_without_req_trace", []):
                continue
            back = req_back_refs.get(rid, set())
            if ref not in back:
                errors.append(
                    f"bidirectional: '{rid}' references source anchor "
                    f"'{ref}' but the anchor's req: line does not list "
                    f"'{rid}'"
                )

    # 4b. req: annotations in src/tests must reference known requirement IDs.
    for path_kind, root_dir, suffixes in (
        ("source", INC_DIR, ("*.h", "*.inc")),
        ("test", TESTS_DIR, ("*.cpp",)),
    ):
        if not root_dir.exists():
            continue
        for suffix in suffixes:
            for path in sorted(root_dir.rglob(suffix)):
                rel = path.relative_to(ROOT).as_posix()
                text = path.read_text(encoding="utf-8")
                for m in ANCHOR_HEADING_RE.finditer(text):
                    anchor = m.group(2).strip()
                    req_block = m.group(3)
                    for line in req_block.strip().splitlines():
                        rm = REQ_LINE_RE.match(line.strip())
                        if not rm:
                            errors.append(
                                f"{rel}: anchor '{anchor}' has malformed "
                                f"req line '{line.strip()}'"
                            )
                            continue
                        for rid in [x.strip() for x in rm.group(1).split(",")
                                    if x.strip()]:
                            if rid not in known_req_ids:
                                errors.append(
                                    f"{rel}: anchor '{anchor}' references "
                                    f"unknown requirement '{rid}'"
                                )

    # 4c. Every requirement must have at least one incoming link, except
    #     top-level (br) and ones explicitly allow-listed.
    incoming: dict[str, set[str]] = defaultdict(set)
    for src_id, targets in outgoing_req.items():
        for t in targets:
            incoming[t].add(src_id)
    # Source anchors that ``req:``-list a requirement count as incoming
    # references — they tie the catalog back to implementation code.
    for rid, refs in req_back_refs.items():
        for ref in refs:
            incoming[rid].add(f"src:{ref}")

    allow_no_incoming = set(allowlist.get("requirements_without_incoming", []))
    allow_no_outgoing = set(allowlist.get("requirements_without_outgoing", []))
    for rid in sorted(known_req_ids):
        prefix = rid.split("-")[0]
        if prefix == "br":
            continue
        if rid in allow_no_incoming:
            continue
        if not incoming.get(rid):
            errors.append(
                f"orphan: requirement '{rid}' has no incoming references "
                f"(add a parent ref or allow-list it in "
                f"req/.catalog-allowlist.json)"
            )

    for rid in sorted(known_req_ids):
        if rid in allow_no_outgoing:
            continue
        prefix = rid.split("-")[0]
        # ac requirements may have no further outgoing refs beyond Проверяет;
        # those land in outgoing_req via links from inside the AC body.
        if (prefix != "ac" and not outgoing_req.get(rid)
                and not outgoing_src.get(rid)
                and not outgoing_tests.get(rid)):
            errors.append(
                f"orphan: requirement '{rid}' has no outgoing references")

    # 5. Test traceability: every test linked from a requirement must have a
    #    ``req:`` annotation that mentions either the requirement itself,
    #    one of the AC's it gates, or any of its parents.
    test_exceptions = set(allowlist.get("test_traceability_exceptions", []))
    for rid, tests in outgoing_tests.items():
        for tpath in tests:
            key = f"{rid}:{tpath}"
            if key in test_exceptions:
                continue
            mentioned = test_req_refs.get(tpath, set())
            if rid in mentioned:
                continue
            # Allow indirect coverage: if the requirement's outgoing
            # parent refs (or its outgoing ac refs) appear in the test.
            related = outgoing_req.get(rid, set())
            if mentioned & related:
                continue
            errors.append(
                f"test-traceability: '{rid}' lists '{tpath}' under "
                f"Проверяется в:/Тесты:, but the test file does not "
                f"have a 'req:' line that lists '{rid}' or one of its "
                f"related ids"
            )

    # 6. Validate links in supporting catalog files (templates, README,
    #    traceability matrix). These files are part of the requirements
    #    contract but are not parsed as ``## <id>`` requirement blocks,
    #    so we only check that their Markdown links resolve.
    extra_md = list((REQ_DIR / "templates").rglob("*.md")) if (
        REQ_DIR / "templates").exists() else []
    for name in NON_REQ_FILES:
        candidate = REQ_DIR / name
        if candidate.exists():
            extra_md.append(candidate)
    for md in sorted(extra_md):
        validate_md_links(md, md_anchors, docs_anchors, src_anchors, errors)

    if warnings:
        for w in warnings:
            print(f"WARNING: {w}", file=sys.stderr)
    if errors:
        for e in errors:
            print(f"ERROR: {e}", file=sys.stderr)
        print(f"\nFAIL: {len(errors)} catalog violation(s), "
              f"{len(warnings)} warning(s)", file=sys.stderr)
        return 1
    print(f"OK: catalog is consistent ({len(warnings)} warning(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
