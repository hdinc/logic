#include "gate.h"
#include "pin.h"

gate::~gate() = default;

gate::gate(const gate& other) {
    for (auto& p : pins) {
        p.g = this;
    }
}
