#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Writes the package description for NBT Manager's tools/package.py
(release/package.args, committed: the release workflow signs from it): the
fixed part (release/package.base), then one --setting line per row of the
settings table (src/settings.def). With --check, fails when the file is not
what it would write.

    package_args.py [--check] release/package.base src/settings.def release/package.args
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from settings_table import read  # noqa: E402


HEADER = ("# Written by tools/package_args.py from release/package.base and src/settings.def\n"
          "# (make package-args): the package description for tools/package.py, one argument\n"
          "# per line. `make package` and the release workflow add the version, sequence, key,\n"
          "# output and executable.\n")


def main():
    args = sys.argv[1:]
    check = args[:1] == ["--check"]
    args = args[1:] if check else args
    if len(args) != 3:
        sys.exit(__doc__)
    fixed, table, out = args
    with open(fixed) as f:
        lines = [line.rstrip("\n") for line in f if not line.startswith("#")]
    for row in read(table):
        lines += ["--setting", "%s|%s|%s|%s" % (row.key, row.label, row.default, ",".join(row.choices))]
    text = HEADER + "\n".join(lines) + "\n"
    if check:
        with open(out) as f:
            if f.read() != text:
                sys.exit("%s is out of date: make package-args" % out)
        return
    with open(out, "w") as f:
        f.write(text)


if __name__ == "__main__":
    main()
