#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""X0X_AMY device build: AMY's sources, copied inside this tree and trimmed to fit the FM-1.

    tools/prep_amy_src.py AMY_DIR OUT_DIR

The toolchain runs in a container that only sees this tree, so AMY's src/ is copied to OUT_DIR
(build/amy/src). Two of its tables are replaced on the way, because the app slot is 568 KB:
  patches.h    only patch 257, AMYboard's default (the FM-1 loads its Juno presets onto it, as the
               AMYboard editor does: tools/gen_amy_k257.js); not the Juno or DX7 patches
  pcm_tiny.h   no built-in PCM samples (AMY skips pcm_init when pcm_samples is 0)
miniaudio.h, the sample banks and the other platforms' audio and MIDI files are not copied.
"""
import re
import shutil
import sys
from pathlib import Path

KEEP = 257                             # the only patch the FM-1 loads (amy_k257.json's base_patch)
SKIP = re.compile(r"^(miniaudio\.h|pcm_samples_.*\.h|pcm_gamma.*\.h|libminiaudio-audio\.[ch]|linux_midi\.c|"
                  r"macos_midi\.m|i2s\.c|amy-example\.c|amy-message\.c|amy-piano\.c|.*\.o|.*\.os)$")

PCM_NONE = """/* prep_amy_src.py: no built-in PCM samples on the FM-1 (pcm_samples 0: pcm_init is skipped) */
#ifndef __PCM_H
#define __PCM_H
#define PCM_AMY_SAMPLE_RATE 22050
const int16_t pcm[1] PROGMEM = {0};
const uint16_t pcm_samples = 0;
const uint16_t pcm_wavetable_base = 0;
const uint16_t pcm_wavetable_samples = 0;
const uint32_t pcm_wavetable_len = 0;
const pcm_map_t pcm_map[1] PROGMEM = {{0, 0, 0, 0, 0}};
#endif  // __PCM_H
"""


def base_patch_only(src):
    """patch KEEP at its own number, every other entry NULL (AMY refuses a NULL patch)"""
    t = src.read_text()
    line = [ln for ln in t.splitlines() if re.match(r"^\t/\* %d: " % KEEP, ln)]
    if len(line) != 1:
        raise SystemExit(f"prep_amy_src: patch {KEEP} not found (once) in {src}")
    oscs = re.search(r"patch_oscs\[\d+\] PROGMEM = \{(.*?)\};", t, re.S)[1]
    oscs = [x for x in re.split(r"[\s,]+", oscs) if x][:KEEP + 1]
    n = KEEP + 1
    return (f"/* prep_amy_src.py: AMY's patches.h with patch {KEEP} only */\n"
            "#ifndef __PATCHESH\n#define __PATCHESH\n"
            f"#define _PATCHES_NUM_BUILTIN {n}\n"
            f"static const char * const patch_commands[{n}] PROGMEM = {{\n"
            + "\tNULL,\n" * KEEP + line[0].rstrip(",") + ",\n};\n"
            f"const uint16_t patch_oscs[{n}] PROGMEM = {{\n" + ",".join(oscs) + ",\n};\n#endif\n")


def main(amy, out):
    src, out = Path(amy) / "src", Path(out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for f in sorted(src.iterdir()):
        if f.is_file() and f.suffix in (".c", ".h") and not SKIP.match(f.name):
            shutil.copy2(f, out / f.name)
    (out / "patches.h").write_text(base_patch_only(src / "patches.h"))
    (out / "pcm_tiny.h").write_text(PCM_NONE)
    print(f"amy: {len(list(out.iterdir()))} files in {out} (patch {KEEP} only, no PCM)")


if __name__ == "__main__":
    main(*sys.argv[1:3])
