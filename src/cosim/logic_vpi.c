/*
 * vpi bridge between logic and an hdl simulator (icarus verilog, ghdl).
 * compiled by the program with the simulator's vpi tools and loaded into
 * the simulator, see cosim_protocol.h.
 *
 * describe mode prints the ports and parameters of the top and ends.
 * run mode connects to the program, which drives the inputs of a generated
 * testbench and reads its outputs. the simulator only runs when the
 * program asks it to, the bridge blocks inside callbacks in between.
 *
 * the testbench has for each port: in_<name> (reg) for inputs, out_<name>
 * (wire) for outputs, and drv_<name> (reg, driven weakly onto the port) and
 * io_<name> (the port's net) for inouts. the design's drive of an inout is
 * what's stronger than weak on the net.
 */

#include "cosim_protocol.h"

#include <vpi_user.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define BAD_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define BAD_SOCKET (-1)
#define closesocket close
#endif

/* ---- connection ---- */

static socket_t sock = BAD_SOCKET;

static int send_all(const void* data, size_t n)
{
    const char* p = (const char*)data;
    while (n > 0) {
        int k = (int)send(sock, p, (int)n, 0);
        if (k <= 0) return 0;
        p += k;
        n -= (size_t)k;
    }
    return 1;
}

static int recv_all(void* data, size_t n)
{
    char* p = (char*)data;
    while (n > 0) {
        int k = (int)recv(sock, p, (int)n, 0);
        if (k <= 0) return 0;
        p += k;
        n -= (size_t)k;
    }
    return 1;
}

/* a message being built */
static unsigned char* out_buf;
static size_t out_len, out_cap;

static void out_put(const void* data, size_t n)
{
    if (out_len + n > out_cap) {
        out_cap = (out_len + n) * 2 + 64;
        out_buf = (unsigned char*)realloc(out_buf, out_cap);
    }
    memcpy(out_buf + out_len, data, n);
    out_len += n;
}

static void out_begin(unsigned char type)
{
    out_len = 4;
    if (out_cap < 64) {
        out_cap = 64;
        out_buf = (unsigned char*)realloc(out_buf, out_cap);
    }
    out_buf[4] = type;
    out_len = 5;
}

static int out_send(void)
{
    uint32_t len = (uint32_t)(out_len - 4);
    memcpy(out_buf, &len, 4);
    return send_all(out_buf, out_len);
}

/* the last message received */
static unsigned char* in_buf;
static size_t in_len, in_pos;

static int receive(unsigned char* type)
{
    uint32_t len;
    if (!recv_all(&len, 4) || len < 1) return 0;
    in_buf = (unsigned char*)realloc(in_buf, len);
    if (!recv_all(in_buf, len)) return 0;
    *type = in_buf[0];
    in_len = len;
    in_pos = 1;
    return 1;
}

static int in_get(void* data, size_t n)
{
    if (in_pos + n > in_len) return 0;
    memcpy(data, in_buf + in_pos, n);
    in_pos += n;
    return 1;
}

static void fail(const char* message)
{
    vpi_printf("logic: %s\n", message);
    if (sock != BAD_SOCKET) {
        out_begin(LOGIC_COSIM_ERROR);
        out_put(message, strlen(message));
        out_send();
    }
    vpi_control(vpiFinish, 1);
}

/* ---- ports and values ---- */

struct port {
    int dir; /* 0 in, 1 out, 2 inout */
    int width;
    vpiHandle drive; /* in_ or drv_ */
    vpiHandle net;   /* out_ or io_ */
};

static struct port* ports;
static uint32_t port_count;
static int precision;

static int words_of(int width) { return (width + 63) / 64; }

