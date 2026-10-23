#ifndef JOYCOND_JOYCOND_CONFIG_H
#define JOYCOND_JOYCOND_CONFIG_H

#include <string>

// Which physical joy-con(s) receive forwarded rumble (EV_FF) events on
// the combined controller.
enum class CombinedRumbleMode
{
    Both,
    Left,
    Right,
    Off
};

struct joycond_config
{
    // Scale the magnitudes of uploaded rumble effects before they are
    // forwarded to the physical joy-cons (0.0 = off .. 1.0 = full).
    double rumble_gain = 1.0;
    // Which physical joy-con(s) receive forwarded rumble events on the
    // paired (combined) controller.
    CombinedRumbleMode pair_rumble_mode = CombinedRumbleMode::Both;
    // Swap the A/B and X/Y buttons on the combined controller.
    bool swap_ab_xy = false;
};

// Parse a "key = value" config file from path. A missing file yields the
// defaults; malformed lines and unknown keys are reported on stderr and
// parsing continues.
joycond_config load_config(std::string const &path);

#endif
