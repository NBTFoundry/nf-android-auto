#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Writes legal/NOTICE.txt: the notices this application's package carries.

Ours (MPL-2.0); NBT-AAD's daemon, which the package contains, under the
PolyForm Noncommercial License with its Required Notice; the speexdsp
resampler NBT-AAD links; then the NBT App SDK's third-party notices
(NBT_SDK_DIR, default ../sdk). Run again when a dependency changes.
"""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get("NBT_SDK_DIR", ROOT.parent / "sdk"))
AAD = ROOT / "third_party/nbt-aad"
RULE = "\n" + "=" * 72 + "\n\n"

HEADER = """NBTFoundry

NBTFoundry nf-android-auto
Copyright (c) 2026 Fabien Poussin
Original software: MPL-2.0. You may redistribute and modify it under that license.
This software comes WITHOUT ANY WARRANTY, to the extent permitted by law.
Original NBTFoundry artwork: MPL-2.0. Trademark rights are separate.
Source: https://github.com/NBTFoundry/nf-android-auto
When redistributing binaries, include applicable notices and provide the source required by the component licenses.

The application's package contains NBT-AAD's daemon, licensed under the
PolyForm Noncommercial License 1.0.0: the package, as a whole, may be used
and distributed for noncommercial purposes only, on NBT-AAD's terms below.
NBT-AAD is free of charge; selling it, or bundling it with anything sold, is
not permitted.
"""


def required_notices():
    """NBT-AAD's "Required Notice:" lines, which every copy must carry."""
    text = (AAD / "LICENSE.md").read_text() + (AAD / "README.md").read_text()
    lines = sorted(set(re.findall(r"^(?:> )?(Required Notice: .+)$", text, re.M)))
    if not lines:
        sys.exit("no Required Notice line in NBT-AAD's LICENSE.md or README.md")
    return "\n".join(line.lstrip("> ") for line in lines)


def main():
    sdk = subprocess.run([sys.executable, str(SDK / "tools/third_party_notices.py")], check=True,
                         capture_output=True, text=True).stdout
    parts = [
        HEADER,
        "Head-unit risk and disclaimer\n\n" + (ROOT / "DISCLAIMER.md").read_text().split("\n", 1)[1],
        "MPL-2.0\n\n" + (ROOT / "LICENSES/MPL-2.0.txt").read_text(),
        "NBT-AAD, the Android Auto daemon (third_party/nbt-aad)\n"
        "https://github.com/Sq00ky/Supra-iDrive6-Android-Auto\n\n" + required_notices() + "\n\n"
        + (AAD / "LICENSE.md").read_text(),
        "speexdsp's resampler, used by NBT-AAD (third_party/nbt-aad/milestone-7/third_party/speexdsp)\n\n"
        + (AAD / "milestone-7/third_party/speexdsp/COPYING").read_text(),
        sdk,
    ]
    text = "\n\n" + RULE.join(p.rstrip("\n") + "\n" for p in parts)
    (ROOT / "legal/NOTICE.txt").write_text(text)
    print("legal/NOTICE.txt: %d sections" % len(parts))


if __name__ == "__main__":
    main()
