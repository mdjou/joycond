#ifndef JOYCOND_HIDRAW_H
#define JOYCOND_HIDRAW_H

#include <cstdint>
#include <optional>
#include <string>

// Resolve the /dev/hidrawN node of the controller whose evdev node lives
// at the given sysfs devpath (as stored in phys_ctlr, e.g.
// "devices/.../0019:057e2007.0005/input12/event4" on desktop,
// "/class/input/event4/device" on Android).
// Returns std::nullopt if no hidraw node can be found.
std::optional<std::string> detect_hidraw_node(std::string const &devpath);

// Turn the controller's IMU (gyro + accelerometer) on or off.
// Returns true if the controller acknowledged.
bool set_imu_enabled(std::string const &hidraw_node, bool enable);

// Set all four player LEDs at once with a single SET_PLAYER_LIGHTS (0x30)
// subcommand. Each element: 0 = off, 1 = on, 2 = flash;
bool set_player_leds(std::string const &hidraw_node, uint8_t const (&leds)[4]);

#endif
