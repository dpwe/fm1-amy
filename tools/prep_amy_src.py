#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""X0X_AMY device build: AMY's sources, copied inside this tree and trimmed to fit the FM-1.

    tools/prep_amy_src.py AMY_DIR OUT_DIR

The toolchain runs in a container that only sees this tree, so AMY's src/ is copied to OUT_DIR
(build/amy/src). Two of its tables are replaced on the way, because the app slot is 568 KB:
  patches.h    only the 128 Juno-60 patches (AMY numbers 0..127), not the DX7 banks
  pcm_tiny.h   no built-in PCM samples (AMY skips pcm_init when pcm_samples is 0)
miniaudio.h, the sample banks and the other platforms' audio and MIDI files are not copied.
"""
import re
import shutil
import sys
from pathlib import Path

NJUNO = 128
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


def juno_patches(src):
    t = src.read_text()
    entries = re.findall(r"^\t/\* (\d+): .*$", t, re.M)
    lines = [ln for ln in t.splitlines() if re.match(r"^\t/\* (\d+): ", ln) and int(re.match(r"^\t/\* (\d+)", ln)[1]) < NJUNO]
    if len(lines) != NJUNO or not entries:
        raise SystemExit(f"prep_amy_src: {len(lines)} Juno patches in {src}, expected {NJUNO}")
    oscs = re.search(r"patch_oscs\[\d+\] PROGMEM = \{(.*?)\};", t, re.S)[1]
    oscs = [x for x in re.split(r"[\s,]+", oscs) if x][:NJUNO]
    return ("/* prep_amy_src.py: AMY's patches.h, the Juno-60 patches only */\n"
            "#ifndef __PATCHESH\n#define __PATCHESH\n"
            f"#define _PATCHES_NUM_BUILTIN {NJUNO}\n"
            f"static const char * const patch_commands[{NJUNO}] PROGMEM = {{\n" + "\n".join(lines) + "\n};\n"
            f"const uint16_t patch_oscs[{NJUNO}] PROGMEM = {{\n" + ",".join(oscs) + ",\n};\n#endif\n")


def main(amy, out):
    src, out = Path(amy) / "src", Path(out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for f in sorted(src.iterdir()):
        if f.is_file() and f.suffix in (".c", ".h") and not SKIP.match(f.name):
            shutil.copy2(f, out / f.name)
    (out / "patches.h").write_text(juno_patches(src / "patches.h"))
    (out / "pcm_tiny.h").write_text(PCM_NONE)
    print(f"amy: {len(list(out.iterdir()))} files in {out} (Juno patches only, no PCM)")


if __name__ == "__main__":
    main(*sys.argv[1:3])
