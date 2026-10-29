#!/bin/bash
# builds the rv32 isa tests of riscv-tests (github.com/riscv-software-src/riscv-tests,
# BSD license) into isa/ next to this script. the binaries are committed, so the
# tests don't need a toolchain. usage: build.sh <riscv-tests checkout>
# needs riscv32-unknown-linux-gnu-gcc, e.g. from "module load riscv-toolchain".
set -e
src=$(realpath "$1")
out=$(dirname "$(realpath "$0")")/isa
cc=${CC:-riscv32-unknown-linux-gnu-gcc}
strip=${STRIP:-riscv32-unknown-linux-gnu-strip}
mkdir -p "$out"
for suite in rv32ui rv32um rv32ua rv32mi rv32si; do
    # the names after "<suite>_sc_tests =" up to the first line without a backslash
    tests=$(awk '/_sc_tests =/ { on = 1; next } on { last = !/\\$/; gsub(/\\/, ""); print; if (last) exit }' "$src/isa/$suite/Makefrag")
    for t in $tests; do
        "$cc" -march=rv32ima_zicsr_zifencei -mabi=ilp32 -static -mcmodel=medany -fvisibility=hidden -nostdlib -nostartfiles \
            -I"$src/env/p" -I"$src/isa/macros/scalar" -T"$src/env/p/link.ld" \
            "$src/isa/$suite/$t.S" -o "$out/$suite-p-$t"
        "$strip" --strip-debug "$out/$suite-p-$t"
    done
done
