# Test ROMs

- `6502_functional_test.bin`: Klaus Dormann's 6502 functional test
  (https://github.com/Klaus2m5/6502_65C02_functional_tests, commit
  7954e2dbb49c469ea286070bf46cdd71aeb29e4b, `bin_files/`), GPL-3.0. 64 KB
  loaded at 0, starts at 0x0400, reaches `jmp *` at 0x3469 when every test
  passes and loops at another address when one fails.
