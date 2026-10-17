#pragma once

#include "../item.h"

namespace logic::editor::circuit_editor {

class port : public item {
public:
    enum port_type {
        input = 0,
        output = 1,
        inout = 2
    };

    void draw(glm::mat4& vp, Hd::Graphics2D& p) override;
    void property_editor() override;
    port(port_type type, int number, std::string name);

    int get_number();
    port_type get_type();
    std::string get_name();

private:
    int number;
    port_type type;

    std::string name;

private:
    char name_buf[100];
    int type_;
};

inline int port::get_number() { return number; }

inline port::port_type port::get_type() { return type; }

inline std::string port::get_name() { return name; }

}