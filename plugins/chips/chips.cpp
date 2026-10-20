// processors and peripheral chips with the cores of floooh/chips

#include "logic_model.hpp"

const logic_model_desc& chips_m6502();

namespace {

const logic_plugin* plugin()
{
    static const logic_model_desc models[] = {
        chips_m6502(),
    };
    static const logic_plugin p = {LOGIC_MODEL_ABI_VERSION, "logic_chips", models, sizeof(models) / sizeof(models[0])};
    return &p;
}

}

extern "C" LOGIC_EXPORT const logic_plugin* logic_plugin_entry(uint32_t host_abi_version)
{
    return host_abi_version == LOGIC_MODEL_ABI_VERSION ? plugin() : nullptr;
}
