#pragma once

#include "value.h"

namespace logic::sim {

// arithmetic on known values of any width, two's complement for signed. x
// and z bits count as 0, callers check fully_known() first. results have
// the width of the first operand unless said otherwise, the second operand
// is extended or cut to it.

struct carry_result {
    value v;
    bool carry = false;
};

// a + b + carry_in, carry is the bit out of the top
carry_result add(const value& a, const value& b, bool carry_in = false);
// a - b - borrow_in, borrow is set if the true result is below 0 (unsigned)
carry_result sub(const value& a, const value& b, bool borrow_in = false);
value negate(const value& a);

// the full product, twice the width of a
value mul(const value& a, const value& b, bool is_signed);

struct div_result {
    value q;
    value r;
};
// truncating division like c: the remainder has the sign of a. b must not
// be 0. the most negative value divided by -1 wraps to itself.
div_result divmod(const value& a, const value& b, bool is_signed);

// shifts by n bits (any n >= 0), the width stays. sar copies the top bit,
// rotations go around by n modulo the width.
value shl(const value& a, int n);
value shr(const value& a, int n);
value sar(const value& a, int n);
value rotl(const value& a, int n);
value rotr(const value& a, int n);

// -1, 0 or 1 for a < b, a == b, a > b
int compare(const value& a, const value& b, bool is_signed);

int popcount(const value& a);

enum class extend_mode {
    zero,
    sign,
    one
};
// to the width: wider gets the new top bits by the mode, narrower keeps
// the low bits
value extend(const value& a, int width, extend_mode mode);

bool is_zero(const value& a);
bool top_bit(const value& a);

}
