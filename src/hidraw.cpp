#include "hidraw.h"

#include <cstdint>
#include <ctime>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

namespace
{
// Sysfs path of the controller's hid device. Mirrors
// phys_ctlr::get_led_path, which reaches the same directory for the
// leds group:
//   desktop: /sys/<devpath>/device/device
//   android: /sys/<devpath>/device
std::string hid_device_sysfs_path(std::string const &devpath)
{
#if defined(ANDROID) || defined(__ANDROID__)
    return std::string("/sys/") + devpath + "/device";
#else
    return std::string("/sys/") + devpath + "/device/device";
#endif
}

// joycon report layout, per the kernel's hid-nintendo driver
// (struct joycon_subcmd_request, see misc/hid-nintendo.c)
constexpr uint8_t OUTPUT_ID_SUBCMD = 0x01;
constexpr uint8_t INPUT_ID_SUBCMD_REPLY = 0x21;
constexpr uint8_t SUBCMD_SET_PLAYER_LIGHTS = 0x30;
constexpr uint8_t SUBCMD_ENABLE_IMU = 0x40;
constexpr int SUBCMD_MAX_ATTEMPTS = 3;
constexpr int SUBCMD_ACK_TIMEOUT_MS = 250;

bool send_subcmd(int fd, uint8_t packet_num, uint8_t subcmd_id, uint8_t data0)
{
    // see struct joycon_subcmd_request in hid-nintendo.c
    uint8_t report[12] = { 0 };
    report[0] = OUTPUT_ID_SUBCMD;
    report[1] = packet_num;
    report[10] = subcmd_id;
    report[11] = data0;
    return write(fd, report, sizeof(report)) == (ssize_t)sizeof(report);
}

bool wait_for_subcmd_ack(int fd, uint8_t subcmd_id)
{
    // https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/bluetooth_hid_subcommands_notes.md

    // The controller pushes an input report roughly every 15ms, so the
    // fd is almost always ready and select()'s own timeout never fires.
    // The deadline must therefore be measured against the clock: if the
    // ACK does not arrive in time, this attempt failed (e.g. the report
    // was dropped as a duplicate packet number, or while the driver's
    // own subcommands were in flight) and the caller retries with a
    // fresh number.
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    fd_set fds;
    uint8_t buf[64];

    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        unsigned long elapsed_ms =
            (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= SUBCMD_ACK_TIMEOUT_MS)
            return false;

        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        // elapsed_ms < SUBCMD_ACK_TIMEOUT_MS here, so this is non-negative
        struct timeval tv = { 0, (SUBCMD_ACK_TIMEOUT_MS - (int)elapsed_ms) * 1000 };
        if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0)
            return false;

        ssize_t n = read(fd, buf, sizeof(buf));
        // see struct joycon_input_report in kernel's hid-nintendo.c:
        // 13-byte fixed header (1st byte is id), then the joycon_subcmd_reply bytes
        if (n < 15)
            return false;
        // ACK bit set, echoing the requested subcommand id
        if (buf[0] == INPUT_ID_SUBCMD_REPLY && (buf[13] & 0x80) && buf[14] == subcmd_id)
            return true;
    }
}

// Send a single-argument subcommand to the hidraw node and wait for the
// controller's ACK.
bool send_acked_subcmd(std::string const &hidraw_node, uint8_t subcmd_id, uint8_t data0)
{
    int fd = open(hidraw_node.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "Failed to open " << hidraw_node << " ; errno=" << errno << std::endl;
        return false;
    }

    // The bound driver tracks the last packet number it sent, and the
    // controller can drop a report with a duplicate number, so retry
    // each attempt with a fresh one.
    bool acked = false;
    for (uint8_t pkt = 0; pkt < SUBCMD_MAX_ATTEMPTS && !acked; pkt++) {
        if (!send_subcmd(fd, pkt, subcmd_id, data0)) {
            std::cerr << "Failed to write subcommand report to " << hidraw_node << " ; errno=" << errno << std::endl;
            break;
        }
        acked = wait_for_subcmd_ack(fd, subcmd_id);
    }
    close(fd);
    return acked;
}
}

//public
std::optional<std::string> detect_hidraw_node(std::string const &devpath)
{
    char hid_dir[PATH_MAX];
    if (!realpath(hid_device_sysfs_path(devpath).c_str(), hid_dir))
        return std::nullopt;

    // Match the class entries against the resolved hid device directory
    // rather than scanning that directory: recent kernels nest the
    // hidraw node in a "hidraw" subdirectory of the hid device, where a
    // direct scan would match the subdirectory name instead of the node.
    std::string prefix = hid_dir;
    prefix += '/';

    DIR *dh = opendir("/sys/class/hidraw");
    if (!dh)
        return std::nullopt;

    std::optional<std::string> node;
    struct dirent *entry;
    while ((entry = readdir(dh)) != nullptr) {
        if (strncmp(entry->d_name, "hidraw", 6) != 0)
            continue;

        char node_path[PATH_MAX];
        if (!realpath((std::string("/sys/class/hidraw/") + entry->d_name).c_str(), node_path))
            continue;
        if (std::string(node_path).compare(0, prefix.size(), prefix) == 0) {
            node = std::string("/dev/") + entry->d_name;
            break;
        }
    }
    closedir(dh);
    return node;
}

//public
bool set_imu_enabled(std::string const &hidraw_node, bool enable)
{
    return send_acked_subcmd(hidraw_node, SUBCMD_ENABLE_IMU, enable ? 1 : 0);
}

//public
bool set_player_leds(std::string const &hidraw_node, uint8_t const (&leds)[4])
{
    // 0 = off (expected), 1 = on, 2 = flash; anything else is off
    uint8_t on = 0;
    uint8_t flash = 0;
    for (int i = 0; i < 4; i++) {
        if (leds[i] == 1)
            on |= uint8_t(1 << i);
        else if (leds[i] == 2)
            flash |= uint8_t(1 << i);
    }
    return send_acked_subcmd(hidraw_node, SUBCMD_SET_PLAYER_LIGHTS, uint8_t((flash << 4) | on));
}