/* puts the value of a port from the message */
static int put_value(struct port* p)
{
    int n = words_of(p->width);
    int vecs = (p->width + 31) / 32;
    uint64_t* w = (uint64_t*)malloc(sizeof(uint64_t) * 3 * (size_t)n);
    s_vpi_vecval* vec = (s_vpi_vecval*)calloc((size_t)vecs, sizeof(s_vpi_vecval));
    s_vpi_value v;
    int k, ok = in_get(w, sizeof(uint64_t) * 3 * (size_t)n);
    /* 0: a0 b0, 1: a1 b0, z: a0 b1, x: a1 b1 */
    for (k = 0; ok && k < vecs; k++) {
        int word = k / 2, shift = 32 * (k % 2);
        uint32_t bits = (uint32_t)(w[word] >> shift), x = (uint32_t)(w[n + word] >> shift), z = (uint32_t)(w[2 * n + word] >> shift);
        vec[k].aval = (PLI_INT32)(bits | x);
        vec[k].bval = (PLI_INT32)(x | z);
    }
    if (ok) {
        v.format = vpiVectorVal;
        v.value.vector = vec;
        vpi_put_value(p->drive, &v, NULL, vpiNoDelay);
    }
    free(w);
    free(vec);
    return ok;
}

/* adds what the design drives on a port to the message */
static void get_value(struct port* p)
{
    int n = words_of(p->width);
    uint64_t* w = (uint64_t*)calloc((size_t)3 * (size_t)n, sizeof(uint64_t));
    s_vpi_value v;
    int k;

    if (p->dir == 1) {
        v.format = vpiVectorVal;
        vpi_get_value(p->net, &v);
        for (k = 0; k < (p->width + 31) / 32; k++) {
            int word = k / 2, shift = 32 * (k % 2);
            uint64_t a = (uint32_t)v.value.vector[k].aval, b = (uint32_t)v.value.vector[k].bval;
            w[word] |= (a & ~b) << shift;
            w[n + word] |= (a & b) << shift;
            w[2 * n + word] |= (~a & b) << shift;
        }
    } else {
        /* bits driven stronger than weak, the rest is the testbench */
        v.format = vpiStrengthVal;
        vpi_get_value(p->net, &v);
        for (k = 0; k < p->width; k++) {
            s_vpi_strengthval* s = &v.value.strength[k];
            int strength = s->s0 > s->s1 ? s->s0 : s->s1;
            uint64_t bit = (uint64_t)1 << (k % 64);
            int word = k / 64;
            if (s->logic == vpiZ || strength <= vpiWeakDrive) w[2 * n + word] |= bit;
            else if (s->logic == vpiX) w[n + word] |= bit;
            else if (s->logic == vpi1) w[word] |= bit;
        }
    }
    /* clear what's above the width */
    if (p->width % 64) {
        uint64_t m = ((uint64_t)1 << (p->width % 64)) - 1;
        w[n - 1] &= m;
        w[2 * n - 1] &= m;
        w[3 * n - 1] &= m;
    }
    out_put(w, sizeof(uint64_t) * 3 * (size_t)n);
    free(w);
}

/* ---- time ---- */

static uint64_t now_units(void)
{
    s_vpi_time t;
    t.type = vpiSimTime;
    vpi_get_time(NULL, &t);
    return ((uint64_t)(uint32_t)t.high << 32) | (uint32_t)t.low;
}

static uint64_t pow10u(int e)
{
    uint64_t r = 1;
    while (e-- > 0)
        r *= 10;
    return r;
}

static uint64_t to_ps(uint64_t units)
{
    return precision >= -12 ? units * pow10u(precision + 12) : units / pow10u(-12 - precision);
}

static uint64_t to_units(uint64_t ps)
{
    return precision >= -12 ? ps / pow10u(precision + 12) : ps * pow10u(-12 - precision);
}

/* ---- running ---- */

static int armed;
static vpiHandle delay_cb;
static int finished;

static PLI_INT32 on_sync(p_cb_data cb);

static void register_sync(void)
{
    s_cb_data cb;
    static s_vpi_time t = {vpiSimTime, 0, 0, 0};
    memset(&cb, 0, sizeof(cb));
    cb.reason = cbReadWriteSynch;
    cb.cb_rtn = on_sync;
    cb.time = &t;
    vpi_register_cb(&cb);
}

