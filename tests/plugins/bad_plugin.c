/* a plugin that works with no abi version */

#include "logic_model.h"

LOGIC_EXPORT const logic_plugin* logic_plugin_entry(uint32_t host_abi_version)
{
    (void)host_abi_version;
    return 0;
}
