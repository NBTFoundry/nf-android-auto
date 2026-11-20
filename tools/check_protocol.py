#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Checks what this adapter assumes about NBT-AAD's daemon against the pinned
sources, so that moving the pin to a release that changed one of them fails
the build instead of the car:

- every value in src/protocol.hpp with an "upstream: <file>" note is in that
  file (a quoted word, a path ending in it, or the number);
- every row of src/settings.def follows NBT Manager's setting rules, and its
  daemon variable or flag name, and each word it gives, are in the daemon's
  sources.

    check_protocol.py --upstream third_party/nbt-aad/milestone-7
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from settings_table import read  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FACT = re.compile(r'^\s*const\s+(?:char\s+(\w+)\[\]\s*=\s*"([^"]*)"|unsigned\s+(\w+)\s*=\s*(\d+))'
                  r'\s*;\s*//\s*upstream:\s*(\S+)')
KEY = re.compile(r"^[a-z][a-z0-9_]{0,15}$")
CHOICE = re.compile(r"^[a-z0-9_-]{1,12}$")
GENERIC_VALUES = {"-", "0", "1"}  # no word to find: unset, off, on


def has_word(text, word):
    return re.search(r'"(?:[^"\n]*/)?%s"' % re.escape(word), text) is not None


def sources(upstream):
    texts = []
    for base, _, names in os.walk(os.path.join(upstream, "src")):
        for name in names:
            if name.endswith((".cpp", ".hpp", ".h", ".c")):
                with open(os.path.join(base, name), errors="replace") as f:
                    texts.append(f.read())
    return "\n".join(texts)


def check_facts(upstream, errors):
    count = 0
    with open(os.path.join(ROOT, "src/protocol.hpp")) as f:
        for number, line in enumerate(f, 1):
            match = FACT.match(line)
            if not match:
                continue
            count += 1
            name = match.group(1) or match.group(3)
            path = os.path.join(upstream, os.path.relpath(match.group(5), "milestone-7"))
            try:
                with open(path, errors="replace") as source:
                    text = source.read()
            except OSError:
                errors.append("protocol.hpp:%d %s: no %s" % (number, name, path))
                continue
            found = has_word(text, match.group(2)) if match.group(1) else \
                re.search(r"\b%s\b" % match.group(4), text)
            if not found:
                errors.append("protocol.hpp:%d %s = %s: not in %s" % (
                    number, name, match.group(2) or match.group(4), match.group(5)))
    return count


def check_settings(upstream, errors):
    rows = read(os.path.join(ROOT, "src/settings.def"))
    text = sources(upstream)
    if not 1 <= len(rows) <= 12:
        errors.append("settings.def: %d rows (the manager takes 1 to 12)" % len(rows))
    keys = set()
    for row in rows:
        where = "settings.def %s" % row.key
        if not KEY.match(row.key) or row.key in keys:
            errors.append("%s: key not [a-z][a-z0-9_]{0,15} or repeated" % where)
        keys.add(row.key)
        if not (1 <= len(row.label) <= 24 and row.label == row.label.strip() and "|" not in row.label
                and row.label.isprintable() and row.label.isascii()):
            errors.append("%s: label must be 1-24 printable ASCII characters, no '|'" % where)
        if not 1 <= len(row.choices) <= 6 or len(set(row.choices)) != len(row.choices) or \
                not all(CHOICE.match(c) for c in row.choices):
            errors.append("%s: 1-6 distinct choices of [a-z0-9_-]{1,12}" % where)
        if row.default not in row.choices:
            errors.append("%s: default %r is not a choice" % (where, row.default))
        if len(row.values) != len(row.choices):
            errors.append("%s: %d values for %d choices" % (where, len(row.values), len(row.choices)))
        if row.kind not in ("AA_ENV", "AA_FLAG"):
            errors.append("%s: kind %s" % (where, row.kind))
        if row.kind == "AA_FLAG" and not set(row.values) <= {"0", "1"}:
            errors.append("%s: a flag's values are 0 or 1" % where)
        if not has_word(text, row.target):
            errors.append("%s: %s is not in the daemon's sources" % (where, row.target))
        for value in row.values:
            if value not in GENERIC_VALUES and not has_word(text, value):
                errors.append("%s: value %r is not in the daemon's sources" % (where, value))
    return len(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--upstream", required=True, help="NBT-AAD's milestone folder")
    args = parser.parse_args()
    errors = []
    facts = check_facts(args.upstream, errors)
    settings = check_settings(args.upstream, errors)
    if errors:
        print("The pinned NBT-AAD differs from what the adapter assumes:")
        print("\n".join("  " + e for e in errors))
        sys.exit(1)
    print("OK: %d protocol facts and %d settings match %s" % (facts, settings, args.upstream))


if __name__ == "__main__":
    main()
