// avr microcontrollers with simavr: the parts, their pins and the plugin

#include "mcu.h"

namespace {

using avr::part;

const part atmega328p = {
    "atmega328p",
    "atmega328p",
    "ATmega328P (Arduino Uno) with simavr. Runs on its own clock and up to "
    "'run ahead' in front of the simulation: outputs change at their exact "
    "time, inputs are seen up to that much late.",
    16000000,
    {
        {'B', 0, "PB0", "PB0: ICP1, CLKO, Arduino D8"},
        {'B', 1, "PB1", "PB1: OC1A, Arduino D9"},
        {'B', 2, "PB2", "PB2: SS, OC1B, Arduino D10"},
        {'B', 3, "PB3", "PB3: MOSI, OC2A, Arduino D11"},
        {'B', 4, "PB4", "PB4: MISO, Arduino D12"},
        {'B', 5, "PB5", "PB5: SCK, Arduino D13 (the board's led)"},
        {'C', 0, "PC0", "PC0: ADC0 (digital only), Arduino A0"},
        {'C', 1, "PC1", "PC1: ADC1 (digital only), Arduino A1"},
        {'C', 2, "PC2", "PC2: ADC2 (digital only), Arduino A2"},
        {'C', 3, "PC3", "PC3: ADC3 (digital only), Arduino A3"},
        {'C', 4, "PC4", "PC4: SDA, Arduino A4"},
        {'C', 5, "PC5", "PC5: SCL, Arduino A5"},
        {'D', 0, "PD0", "PD0: RXD, Arduino D0"},
        {'D', 1, "PD1", "PD1: TXD, Arduino D1"},
        {'D', 2, "PD2", "PD2: INT0, Arduino D2"},
        {'D', 3, "PD3", "PD3: INT1, OC2B, Arduino D3"},
        {'D', 4, "PD4", "PD4: T0, Arduino D4"},
        {'D', 5, "PD5", "PD5: OC0B, Arduino D5"},
        {'D', 6, "PD6", "PD6: OC0A, Arduino D6"},
        {'D', 7, "PD7", "PD7: Arduino D7"},
    },
    '0',
    13,
    12,
};

const part attiny85 = {
    "attiny85",
    "attiny85",
    "ATtiny85 with simavr. Runs on its own clock and up to 'run ahead' in "
    "front of the simulation: outputs change at their exact time, inputs are "
    "seen up to that much late.",
    8000000,
    {
        {'B', 0, "PB0", "PB0: MOSI, SDA, OC0A, pin 5"},
        {'B', 1, "PB1", "PB1: MISO, OC0B, OC1A, pin 6"},
        {'B', 2, "PB2", "PB2: SCK, SCL, INT0, pin 7"},
        {'B', 3, "PB3", "PB3: ADC3 (digital only), pin 2"},
        {'B', 4, "PB4", "PB4: ADC2 (digital only), OC1B, pin 3"},
    },
    0,
    -1,
    -1,
};

template <const part& P>
struct model : avr::mcu {
    model(const logic_params& params, const logic_host& host)
        : avr::mcu(P, params, host)
    {
    }
};

template <const part& P>
int ports(const logic_params*, const logic_port_sink* sink)
{
    for (auto& pin : P.pins)
        logic_sdk::add_port(sink, pin.name, LOGIC_INOUT, 1, pin.description);
    logic_sdk::add_port(sink, "reset_n", LOGIC_INPUT, 1, "0 holds the chip in reset, it starts like after power on when let go");
    return 1;
}

template <const part& P>
const logic_param_desc* params()
{
    static const logic_param_desc d[] = {
        logic_sdk::file_param("firmware", "Firmware", "program: elf or intel hex from avr-gcc, s-records or binary"),
        logic_sdk::int_param("frequency", "Clock (Hz)", P.default_frequency, 1, 1000000000, "the cpu clock"),
        logic_sdk::int_param("run_ahead", "Run ahead (us)", 10, 0, 1000000,
            "how far the chip may run in front of the simulation: inputs are seen up to this late, larger is faster"),
    };
    return d;
}

template <const part& P>
logic_model_desc make()
{
    return logic_sdk::make_model<model<P>>(P.model, P.description, "Microcontrollers", params<P>(), 3, ports<P>);
}

const logic_plugin* plugin()
{
    static const logic_model_desc models[] = {
        make<atmega328p>(),
        make<attiny85>(),
    };
    static const logic_plugin p = {LOGIC_MODEL_ABI_VERSION, "logic_avr", models, sizeof(models) / sizeof(models[0])};
    return &p;
}

}

extern "C" LOGIC_EXPORT const logic_plugin* logic_plugin_entry(uint32_t host_abi_version)
{
    return host_abi_version == LOGIC_MODEL_ABI_VERSION ? plugin() : nullptr;
}
