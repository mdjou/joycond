#include "virt_ctlr_nyx.h"

#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <libevdev/libevdev-uinput.h>
#include <linux/uinput.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

//private
/* Rescale the Nyx's native 0-255 stick values (127 = center) to -32767..32767 */
static int nyx_stick_rescale(int value)
{
    if (value > 127)
        return (value - 127) * 32767 / 128;
    return (value - 127) * 32767 / 127;
}

void virt_ctlr_nyx::relay_events(std::shared_ptr<phys_ctlr> phys)
{
    struct input_event ev;
    struct libevdev *evdev = phys->get_evdev();

    int ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
    while (ret == LIBEVDEV_READ_STATUS_SYNC || ret == LIBEVDEV_READ_STATUS_SUCCESS) {
        if (ret == LIBEVDEV_READ_STATUS_SYNC) {
            std::cout << "handle sync\n";
            while (ret == LIBEVDEV_READ_STATUS_SYNC) {
                libevdev_uinput_write_event(uidev, ev.type, ev.code, ev.value);
                ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_SYNC, &ev);
            }
        } else if (ret == LIBEVDEV_READ_STATUS_SUCCESS) {

            /* Translate the Nyx's non-standard button codes to standard ones */
            if (ev.type == EV_KEY) {
                switch (ev.code) {
                    case BTN_BASE3:  ev.code = BTN_SELECT; break;
                    case BTN_BASE4:  ev.code = BTN_START;  break;
                    case BTN_THUMB:  ev.code = BTN_EAST;   break;
                    case BTN_TRIGGER:ev.code = BTN_SOUTH;  break;
                    case BTN_TOP:    ev.code = BTN_NORTH;  break;
                    case BTN_THUMB2: ev.code = BTN_WEST;   break;
                    case BTN_TOP2:   ev.code = BTN_TL;     break;
                    case BTN_PINKIE: ev.code = BTN_TR;     break;
                    case BTN_BASE:   ev.code = BTN_TL2;    break;
                    case BTN_BASE2:  ev.code = BTN_TR2;    break;
                    case BTN_BASE5:  ev.code = BTN_THUMBL; break;
                    case BTN_BASE6:  ev.code = BTN_THUMBR; break;
                    default: break;
                }
            }

            if (ev.type == EV_ABS) {
                /* The right stick is reported as ABS_Z/ABS_RZ natively */
                switch (ev.code) {
                    case ABS_Z:  ev.code = ABS_RX; break;
                    case ABS_RZ: ev.code = ABS_RY; break;
                    default: break;
                }

                /* Rescale the 0-255 stick values (127 = center) to -32767..32767 */
                switch (ev.code) {
                    case ABS_X:
                    case ABS_Y:
                    case ABS_RX:
                    case ABS_RY:
                        ev.value = nyx_stick_rescale(ev.value);
                        break;
                    default: break;
                }
            }

#if !defined(ANDROID) && !defined(__ANDROID__)
            /* Map the dpad HAT inputs to BTN_DPAD_* key outputs. */
            if (ev.type == EV_ABS && (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y)) {
                if (ev.code == ABS_HAT0X) {
                    libevdev_uinput_write_event(uidev, EV_KEY, BTN_DPAD_LEFT,  (ev.value < 0) ? 1 : 0);
                    libevdev_uinput_write_event(uidev, EV_KEY, BTN_DPAD_RIGHT, (ev.value > 0) ? 1 : 0);
                } else {
                    libevdev_uinput_write_event(uidev, EV_KEY, BTN_DPAD_UP,   (ev.value < 0) ? 1 : 0);
                    libevdev_uinput_write_event(uidev, EV_KEY, BTN_DPAD_DOWN, (ev.value > 0) ? 1 : 0);
                }
                ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                continue;
            }
#endif

            /* Swap A/B and X/Y if requested in the config */
            if (config.swap_ab_xy && ev.type == EV_KEY) {
                switch (ev.code) {
                    case BTN_SOUTH: ev.code = BTN_EAST; break;
                    case BTN_EAST:  ev.code = BTN_SOUTH; break;
                    case BTN_WEST:  ev.code = BTN_NORTH; break;
                    case BTN_NORTH: ev.code = BTN_WEST; break;
                    default: break;
                }
            }
            libevdev_uinput_write_event(uidev, ev.type, ev.code, ev.value);
        }
        ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
    }
}