static void send_state(int done)
{
    uint64_t t = to_ps(now_units());
    unsigned char f = (unsigned char)done;
    uint32_t k;
    out_begin(LOGIC_COSIM_STATE);
    out_put(&t, 8);
    out_put(&f, 1);
    for (k = 0; k < port_count; k++)
        if (ports[k].dir != 0) get_value(&ports[k]);
    out_send();
}

static PLI_INT32 on_delay(p_cb_data cb)
{
    (void)cb;
    delay_cb = NULL;
    if (armed) {
        armed = 0;
        register_sync();
    }
    return 0;
}

static PLI_INT32 on_change(p_cb_data cb)
{
    (void)cb;
    if (armed) {
        armed = 0;
        if (delay_cb) vpi_remove_cb(delay_cb);
        delay_cb = NULL;
        register_sync();
    }
    return 0;
}

/* everything at the current time happened: report and wait for a command */
static PLI_INT32 on_sync(p_cb_data cb)
{
    unsigned char type;
    uint64_t t;
    uint32_t k;
    (void)cb;

    if (finished) return 0;
    send_state(0);

    if (!receive(&type)) {
        /* the program is gone */
        vpi_control(vpiFinish, 0);
        return 0;
    }

    switch (type) {
    case LOGIC_COSIM_SET:
        in_get(&t, 8);
        for (k = 0; k < port_count; k++) {
            if (ports[k].dir != 1 && !put_value(&ports[k])) {
                fail("short SET message");
                return 0;
            }
        }
        register_sync();
        break;
    case LOGIC_COSIM_RUN: {
        uint64_t until, now = now_units();
        in_get(&t, 8);
        until = to_units(t);
        if (until <= now) {
            register_sync();
        } else {
            s_cb_data d;
            s_vpi_time dt;
            uint64_t delta = until - now;
            memset(&d, 0, sizeof(d));
            dt.type = vpiSimTime;
            dt.high = (PLI_UINT32)(delta >> 32);
            dt.low = (PLI_UINT32)delta;
            d.reason = cbAfterDelay;
            d.cb_rtn = on_delay;
            d.time = &dt;
            armed = 1;
            delay_cb = vpi_register_cb(&d);
        }
        break;
    }
    case LOGIC_COSIM_QUIT:
        finished = 1;
        vpi_control(vpiFinish, 0);
        break;
    default:
        fail("unknown message");
    }
    return 0;
}

static PLI_INT32 on_end(p_cb_data cb)
{
    (void)cb;
    if (sock != BAD_SOCKET && !finished) {
        finished = 1;
        send_state(1);
    }
    return 0;
}

static vpiHandle find(const char* top, const char* prefix, const char* name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s.%s%s", top, prefix, name);
    return vpi_handle_by_name(path, NULL);
}

