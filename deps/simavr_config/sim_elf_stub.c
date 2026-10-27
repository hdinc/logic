// the web build has no libelf. the avr plugin gets its firmware from the
// program (load_image), these are only reached through simavr's own
// loaders, which it doesn't call.
#include "sim_elf.h"

int elf_read_firmware(const char * file, elf_firmware_t * firmware)
{
	(void)file;
	(void)firmware;
	return -1;
}

void avr_load_firmware(avr_t * avr, elf_firmware_t * firmware)
{
	(void)avr;
	(void)firmware;
}