//public
virt_ctlr_nyx::virt_ctlr_nyx(std::shared_ptr<phys_ctlr> phys, epoll_mgr& epoll_manager, joycond_config const &config) :
    phys(phys),
    epoll_manager(epoll_manager),
    config(config),
    subscriber(nullptr),
    virt_evdev(nullptr),
    uidev(nullptr),
    uifd(-1),
    rumble_effects(),
    mac(phys->get_mac_addr())
{
    int ret;

    uifd = open("/dev/uinput", O_RDWR);
    if (uifd < 0) {
        std::cerr << "Failed to open uinput; errno=" << errno << std::endl;
        exit(1);
    }

    // Create a virtual evdev on which the uinput will be based
    virt_evdev = libevdev_new();
    if (!virt_evdev) {
        std::cerr << "Failed to create virtual evdev\n";
        exit(1);
    }

    libevdev_set_name(virt_evdev, "Nintendo Switch Virtual Nyx Controller");

    // Make sure that all of this configuration remains in sync with the hid-nintendo driver.
    libevdev_enable_event_type(virt_evdev, EV_KEY);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_SELECT, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_Z, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_THUMBL, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_START, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_MODE, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_THUMBR, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_SOUTH, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_EAST, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_NORTH, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_WEST, NULL);
#if !defined(ANDROID) && !defined(__ANDROID__)
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_DPAD_UP, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_DPAD_DOWN, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_DPAD_LEFT, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_DPAD_RIGHT, NULL);
#endif
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_TL, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_TR, NULL);
#if !defined(ANDROID) && !defined(__ANDROID__)
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_TL2, NULL);
    libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_TR2, NULL);
#endif

    struct input_absinfo absconfig = { 0 };
    absconfig.minimum = -32767;
    absconfig.maximum = 32767;
    absconfig.fuzz = 250;
    absconfig.flat = 500;
    libevdev_enable_event_type(virt_evdev, EV_ABS);
    libevdev_enable_event_code(virt_evdev, EV_ABS, ABS_X, &absconfig);
    libevdev_enable_event_code(virt_evdev, EV_ABS, ABS_Y, &absconfig);
    libevdev_enable_event_code(virt_evdev, EV_ABS, ABS_RX, &absconfig);
    libevdev_enable_event_code(virt_evdev, EV_ABS, ABS_RY, &absconfig);

#if defined(ANDROID) || defined(__ANDROID__)
    // HAT for dpad
    struct input_absinfo dpad_absconfig = { 0 };
    dpad_absconfig.minimum = -1;
    dpad_absconfig.maximum = 1;
    dpad_absconfig.fuzz = 0;
    dpad_absconfig.flat = 0;
    libevdev_enable_event_code(virt_evdev, EV_ABS, ABS_HAT0X, &dpad_absconfig);
    libevdev_enable_event_code(virt_evdev, EV_ABS, ABS_HAT0Y, &dpad_absconfig);
#endif

    // Emulate analog triggers for android
#if defined(ANDROID) || defined(__ANDROID__)
    struct input_absinfo absconfig_fake = { 0 };
    absconfig_fake.minimum = 0;
    absconfig_fake.maximum = 1;
    absconfig_fake.fuzz = 0;
    absconfig_fake.flat = 0;

    libevdev_enable_event_code(virt_evdev, EV_ABS, ABS_Z, &absconfig_fake);
    libevdev_enable_event_code(virt_evdev, EV_ABS, ABS_RZ, &absconfig_fake);
