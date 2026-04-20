#!/usr/bin/env python3
"""
strip-comments.py — Strip C/C++ comments from a source file.

Usage:
    python3 scripts/strip-comments.py input.h output.h

Handles:
  - // line comments
  - /* block comments */
  - String literals (comments inside "..." or '...' are preserved)
  - Collapses multiple consecutive blank lines into one

This script is used by generate-single-headers.sh to produce the
"no-comments" variant of the single-header library (pmm_no_comments.h).
"""

import sys


def strip_cpp_comments(source: str) -> str:
    """Return source with all C and C++ comments removed."""
    result: list[str] = []
    i = 0
    n = len(source)

    while i < n:
        ch = source[i]

        # String literal — copy verbatim, do not inspect inside
        if ch == '"':
            j = i + 1
            while j < n:
                if source[j] == '\\':
                    j += 2
                    continue
                if source[j] == '"':
                    j += 1
                    break
                j += 1
            result.append(source[i:j])
            i = j

        # Character literal — copy verbatim
        elif ch == "'":
            j = i + 1
            while j < n:
                if source[j] == '\\':
                    j += 2
                    continue
                if source[j] == "'":
                    j += 1
                    break
                j += 1
            result.append(source[i:j])
            i = j

        # Line comment (//) — drop everything up to (but not including) newline
        elif source[i:i + 2] == '//':
            j = i
            while j < n and source[j] != '\n':
                j += 1
            i = j  # newline kept by the default path below

        # Block comment (/* ... */) — replace with equivalent newlines so that
        # line numbers of remaining code are preserved (aids debugging)
        elif source[i:i + 2] == '/*':
            end = source.find('*/', i + 2)
            if end == -1:
                i = n  # unterminated comment — skip to EOF
            else:
                comment_text = source[i:end + 2]
                newlines = comment_text.count('\n')
                result.append('\n' * newlines)
                i = end + 2

        else:
            result.append(ch)
            i += 1

    return ''.join(result)


def collapse_blank_lines(source: str) -> str:
    """Reduce runs of more than one consecutive blank line to a single blank line."""
    lines = source.split('\n')
    out: list[str] = []
    prev_blank = False
    for line in lines:
        line = line.rstrip()
        is_blank = not line
        if is_blank and prev_blank:
            continue
        out.append(line)
        prev_blank = is_blank
    return '\n'.join(out)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input> <output>", file=sys.stderr)
        return 1

    input_path, output_path = sys.argv[1], sys.argv[2]

    with open(input_path, 'r', encoding='utf-8') as fh:
        source = fh.read()

    stripped = strip_cpp_comments(source)
    stripped = collapse_blank_lines(stripped)

    with open(output_path, 'w', encoding='utf-8') as fh:
        fh.write(stripped)

    orig_lines = source.count('\n')
    new_lines = stripped.count('\n')
    orig_bytes = len(source.encode('utf-8'))
    new_bytes = len(stripped.encode('utf-8'))
    print(
        f"  Stripped: {orig_lines} -> {new_lines} lines "
        f"({100 * (orig_lines - new_lines) / orig_lines:.1f}% fewer), "
        f"{orig_bytes} -> {new_bytes} bytes "
        f"({100 * (orig_bytes - new_bytes) / orig_bytes:.1f}% smaller)"
    )
    return 0


if __name__ == '__main__':
    sys.exit(main())
