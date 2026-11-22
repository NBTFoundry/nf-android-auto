#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Writes the package description for NBT Manager's tools/package.py
(release/package.args, committed: the release workflow signs from it): the
fixed part (release/package.base), one --setting line per row of the settings
table (src/settings.def), then the NBT-AAD version the pin builds as a
--property (its NBT_AAD_BUILD, in the pinned src/main.cpp), which the phone app
shows before an install. With --check, fails when the file is not what it
would write (the pin moved without `make package-args`).

    package_args.py [--check] release/package.base src/settings.def UPSTREAM release/package.args

UPSTREAM is the pinned daemon's folder (third_party/nbt-aad/milestone-7).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from settings_table import read  # noqa: E402


HEADER = ("# Written by tools/package_args.py from release/package.base, src/settings.def and\n"
          "# the pinned NBT-AAD's version (make package-args): the package description for\n"
          "# tools/package.py, one argument per line. `make package` and the release workflow\n"
          "# add the version, sequence, key, output and executable.\n")


def main():
    args = sys.argv[1:]
    check = args[:1] == ["--check"]
    args = args[1:] if check else args
    if len(args) != 4:
        sys.exit(__doc__)
    fixed, table, upstream, out = args
    with open(fixed) as f:
        lines = [line.rstrip("\n") for line in f if not line.startswith("#")]
    for row in read(table):
        lines += ["--setting", "%s|%s|%s|%s" % (row.key, row.label, row.default, ",".join(row.choices))]
    with open(os.path.join(upstream, "src/main.cpp")) as f:
        build = re.search(r'^#define NBT_AAD_BUILD "v?([0-9][0-9A-Za-z.-]{0,40})"', f.read(), re.M)
    if not build:
        sys.exit("%s/src/main.cpp: no NBT_AAD_BUILD" % upstream)
    # First, so the phone lists it first (properties keep their order).
    lines = ["--property", "NBT-AAD|%s" % build.group(1)] + lines
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
