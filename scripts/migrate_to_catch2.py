#!/usr/bin/env python3
"""
Migrate PMM test files from custom PMM_TEST/PMM_RUN macros to Catch2 v3.

Three input patterns handled:
  A) PMM_TEST(expr) + PMM_RUN(name, fn) + static bool test_xxx() + main()
  B) PMM_TEST(cond, msg) + static void test_xxx() + main()  (thread tests)
  C) assert(expr) + main() body  (single-header tests)
"""

import re
import sys
import os
import glob


def read_file(path):
    with open(path, 'r') as f:
        return f.read()


def write_file(path, content):
    with open(path, 'w') as f:
        f.write(content)


def suite_name(filepath):
    return os.path.basename(filepath).replace('.cpp', '')


def remove_macro_block(content, macro_name, first_param):
    """Remove a #define MACRO(...) \\ multi-line block."""
    # Find #define <macro_name>( <first_param> ...
    pattern = re.compile(
        r'#define\s+' + re.escape(macro_name) + r'\s*\(\s*' + re.escape(first_param) + r'[^)]*\)\s*\\\n'
        r'(?:.*\\\n)*'
        r'.*\n',
        re.MULTILINE
    )
    return pattern.sub('', content)


def migrate_one_arg(content, tag):
    """Pattern A: PMM_TEST(expr) + PMM_RUN(name, fn)."""
    # 1. Remove macro definitions
    content = remove_macro_block(content, 'PMM_TEST', 'expr')
    content = remove_macro_block(content, 'PMM_RUN', 'name')

    # 2. Collect PMM_RUN( "display", func ) pairs before removing main
    runs = re.findall(r'PMM_RUN\(\s*"([^"]+)"\s*,\s*(\w+)\s*\)', content)
    run_map = {func: display for display, func in runs}

    # 3. Replace PMM_TEST( ... ) -> REQUIRE( ... )
    content = content.replace('PMM_TEST(', 'REQUIRE(')

    # 4. Convert function signatures
    def repl_func(m):
        fname = m.group(1)
        display = run_map.get(fname, fname)
        return f'TEST_CASE( "{display}", "[{tag}]" )\n{{'

    content = re.sub(
        r'static\s+bool\s+(test_\w+)\s*\(\s*\)\s*\n?\{',
        repl_func,
        content,
    )

    # 5. Remove 'return true;'
    content = re.sub(r'\n(\s*)return true;\n', '\n', content)

    # 6. Remove main() and everything after the main comment
    content = _remove_main_section(content)

    return content


def migrate_two_arg(content, tag):
    """Pattern B: PMM_TEST(cond, msg) + void test_xxx()."""
    # 1. Remove macro definition
    content = remove_macro_block(content, 'PMM_TEST', 'cond')

    # 2. Replace PMM_TEST( cond, msg ) -> INFO( msg ); REQUIRE( cond );
    def repl_two(m):
        indent = m.group(1)
        # We need to carefully extract cond and msg - msg is last quoted string
        inner = m.group(2).strip()
        # Find the last ", " followed by a quoted string
        # Use rfind to find the last occurrence of ", \""
        last_comma = inner.rfind(', "')
        if last_comma == -1:
            # Fallback: just use REQUIRE
            return f'{indent}REQUIRE( {inner} );'
        cond = inner[:last_comma].strip()
        msg = inner[last_comma + 2:].strip()
        return f'{indent}INFO( {msg} );\n{indent}REQUIRE( {cond} );'

    content = re.sub(
        r'^(\s*)PMM_TEST\(\s*(.+?)\s*\)\s*;',
        repl_two,
        content,
        flags=re.MULTILINE,
    )

    # 3. Convert void test functions
    def repl_func(m):
        fname = m.group(1)
        return f'TEST_CASE( "{fname}", "[{tag}]" )\n{{'

    content = re.sub(
        r'static\s+void\s+(test_\w+)\s*\(\s*\)\s*\n?\{',
        repl_func,
        content,
    )

    # 4. Remove main() section
    content = _remove_main_section(content)

    return content


def migrate_assert(content, tag):
    """Pattern C: assert()-based single-header tests."""
    # Replace assert( to REQUIRE(
    content = re.sub(r'\bassert\(\s*', 'REQUIRE( ', content)

    # Wrap main body in a TEST_CASE
    m = re.search(r'int\s+main\s*\(\s*\)\s*\n?\{', content)
    if m:
        content = content[:m.start()] + f'TEST_CASE( "{tag}", "[{tag}]" )\n{{' + content[m.end():]

    # Remove 'return 0;'
    content = re.sub(r'\n\s*return 0;\s*\n', '\n', content)

    # Remove cout lines
    content = re.sub(r'\s*std::cout\s*<<\s*"===.*?\\n"\s*;\s*\n', '\n', content)
    content = re.sub(r'\s*std::cout\s*<<\s*"PASSED.*?\\n"\s*;\s*\n', '\n', content)

    return content


