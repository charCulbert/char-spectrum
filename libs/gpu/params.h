#pragma once
#include "../core/messages.h"
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The plugin's parameters as the interface sees them: filled from the "params"
// and "values" messages, and edited with the begin/change/end messages the
// plugin turns into CLAP gesture and value events.
namespace gpu {

struct Param
{
    int id = -1;
    std::string name;
    double min = 0, max = 1, defaultValue = 0, value = 0;
    bool dragging = false;
};

class Params
{
public:
    // Handles "params" and "values"; returns false for other messages.
    bool receive(const core::Value &message);

    Param *find(std::string_view name);
    void begin(Param &p);
    void change(Param &p, double value); // clamps to the range
    void end(Param &p);

    std::function<void(const core::Value &)> send; // set to the View's send

private:
    std::vector<Param> params;
};

}
