# riscv_linux

The files of the `riscv_linux` example schematic (`../cells/riscv_linux.json`):
an RV32IMA cpu with Sv32, a crossbar, a 16550 UART, a CLINT, a PLIC and
128 MiB of RAM boot OpenSBI, Linux and a busybox shell. Open the cell, press F5,
tick **Max** and run; the shell's prompt shows in the UART's terminal window after
about 15 s.

| File | What | Loaded at |
|---|---|---|
| `fw_jump.bin` | OpenSBI, fw_jump firmware (BSD-2-Clause) | 0x80000000 |
| `Image` | Linux 7.2 for rv32ima, S mode (GPL-2.0) | 0x80400000 |
| `rootfs.cpio` | busybox initramfs (GPL-2.0) | 0x86000000 |
| `soc.dtb` | the device tree, from `soc.dts` | 0x87000000 |

The images are the prebuilt ones of the `smode` branch of
[riscv-simulator](https://github.com/uwrk9amtdn/riscv-simulator) (`images/s-mode`),
where the kernel and buildroot configurations are too. The RAM part loads them
through its `images` parameter; the cpu starts at 0x80000000 with a0 = 0 (hart id)
and a1 = 0x87000000 (the device tree).

Memory map, the crossbar's `map` parameter:

| Base | Size | Part |
|---|---|---|
| 0x01000000 | 4 KiB | UART (PLIC source 1) |
| 0x02000000 | 64 KiB | CLINT |
| 0x03000000 | 4 MiB | PLIC |
| 0x80000000 | 128 MiB | RAM |

Rebuilding the device tree after changing `soc.dts` (e.g. another initrd, whose
end is in `/chosen`): `dtc -O dtb -o soc.dtb soc.dts`.