def _remove_main_section(content):
    """Remove main() and its comment header to end of file."""
    # Try to find main section comment
    m = re.search(r'\n// [─\-]+ [Mm]ain [─\-]+\n', content)
    if m:
        return content[:m.start()].rstrip('\n') + '\n'

    # Fallback: find int main()
    m = re.search(r'\nint\s+main\s*\(\s*\)', content)
    if m:
        return content[:m.start()].rstrip('\n') + '\n'

    return content


def fix_includes(content):
    """Replace cassert with Catch2, remove iostream if unused."""
    # Add catch2 include
    if '#include <cassert>' in content:
        content = content.replace('#include <cassert>', '#include <catch2/catch_test_macros.hpp>')
    elif '#include <catch2/catch_test_macros.hpp>' not in content:
        # Add after first include
        m = re.search(r'(#include\s+[<"].*?[>"].*\n)', content)
        if m:
            content = content[:m.end()] + '#include <catch2/catch_test_macros.hpp>\n' + content[m.end():]

    # Remove iostream if no remaining uses
    if '#include <iostream>' in content:
        temp = content.replace('#include <iostream>', '')
        if 'std::cout' not in temp and 'std::cerr' not in temp and 'std::endl' not in temp:
            content = temp

    # Remove cstdlib if only used for std::exit
    if '#include <cstdlib>' in content:
        temp = content.replace('#include <cstdlib>\n', '')
        # Check if anything from cstdlib is used in remaining code
        stdlib_uses = re.findall(r'\bstd::(exit|abort|rand|srand|system|getenv|atoi|atof|strtol|strtod)\b', temp)
        if not stdlib_uses and 'EXIT_' not in temp:
            content = temp

    return content


def cleanup(content):
    """Remove old harness artifacts, collapse blank lines."""
    # Remove remaining PMM_RUN calls
    content = re.sub(r'\s*PMM_RUN\([^)]*\)\s*;\s*\n', '', content)
    # Remove bool all_passed
    content = re.sub(r'\s*bool\s+all_passed\s*=\s*true\s*;\s*\n', '', content)
    # Remove cout suite banner
    content = re.sub(r'\s*std::cout\s*<<\s*"===.*?===.*?\n', '', content)
    # Remove all_passed ternary cout
    content = re.sub(r'\s*std::cout\s*<<\s*\(\s*all_passed.*?\n', '', content)
    # Remove return all_passed
    content = re.sub(r'\s*return\s+all_passed.*?\n', '', content)
    # Remove any remaining "All ... PASSED" cout
    content = re.sub(r'\s*std::cout\s*<<\s*"\\n?All.*?PASSED.*?\n', '', content)
    # Remove test-macros comment
    content = re.sub(r'// ---+ Test macros ---+\n\n?', '', content)
    content = re.sub(r'// ─── Test helpers ─+\n\n?', '', content)
    # Collapse >2 blank lines
    content = re.sub(r'\n{4,}', '\n\n\n', content)
    # Ensure trailing newline
    content = content.rstrip('\n') + '\n'
    return content


def detect_style(content):
    if re.search(r'#define\s+PMM_TEST\s*\(\s*cond\s*,', content):
        return 'B'
    if re.search(r'#define\s+PMM_TEST\s*\(\s*expr\s*\)', content):
        return 'A'
    if re.search(r'#define\s+PMM_RUN', content):
        return 'A'
    if re.search(r'\bassert\s*\(', content) and 'int main' in content:
        return 'C'
    return None


def process(filepath, dry_run=False):
    content = read_file(filepath)
    tag = suite_name(filepath)
    style = detect_style(content)

    if style is None:
        print(f'  SKIP (no known pattern): {filepath}')
        return False

    if style == 'A':
        content = migrate_one_arg(content, tag)
    elif style == 'B':
        content = migrate_two_arg(content, tag)
    elif style == 'C':
        content = migrate_assert(content, tag)

    content = fix_includes(content)
    content = cleanup(content)

    if dry_run:
        print(f'  DRY-RUN ({style}): {filepath}  ({len(content.splitlines())} lines)')
    else:
        write_file(filepath, content)
        print(f'  OK ({style}): {filepath}  ({len(content.splitlines())} lines)')
    return True


def main():
    dry_run = '--dry-run' in sys.argv
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    test_dir = os.path.join(repo, 'tests')

    # Skip already-migrated and demo-only files
    skip = {
        'test_allocate.cpp',       # already migrated manually
        'test_demo_headless.cpp',  # demo-only, built with PMM_BUILD_DEMO
    }

    files = sorted(glob.glob(os.path.join(test_dir, 'test_*.cpp')))
    print(f'Found {len(files)} test files')

    ok = skipped = 0
    for f in files:
        if os.path.basename(f) in skip:
            print(f'  SKIP (excluded): {f}')
            skipped += 1
            continue
        if process(f, dry_run):
            ok += 1
        else:
            skipped += 1

    print(f'\nDone: {ok} migrated, {skipped} skipped')


if __name__ == '__main__':
    main()
