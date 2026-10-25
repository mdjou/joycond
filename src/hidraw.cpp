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

// joycon output report layout (mirrors hid-nintendo's joycon_subcmd_request)
constexpr uint8_t OUTPUT_ID_SUBCMD = 0x01;
constexpr uint8_t INPUT_ID_SUBCMD_REPLY = 0x21;
constexpr uint8_t SUBCMD_ENABLE_IMU = 0x40;
constexpr int IMU_CMD_MAX_ATTEMPTS = 3;
constexpr int IMU_CMD_ACK_TIMEOUT_MS = 250;

bool send_imu_command(int fd, uint8_t packet_num, bool enable)
{
    uint8_t report[12] = { 0 };
    report[0] = OUTPUT_ID_SUBCMD;
    report[1] = packet_num;
    report[10] = SUBCMD_ENABLE_IMU;
    report[11] = enable ? 1 : 0;
    return write(fd, report, sizeof(report)) == (ssize_t)sizeof(report);
}

bool wait_for_imu_ack(int fd)
{
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
        if (elapsed_ms >= IMU_CMD_ACK_TIMEOUT_MS)
            return false;

        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        // elapsed_ms < IMU_CMD_ACK_TIMEOUT_MS here, so this is non-negative
        struct timeval tv = { 0, (IMU_CMD_ACK_TIMEOUT_MS - (int)elapsed_ms) * 1000 };
        if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0)
            return false;

        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 3)
            return false;
        // ACK bit set, echoing the enable-IMU subcommand id
        if (buf[0] == INPUT_ID_SUBCMD_REPLY && (buf[1] & 0x80) && buf[2] == SUBCMD_ENABLE_IMU)
            return true;
    }
}

//public
bool set_imu_enabled(std::string const &hidraw_node, bool enable)
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
    for (uint8_t pkt = 0; pkt < IMU_CMD_MAX_ATTEMPTS && !acked; pkt++) {
        if (!send_imu_command(fd, pkt, enable)) {
            std::cerr << "Failed to write IMU report to " << hidraw_node << " ; errno=" << errno << std::endl;
            break;
        }
        acked = wait_for_imu_ack(fd);
    }
    close(fd);
    return acked;
}
