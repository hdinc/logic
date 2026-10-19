/*
 * messages between the program and the vpi bridge running inside an hdl
 * simulator. c, shared by both sides.
 *
 * every message is a 32 bit length (of what follows), a byte with the type
 * and the payload. numbers are little endian. values are the words of
 * bits, x and z (64 bit each, see logic_model.h) one after the other.
 * time is in picoseconds.
 */

#ifndef LOGIC_COSIM_PROTOCOL_H
#define LOGIC_COSIM_PROTOCOL_H

#define LOGIC_COSIM_VERSION 1

/* program -> bridge */
enum {
    /* u32 version, u32 port count, per port: u8 direction (0 in, 1 out,
     * 2 inout), u32 width, u16 name length, name */
    LOGIC_COSIM_INIT = 1,
    /* u64 time, values of the inputs and inouts in port order. the bridge
     * sets them and answers with STATE after they settled. */
    LOGIC_COSIM_SET = 2,
    /* u64 time: run until then, or until an output changes earlier. the
     * bridge answers with STATE at the time it stopped. */
    LOGIC_COSIM_RUN = 3,
    LOGIC_COSIM_QUIT = 4
};

/* bridge -> program */
enum {
    /* i32 time precision of the simulator (e.g. -12 for ps) */
    LOGIC_COSIM_READY = 101,
    /* u64 time, u8 finished, values the outputs and inouts drive in port
     * order. an inout bit the design doesn't drive is z. */
    LOGIC_COSIM_STATE = 102,
    /* text */
    LOGIC_COSIM_ERROR = 103
};

/* environment of the simulator process */
#define LOGIC_COSIM_ENV_MODE "LOGIC_COSIM_MODE" /* "describe" or "run" */
#define LOGIC_COSIM_ENV_PORT "LOGIC_COSIM_PORT" /* tcp port on 127.0.0.1 */
#define LOGIC_COSIM_ENV_TOP "LOGIC_COSIM_TOP"   /* top for describe, testbench for run */

#endif
