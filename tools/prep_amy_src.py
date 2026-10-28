#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""X0X_AMY device build: AMY's sources, copied inside this tree.

    tools/prep_amy_src.py AMY_DIR OUT_DIR

The toolchain runs in a container that only sees this tree, so AMY's src/ is copied to OUT_DIR
(build/amy/src). To fit the 568 KB app slot, tools/build.py compiles them with AMY_NO_BUILTIN_PCM
(no built-in samples) and AMY_ONLY_PATCH=257 (AMYboard's default patch only: the FM-1 loads its
Juno presets onto it, as the AMYboard editor does: tools/gen_amy_k257.js), which need
shorepine/amy#1250 or later. miniaudio.h, the sample banks and the other platforms'
audio and MIDI files are not copied.
"""
import re
import shutil
import sys
from pathlib import Path

SKIP = re.compile(r"^(miniaudio\.h|pcm_samples_.*\.h|pcm_gamma.*\.h|libminiaudio-audio\.[ch]|linux_midi\.c|"
                  r"macos_midi\.m|i2s\.c|amy-example\.c|amy-message\.c|amy-piano\.c|.*\.o|.*\.os)$")
NEEDS = {"amy.c": "AMY_NO_BUILTIN_PCM", "patches.c": "AMY_ONLY_PATCH"}   # the build options it uses


def main(amy, out):
    src, out = Path(amy) / "src", Path(out)
    for f, opt in NEEDS.items():
        if opt not in (src / f).read_text():
            raise SystemExit(f"prep_amy_src: {src} has no {opt} (needs shorepine/amy#1250 or later)")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for f in sorted(src.iterdir()):
        if f.is_file() and f.suffix in (".c", ".h") and not SKIP.match(f.name):
            shutil.copy2(f, out / f.name)
    print(f"amy: {len(list(out.iterdir()))} files in {out}")


if __name__ == "__main__":
    main(*sys.argv[1:3])