#endif
    // libevdev_enable_event_type(virt_evdev, EV_FF);
    // libevdev_enable_event_code(virt_evdev, EV_FF, FF_RUMBLE, NULL);
    // libevdev_enable_event_code(virt_evdev, EV_FF, FF_PERIODIC, NULL);
    // libevdev_enable_event_code(virt_evdev, EV_FF, FF_SQUARE, NULL);
    // libevdev_enable_event_code(virt_evdev, EV_FF, FF_TRIANGLE, NULL);
    // libevdev_enable_event_code(virt_evdev, EV_FF, FF_SINE, NULL);
    // libevdev_enable_event_code(virt_evdev, EV_FF, FF_GAIN, NULL);

    // Set the product information to a non-existent product info (but with virtual bus type)
    libevdev_set_id_vendor(virt_evdev, 0x57e);
    libevdev_set_id_product(virt_evdev, 0x2008); // HACK?
    libevdev_set_id_bustype(virt_evdev, BUS_VIRTUAL);
    libevdev_set_id_version(virt_evdev, 0x0000);

    // Enable LED events
    // libevdev_enable_event_type(virt_evdev, EV_LED);
    // libevdev_enable_event_code(virt_evdev, EV_LED, 0, NULL);
    // libevdev_enable_event_code(virt_evdev, EV_LED, 1, NULL);
    // libevdev_enable_event_code(virt_evdev, EV_LED, 2, NULL);
    // libevdev_enable_event_code(virt_evdev, EV_LED, 3, NULL);

    ret = libevdev_uinput_create_from_device(virt_evdev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
    if (ret) {
        std::cerr << "Failed to create libevdev_uinput; " << ret << std::endl;
        exit(1);
    }

    int flags = fcntl(get_uinput_fd(), F_GETFL, 0);
    fcntl(get_uinput_fd(), F_SETFL, flags | O_NONBLOCK);

    subscriber = std::make_shared<epoll_subscriber>(std::vector({get_uinput_fd()}),
                                                    [=](int event_fd){handle_events(event_fd);});
    epoll_manager.add_subscriber(subscriber);
}

virt_ctlr_nyx::~virt_ctlr_nyx()
{
    epoll_manager.remove_subscriber(subscriber);

    libevdev_uinput_destroy(uidev);
    close(uifd);
    libevdev_free(virt_evdev);
}

void virt_ctlr_nyx::handle_events(int fd)
{
    if (fd == phys->get_fd())
        relay_events(phys);
    else
        std::cerr << "fd=" << fd << " is an invalid fd for this virtual nyx controller\n";
}

bool virt_ctlr_nyx::contains_phys_ctlr(std::shared_ptr<phys_ctlr> const ctlr) const
{
    return phys == ctlr;
}

bool virt_ctlr_nyx::contains_phys_ctlr(char const *devpath) const
{
    return phys->get_devpath() == devpath;
}

bool virt_ctlr_nyx::contains_fd(int fd) const
{
    return phys->get_fd() == fd || libevdev_uinput_get_fd(uidev) == fd;
}

std::vector<std::shared_ptr<phys_ctlr>> virt_ctlr_nyx::get_phys_ctlrs()
{
    std::vector<std::shared_ptr<phys_ctlr>> ctlrs;
    ctlrs.push_back(phys);
    return ctlrs;
}

int virt_ctlr_nyx::get_uinput_fd()
{
    return libevdev_uinput_get_fd(uidev);
}

void virt_ctlr_nyx::remove_phys_ctlr(const std::shared_ptr<phys_ctlr> phys)
{
    std::cerr << "Don't support removing controllers to virtual procon\n";
    exit(EXIT_FAILURE);
}

void virt_ctlr_nyx::add_phys_ctlr(std::shared_ptr<phys_ctlr> phys)
{
    std::cerr << "Don't support re-adding controllers to virtual procon\n";
    exit(EXIT_FAILURE);
}

enum phys_ctlr::Model virt_ctlr_nyx::needs_model()
{
    enum phys_ctlr::Model model = phys_ctlr::Model::Unknown;
    return model;
}

