# SPDX-License-Identifier: MPL-2.0
"""Reads src/settings.def, the application's one settings table (its format
is described at the top of that file)."""
import re
from collections import namedtuple

Row = namedtuple("Row", "key label default choices kind target values")

_ROW = re.compile(r"^AA_SETTING\((.*)\)$", re.S)
_ARG = re.compile(r'\s*("(?:[^"\\]|\\.)*"|[A-Za-z_][A-Za-z0-9_]*)\s*(?:,|$)')


def _args(text):
    out, pos = [], 0
    while pos < len(text):
        match = _ARG.match(text, pos)
        if not match:
            raise ValueError("cannot read %r" % text[pos:])
        token = match.group(1)
        out.append(token[1:-1] if token.startswith('"') else token)
        pos = match.end()
    return out


def read(path):
    """The rows, in order. A row may continue on the next lines."""
    rows, current = [], ""
    with open(path) as f:
        for line in f:
            text = line.strip()
            if not text or text.startswith("//"):
                continue
            current = current + " " + text if current else text
            if current.count("(") != current.count(")"):
                continue
            match = _ROW.match(current)
            if not match:
                raise ValueError("not a row: %r" % current)
            key, label, default, choices, kind, target, values = _args(match.group(1))
            rows.append(Row(key, label, default, choices.split(","), kind, target, values.split(",")))
            current = ""
    if current:
        raise ValueError("unfinished row: %r" % current)
    return rows