static PLI_INT32 start_run(p_cb_data cb)
{
    const char* port_env = getenv(LOGIC_COSIM_ENV_PORT);
    const char* top = getenv(LOGIC_COSIM_ENV_TOP);
    struct sockaddr_in addr;
    unsigned char type;
    uint32_t version, k;
    int one = 1;
    s_cb_data d;
    (void)cb;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    if (!port_env || !top) {
        fail(LOGIC_COSIM_ENV_PORT " or " LOGIC_COSIM_ENV_TOP " not set");
        return 0;
    }
    sock = socket(AF_INET, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)atoi(port_env));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (sock == BAD_SOCKET || connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        sock = BAD_SOCKET;
        fail("can't connect to the program");
        return 0;
    }
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));

    if (!receive(&type) || type != LOGIC_COSIM_INIT || !in_get(&version, 4) || !in_get(&port_count, 4)) {
        fail("expected INIT");
        return 0;
    }
    if (version != LOGIC_COSIM_VERSION) {
        fail("the bridge is for another version of the program");
        return 0;
    }
    ports = (struct port*)calloc(port_count ? port_count : 1, sizeof(struct port));
    for (k = 0; k < port_count; k++) {
        unsigned char dir;
        uint32_t width;
        uint16_t len;
        char name[512];
        if (!in_get(&dir, 1) || !in_get(&width, 4) || !in_get(&len, 2) || len >= sizeof(name) || !in_get(name, len)) {
            fail("short INIT message");
            return 0;
        }
        name[len] = 0;
        ports[k].dir = dir;
        ports[k].width = (int)width;
        ports[k].drive = dir == 0 ? find(top, "in_", name) : dir == 2 ? find(top, "drv_", name) : NULL;
        ports[k].net = dir == 1 ? find(top, "out_", name) : dir == 2 ? find(top, "io_", name) : NULL;
        if ((dir != 1 && !ports[k].drive) || (dir != 0 && !ports[k].net)) {
            char m[600];
            snprintf(m, sizeof(m), "port %s not found in the testbench", name);
            fail(m);
            return 0;
        }
        /* stops a run when the design changes an output */
        if (ports[k].net) {
            static s_vpi_time t = {vpiSuppressTime, 0, 0, 0};
            static s_vpi_value v = {vpiSuppressVal, {0}};
            memset(&d, 0, sizeof(d));
            d.reason = cbValueChange;
            d.cb_rtn = on_change;
            d.obj = ports[k].net;
            d.time = &t;
            d.value = &v;
            vpi_register_cb(&d);
        }
    }

    precision = vpi_get(vpiTimePrecision, NULL);
    out_begin(LOGIC_COSIM_READY);
    out_put(&precision, 4);
    out_send();

    memset(&d, 0, sizeof(d));
    d.reason = cbEndOfSimulation;
    d.cb_rtn = on_end;
    vpi_register_cb(&d);

    register_sync();
    return 0;
}

/* ---- describe ---- */

static PLI_INT32 start_describe(p_cb_data cb)
{
    const char* top = getenv(LOGIC_COSIM_ENV_TOP);
    vpiHandle it, m, mod = NULL, p;
    (void)cb;

    it = vpi_iterate(vpiModule, NULL);
    while (it && (m = vpi_scan(it))) {
        if (top && strcmp(vpi_get_str(vpiName, m), top) == 0) mod = m;
    }
    if (!mod) {
        vpi_printf("LOGIC_ERROR top %s not found\n", top ? top : "(not set)");
        vpi_control(vpiFinish, 1);
        return 0;
    }

    it = vpi_iterate(vpiPort, mod);
    while (it && (p = vpi_scan(it))) {
        int dir = vpi_get(vpiDirection, p);
        vpi_printf("LOGIC_PORT %s %d %s\n", dir == vpiOutput ? "out" : dir == vpiInout ? "inout" : "in", vpi_get(vpiSize, p), vpi_get_str(vpiName, p));
    }

    it = vpi_iterate(vpiParameter, mod);
    while (it && (p = vpi_scan(it))) {
        int type = vpi_get(vpiConstType, p);
        s_vpi_value v;
        if (type == vpiStringConst) {
            v.format = vpiStringVal;
            vpi_get_value(p, &v);
            vpi_printf("LOGIC_PARAM string 0 0 %s %s\n", vpi_get_str(vpiName, p), v.value.str);
        } else if (type == vpiRealConst) {
            v.format = vpiRealVal;
            vpi_get_value(p, &v);
            vpi_printf("LOGIC_PARAM real 0 0 %s %.17g\n", vpi_get_str(vpiName, p), v.value.real);
        } else {
            v.format = vpiDecStrVal;
            vpi_get_value(p, &v);
            vpi_printf("LOGIC_PARAM int %d %d %s %s\n", vpi_get(vpiSize, p), vpi_get(vpiSigned, p), vpi_get_str(vpiName, p), v.value.str);
        }
    }

    vpi_printf("LOGIC_END\n");
    vpi_control(vpiFinish, 0);
    return 0;
}

static void startup(void)
{
    const char* mode = getenv(LOGIC_COSIM_ENV_MODE);
    s_cb_data cb;
    if (!mode) return;
    memset(&cb, 0, sizeof(cb));
    cb.reason = cbStartOfSimulation;
    cb.cb_rtn = strcmp(mode, "describe") == 0 ? start_describe : start_run;
    vpi_register_cb(&cb);
}

void (*vlog_startup_routines[])(void) = {startup, 0};
