#ifndef JOYCOND_JOYCOND_CONFIG_H
#define JOYCOND_JOYCOND_CONFIG_H

#include <string>

enum class CombinedRumbleMode
{
    Both,
    Left,
    Right,
    Off
};

struct joycond_config
{
    double rumble_gain = 1.0;
    CombinedRumbleMode pair_rumble_mode = CombinedRumbleMode::Both;
    bool swap_ab_xy = false;
    bool disable_imu = false;
    bool thumb_btn_remap_1 = false;
};

// Parse a "key = value" config file from path. A missing file yields the
// defaults; malformed lines and unknown keys are reported on stderr and
// parsing continues.
joycond_config load_config(std::string const &path);

#endif
