#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the X0X_AMY host simulator: the whole app with AMY (github.com/shorepine/amy) making the
# sound instead of X0X's engines (firmware/src/app/engine_amy.c), into build/host/x0x_amy_host.
#   AMY_DIR=/path/to/amy [X0X_AMY_PATCH=0] [X0X_AMY_VOLUME=2] [X0X_AMY_DEFS=-DX0X_AMY_LOG=1] host/build_host_amy.sh
# AMY's sources build with their own flags into build/host/amy/; the rest is host/build_host.sh.
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
[ -n "${AMY_DIR:-}" ] && [ -f "$AMY_DIR/src/amy.h" ] || { echo "build_host_amy.sh: set AMY_DIR to an amy checkout"; exit 1; }
grep -q AMY_ONLY_PATCH "$AMY_DIR/src/patches.c" || { echo "build_host_amy.sh: $AMY_DIR needs shorepine/amy#1250 or later"; exit 1; }
# as the device builds it (tools/build.py AMY_TRIM): no built-in samples, patch 257 only
AMY_TRIM="-DAMY_NO_BUILTIN_PCM -DAMY_ONLY_PATCH=257"
O=build/host/amy
mkdir -p "$O" build/gen
python3 tools/gen_amy_juno.py tools/amy_k257.json build/gen/amy_juno.h
AMY_SRC="algorithms amy envelope examples parse filters oscillators pcm interp_partials custom delay
         log2_exp2 patches transfer sequencer instrument amy_midi api midi_mappings cv_trigger note_output"
for f in $AMY_SRC; do
    $CC -O2 -ffp-contract=off -w $AMY_TRIM -I"$AMY_DIR/src" -c "$AMY_DIR/src/$f.c" -o "$O/$f.o"
done
$CC -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -Wno-unused-parameter -DX0X_HOST -DX0X_AMY ${X0X_AMY_DEFS:-} \
    -DX0X_AMY_PATCH="${X0X_AMY_PATCH:-0}" -DX0X_AMY_VOLUME="${X0X_AMY_VOLUME:-2}" -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen -I"$AMY_DIR/src" \
    -c firmware/src/app/engine_amy.c -o "$O/engine_amy.o"
# engine_amy.c in place of engine.c and the DSP units (see engine.h)
X0X_HOST_UNITS="firmware/src/seq/sequencer.c firmware/src/seq/tb3po.c firmware/src/seq/pattern.c firmware/src/seq/motion.c" \
    X0X_HOST_DEFS="-DX0X_AMY -DX0X_AMY_PATCH=${X0X_AMY_PATCH:-0} $O/*.o" X0X_HOST_OUT=build/host/x0x_amy_host sh host/build_host.sh >/dev/null
echo "host: build/host/x0x_amy_host (AMY $(git -C "$AMY_DIR" describe --tags --always 2>/dev/null || echo '?'), patch ${X0X_AMY_PATCH:-0})"
