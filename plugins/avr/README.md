# AVR microcontrollers (logic_avr)

ATmega328P (Arduino Uno) and ATtiny85, simulated by
[simavr](https://github.com/buserror/simavr) (GPL-3). The plugin links the
system's simavr found with pkg-config. Without it the plugin isn't built and
cells that use it report the plugin as missing.

- Linux: install simavr (Arch: AUR `simavr`, Debian/Ubuntu: `libsimavr-dev`).
- Windows: there's no MSYS2 package, simavr has to be built by hand.

## Parts

Every I/O pin is a 1-bit inout port named after its port bit (`PB5`), the
pin's description gives its other functions and the Arduino name. `reset_n`
at 0 holds the chip in reset; it starts like after power on when let go.

Parameters:

- `firmware`: elf or intel hex from avr-gcc (s-records and binary work too).
  The `.eeprom` section of an elf file goes into the eeprom.
- `frequency`: the cpu clock in Hz (16 MHz / 8 MHz by default). It replaces
  the fuses and clock sources.
- `run_ahead`: how far the chip may run in front of the simulation, 10 µs by
  default. Outputs change at their exact time, inputs are seen up to this
  much late. Larger is faster.

The ATmega328P's USART0 is on its pins: TXD (`PD1`) sends 8N1 frames at the
baud rate set in UBRR0, RXD (`PD0`) receives them. It works with the builtin
terminal.

The debugger shows flash, the data space (r0–r31 are its first 32 bytes, then
the I/O registers and the sram) and the eeprom, the program counter, a
disassembly, breakpoints (byte addresses like avr-objdump) and single steps.

## Not simulated

- Analog: the ADC and the analog comparator see no voltages, pins are digital.
- SPI, TWI (I2C) and USI aren't on the pins, simavr only has them as bytes.
- Fuses and clock sources.
