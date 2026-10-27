// for the web build, written by hand instead of by simavr's makefile: the
// table of the cores in sim_core_config.h, looked up by name
#ifndef __SIM_CORE_DECL_H__
#define __SIM_CORE_DECL_H__

#include "sim_core_config.h"

extern avr_kind_t mega328;
extern avr_kind_t tiny85;

extern avr_kind_t * avr_kind[];
#ifdef AVR_KIND_DECL
avr_kind_t * avr_kind[] = {
	&mega328,
	&tiny85,
	NULL
};
#endif
#endif
