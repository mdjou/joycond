#ifndef JOYCOND_HIDRAW_H
#define JOYCOND_HIDRAW_H

#include <optional>
#include <string>

// Resolve the /dev/hidrawN node of the controller whose evdev node lives
// at the given sysfs devpath (as stored in phys_ctlr, e.g.
// "devices/.../0019:057e2007.0005/input12/event4" on desktop,
// "/class/input/event4/device" on Android).
// Returns std::nullopt if no hidraw node can be found.
std::optional<std::string> detect_hidraw_node(std::string const &devpath);

// Turn the controller's IMU (gyro + accelerometer) stream on or off by
// sending the enable-IMU subcommand (0x40) to the hidraw node. While
// enabled, the controller pushes 36 bytes of IMU data in every input
// report (~15ms over BT). Returns true if the controller acknowledged.
bool set_imu_enabled(std::string const &hidraw_node, bool enable);

#endif
