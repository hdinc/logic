#pragma once

#include "../model/bundle.h"
#include "../model/param.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace logic::builtin::axi {

enum class protocol {
    axi4,
    lite
};

// the parameters of an axi bundle. lite has no ids, user signals, lengths
// or lock/cache/qos/region, side only switches its prot fields.
struct widths {
    int id = 4;
    int addr = 32;
    int data = 32;
    int user = 0;
    // lock, cache, prot, qos and region (lite: prot)
    bool side = true;
};

// "axi4" or "axi4_lite"
const char* bundle_name(protocol p);
// the bundle type with its parameters and fields, master view
model::bundle_type bundle(protocol p);

// the protocol choice and the widths shared by the axi parts
std::vector<model::param_def> params();
protocol protocol_of(const model::param_values& resolved);
widths widths_of(const model::param_values& resolved);
// the values of the bundle's parameters that differ from its defaults, as
// ports store them
model::param_values bundle_args(protocol p, const widths& w);

// bits [off, off + w) of a bundle value, w 0 if the field isn't there
struct sig {
    int off = 0;
    int w = 0;
};

// a channel's payload is the fields before its valid, they are contiguous
struct channel {
    int off = 0;
    int w = 0;
    int valid = 0;
    int ready = 0;
};

#define LOGIC_AXI_FIELDS(F)                                                                                                             \
    F(awid, O, "ID_W", nullptr)                                                                                                         \
    F(awaddr, O, "ADDR_W", "ADDR_W")                                                                                                    \
    F(awlen, O, "8", nullptr)                                                                                                           \
    F(awsize, O, "3", nullptr)                                                                                                          \
    F(awburst, O, "2", nullptr)                                                                                                         \
    F(awlock, O, "SIDE", nullptr)                                                                                                       \
    F(awcache, O, "4 * SIDE", nullptr)                                                                                                  \
    F(awprot, O, "3 * SIDE", "3 * SIDE")                                                                                                \
    F(awqos, O, "4 * SIDE", nullptr)                                                                                                    \
    F(awregion, O, "4 * SIDE", nullptr)                                                                                                 \
    F(awuser, O, "USER_W", nullptr)                                                                                                     \
    F(awvalid, O, "1", "1")                                                                                                             \
    F(awready, I, "1", "1")                                                                                                             \
    F(wdata, O, "DATA_W", "DATA_W")                                                                                                     \
    F(wstrb, O, "DATA_W / 8", "DATA_W / 8")                                                                                             \
    F(wlast, O, "1", nullptr)                                                                                                           \
    F(wuser, O, "USER_W", nullptr)                                                                                                      \
    F(wvalid, O, "1", "1")                                                                                                              \
    F(wready, I, "1", "1")                                                                                                              \
    F(bid, I, "ID_W", nullptr)                                                                                                          \
    F(bresp, I, "2", "2")                                                                                                               \
    F(buser, I, "USER_W", nullptr)                                                                                                      \
    F(bvalid, I, "1", "1")                                                                                                              \
    F(bready, O, "1", "1")                                                                                                              \
    F(arid, O, "ID_W", nullptr)                                                                                                         \
    F(araddr, O, "ADDR_W", "ADDR_W")                                                                                                    \
    F(arlen, O, "8", nullptr)                                                                                                           \
    F(arsize, O, "3", nullptr)                                                                                                          \
    F(arburst, O, "2", nullptr)                                                                                                         \
    F(arlock, O, "SIDE", nullptr)                                                                                                       \
    F(arcache, O, "4 * SIDE", nullptr)                                                                                                  \
    F(arprot, O, "3 * SIDE", "3 * SIDE")                                                                                                \
    F(arqos, O, "4 * SIDE", nullptr)                                                                                                    \
    F(arregion, O, "4 * SIDE", nullptr)                                                                                                 \
    F(aruser, O, "USER_W", nullptr)                                                                                                     \
    F(arvalid, O, "1", "1")                                                                                                             \
    F(arready, I, "1", "1")                                                                                                             \
    F(rid, I, "ID_W", nullptr)                                                                                                          \
    F(rdata, I, "DATA_W", "DATA_W")                                                                                                     \
    F(rresp, I, "2", "2")                                                                                                               \
    F(rlast, I, "1", nullptr)                                                                                                           \
    F(ruser, I, "USER_W", nullptr)                                                                                                      \
    F(rvalid, I, "1", "1")                                                                                                              \
    F(rready, O, "1", "1")

// where the fields are in a bundle value, for the parts' simulation
struct layout {
#define LOGIC_AXI_SIG(name, dir, axi4, lite) sig name;
    LOGIC_AXI_FIELDS(LOGIC_AXI_SIG)
#undef LOGIC_AXI_SIG
    channel aw, w, b, ar, r;
    int total = 0;
    protocol proto = protocol::axi4;
};
layout make_layout(protocol p, const widths& w);

// an address range of a slave
struct range {
    uint64_t base = 0;
    uint64_t size = 0;
};
// "0x0000/4K, 0x1000/4K": a base/size per slave, sizes with K, M, G or T
// and '_' between digits. empty text splits the address space into equal
// power of two slices. nullopt with an error if the count doesn't match,
// ranges overlap or don't fit in addr_bits.
std::optional<std::vector<range>> parse_map(const std::string& text, int slaves, int addr_bits, std::string& error);
// "0x8000_0000/2G, 0x1000/4K": any number of base/size ranges, empty text
// is none. nullopt with an error if one doesn't fit or two overlap; name
// prefixes the ranges' indexes in the error ("m" gives "m0 and m1").
// overlaps false doesn't check them.
std::optional<std::vector<range>> parse_ranges(const std::string& text, int addr_bits, std::string& error, const char* name = "", bool overlaps = true);
// a number with '_' between digits, hex with 0x, and a K/M/G/T suffix
std::optional<uint64_t> parse_number(std::string s);

}
