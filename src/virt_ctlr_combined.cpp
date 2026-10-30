#include "virt_ctlr_combined.h"

#include <chrono>
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

namespace
{
/* Clear the button and axis states contributed by the left joy-con. */
void clear_left_ctlr_button_states(struct libevdev_uinput* uidev)
{
    libevdev_uinput_write_event(uidev, EV_ABS, ABS_X, 0);
    libevdev_uinput_write_event(uidev, EV_ABS, ABS_Y, 0);
#if defined(ANDROID) || defined(__ANDROID__)
    /* ZL and DPAD are remapped to analog trigger and HAT on android */
    libevdev_uinput_write_event(uidev, EV_ABS, ABS_Z, 0);
    libevdev_uinput_write_event(uidev, EV_ABS, ABS_HAT0X, 0);
    libevdev_uinput_write_event(uidev, EV_ABS, ABS_HAT0Y, 0);
#else
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_DPAD_UP, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_DPAD_DOWN, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_DPAD_LEFT, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_DPAD_RIGHT, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_TL2, 0);
#endif
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_SELECT, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_Z, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_THUMBL, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_TL, 0);
    /* Only relayed when not connected via serial, but zeroing is harmless otherwise */
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_TRIGGER_HAPPY1, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_TRIGGER_HAPPY2, 0);
    libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);
}

/* Clear the button and axis states contributed by the right joy-con. */
void clear_right_ctlr_button_states(struct libevdev_uinput* uidev)
{
    libevdev_uinput_write_event(uidev, EV_ABS, ABS_RX, 0);
    libevdev_uinput_write_event(uidev, EV_ABS, ABS_RY, 0);
#if defined(ANDROID) || defined(__ANDROID__)
    /* ZR is remapped to analog trigger on android */
    libevdev_uinput_write_event(uidev, EV_ABS, ABS_RZ, 0);
#else
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_TR2, 0);
#endif
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_START, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_MODE, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_THUMBR, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_SOUTH, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_EAST, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_NORTH, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_WEST, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_TR, 0);
    /* Only relayed when not connected via serial, but zeroing is harmless otherwise */
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_TRIGGER_HAPPY3, 0);
    libevdev_uinput_write_event(uidev, EV_KEY, BTN_TRIGGER_HAPPY4, 0);
    libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);
}

/* Scale an ff_effect's magnitudes by gain (0.0 = off .. 1.0 = full). */
void scale_ff_effect(struct ff_effect& effect, double gain)
{
    switch (effect.type) {
        case FF_RUMBLE:
            effect.u.rumble.strong_magnitude = (unsigned short)(effect.u.rumble.strong_magnitude * gain);
            effect.u.rumble.weak_magnitude = (unsigned short)(effect.u.rumble.weak_magnitude * gain);
            break;
        case FF_CONSTANT:
            effect.u.constant.level = (unsigned short)(effect.u.constant.level * gain);
            break;
        case FF_PERIODIC:
            effect.u.periodic.magnitude = (short)(effect.u.periodic.magnitude * gain);
            break;
        default:
            break;
    }
}

#ifdef JOYCOND_DEBUG
/* Return the elapsed ms since the last rumble, and update last_rumble to now if this is an EV_FF event. */
long long elapsed_since_last_rumble(const struct input_event& ev,
                                    std::chrono::steady_clock::time_point now,
                                    std::chrono::steady_clock::time_point& last_rumble)
{
    if (ev.type != EV_FF)
        return 0; // short path
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_rumble).count();
    if (ev.code < FF_GAIN) {
        last_rumble = now;
    }
    return elapsed_ms;
}

void debug_print_uinput_event(const struct input_event& ev, std::chrono::steady_clock::time_point now, long long rumble_elapsed_ms)
{
    auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if (ev.type == EV_FF) {
        std::cout << std::dec << ts_ms << "ms|uinput EV_FF: type=0x0015; code=0x" << std::hex << ev.code
                  << "; value=0x" << ev.value << "|" << std::dec << ev.value
                //   << "; time=0x" << std::dec << ev.time.tv_sec << "." << ev.time.tv_usec
                  << "; delta=" << rumble_elapsed_ms << "ms\n";
    } else if (ev.type == EV_UINPUT && ev.code == UI_FF_UPLOAD) {
        std::cout << std::dec << ts_ms << "ms|uinput EV_UINPUT/UI_FF_UPLOAD: type=0x0101; code=0x" << std::hex << ev.code
                  << "; value=0x" << ev.value << "|" << std::dec << ev.value
                //   << "; time=0x" << std::dec << ev.time.tv_sec << "." << ev.time.tv_usec
                  << "\n";
    } else if (ev.type == EV_UINPUT && ev.code == UI_FF_ERASE) {
        std::cout << std::dec << ts_ms << "ms|uinput EV_UINPUT/UI_FF_ERASE: type=0x0101; code=0x" << std::hex << ev.code
                  << "; value=0x" << ev.value << "|" << std::dec << ev.value
                //   << "; time=0x" << std::dec << ev.time.tv_sec << "." << ev.time.tv_usec
                  << "\n";
    } else if (ev.type == EV_LED) {
        std::cout << std::dec << ts_ms << "ms|uinput EV_LED: type=0x0011; code=0x" << std::hex << ev.code
                  << "; value=0x" << ev.value << "|" << std::dec << ev.value
                //   << "; time=0x" << std::dec << ev.time.tv_sec << "." << ev.time.tv_usec
                  << "\n";
    } else {
        std::cout << std::dec << ts_ms << "ms|uinput (unknown): type=0x" << std::hex << ev.type
                  << "; code=0x" << ev.code
                  << "; value=0x" << ev.value << "|" << std::dec << ev.value
                //   << "; time=0x" << std::dec << ev.time.tv_sec << "." << ev.time.tv_usec
                  << "\n";
    }
}

void debug_print_ff_effect(const struct ff_effect& effect, long long ts_ms)
{
    switch (effect.type) {
        case FF_RUMBLE:
            std::cout << std::dec << ts_ms << "ms|effect 0x" << std::hex << effect.type << " FF_RUMBLE"
                    << "; rumble.strong_magnitude=0x" << effect.u.rumble.strong_magnitude
                    << "; rumble.weak_magnitude=0x" << effect.u.rumble.weak_magnitude
                    // << "; replay.delay 0x" << effect.replay.delay
                    << "; replay.length 0x" << effect.replay.length
                    // << "; trigger.button 0x" << effect.trigger.button
                    // << "; trigger.interval 0x" << effect.trigger.interval
                    << std::dec << "\n";
            break;
        case FF_CONSTANT:
            std::cout << std::dec << ts_ms << "ms|effect 0x" << std::hex << effect.type << " FF_CONSTANT"
                    << "; constant.level=0x" << effect.u.constant.level
                    << "; constant.envelope.attack_length=0x" << effect.u.constant.envelope.attack_length
                    << "; constant.envelope.attack_level=0x" << effect.u.constant.envelope.attack_level
                    << "; constant.envelope.fade_length=0x" << effect.u.constant.envelope.fade_length
                    << "; constant.envelope.fade_level=0x" << effect.u.constant.envelope.fade_level
                    << std::dec << "\n";
            break;
        case FF_PERIODIC:
            std::cout << std::dec << ts_ms << "ms|effect 0x" << std::hex << effect.type << " FF_PERIODIC"
                    << "; periodic.magnitude=0x" << effect.u.periodic.magnitude
                    << "; periodic.period=0x" << effect.u.periodic.period
                    << "; periodic.offset=0x" << effect.u.periodic.offset
                    << "; periodic.phase=0x" << effect.u.periodic.phase
                    << "; periodic.waveform=0x" << effect.u.periodic.waveform
                    // (there's more fields, skipping for now)
                    << std::dec << "\n";
            break;
        case FF_HAPTIC:
            std::cout << std::dec << ts_ms << "ms|effect 0x" << std::hex << effect.type << " FF_HAPTIC"
                    << "; haptic.intensity=0x" << effect.u.haptic.intensity
                    << "; haptic.hid_usage=0x" << effect.u.haptic.hid_usage
                    << "; haptic.repeat_count=0x" << effect.u.haptic.repeat_count
                    << "; haptic.retrigger_period=0x" << effect.u.haptic.retrigger_period
                    << "; haptic.vendor_id=0x" << effect.u.haptic.vendor_id
                    << "; haptic.vendor_waveform_page=0x" << effect.u.haptic.vendor_waveform_page
                    << std::dec << "\n";
            break;
        case FF_SPRING:
        case FF_FRICTION:
        case FF_DAMPER:
        case FF_INERTIA:
        case FF_RAMP:
        default:
            std::cout << std::dec << ts_ms << "ms|effect type 0x" << std::hex << effect.type
            << "; direction 0x" << effect.direction
            << "; id 0x" << effect.id
            << "; replay.delay 0x" << effect.replay.delay
            << "; replay.length 0x" << effect.replay.length
            << "; trigger.button 0x" << effect.trigger.button
            << "; trigger.interval 0x" << effect.trigger.interval
            << std::dec << "\n";
            break;
    }
}
#endif
}

//private
void virt_ctlr_combined::relay_events(std::shared_ptr<phys_ctlr> phys)
{
    struct input_event ev;
    struct libevdev *evdev = phys->get_evdev();
    bool is_serial;

    int ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

    if (ret == -ENODEV) {
        if (phys == physl)
            clear_left_ctlr_button_states(uidev);
        else if (phys == physr)
            clear_right_ctlr_button_states(uidev);
        return;
    }

    while (ret == LIBEVDEV_READ_STATUS_SYNC || ret == LIBEVDEV_READ_STATUS_SUCCESS) {
        if (ret == LIBEVDEV_READ_STATUS_SYNC) {
            std::cout << "handle sync\n";
            while (ret == LIBEVDEV_READ_STATUS_SYNC) {
                libevdev_uinput_write_event(uidev, ev.type, ev.code, ev.value);
                ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_SYNC, &ev);
            }
        } else if (ret == LIBEVDEV_READ_STATUS_SUCCESS) {
            is_serial = phys->is_serial_ctlr();
            /* First remap the SL and SR buttons on each physical controller */
            if (phys == physl && ev.type == EV_KEY && (ev.code == BTN_TR || ev.code == BTN_TR2)) {
                if (!is_serial)
                    libevdev_uinput_write_event(uidev, ev.type, ev.code == BTN_TR ? BTN_TRIGGER_HAPPY1 : BTN_TRIGGER_HAPPY2, ev.value);
                ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                continue;
            } else if (phys == physr && ev.type == EV_KEY && (ev.code == BTN_TL || ev.code == BTN_TL2)) {
                if (!is_serial)
                    libevdev_uinput_write_event(uidev, ev.type, ev.code == BTN_TL ? BTN_TRIGGER_HAPPY3 : BTN_TRIGGER_HAPPY4, ev.value);
                ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                continue;
            }

#if defined(ANDROID) || defined(__ANDROID__)
            /* Second remap the ZL and ZR buttons to analog trigger and map the DPAD to a HAT on android */
            if (phys == physl && ev.type == EV_KEY && ev.code == BTN_TL2) {
                libevdev_uinput_write_event(uidev, EV_ABS, ABS_Z, ev.value);
                ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                continue;
            } else if (phys == physr && ev.type == EV_KEY && ev.code == BTN_TR2) {
                libevdev_uinput_write_event(uidev, EV_ABS, ABS_RZ, ev.value);
                ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                continue;
            }

            if (ev.type == EV_KEY) {
                switch (ev.code) {
                    case BTN_DPAD_UP:
                        libevdev_uinput_write_event(uidev, EV_ABS, ABS_HAT0Y, -ev.value);
                        ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                        continue;
                    case BTN_DPAD_DOWN:
                        libevdev_uinput_write_event(uidev, EV_ABS, ABS_HAT0Y, ev.value);
                        ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                        continue;
                    case BTN_DPAD_LEFT:
                        libevdev_uinput_write_event(uidev, EV_ABS, ABS_HAT0X, -ev.value);
                        ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                        continue;
                    case BTN_DPAD_RIGHT:
                        libevdev_uinput_write_event(uidev, EV_ABS, ABS_HAT0X, ev.value);
                        ret = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                        continue;
                    default:
                        break;
                }
            }
#endif
            /* Remap for for less thumbstick clicking */
            if (config.thumb_btn_remap_1) {
                switch (ev.code) {
                    case BTN_Z:  ev.code = BTN_THUMBL; break;
                    case BTN_MODE:  ev.code = BTN_THUMBR; break;
                    default: break;
                }
            }
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


void virt_ctlr_combined::handle_uinput_event()
{
    struct input_event ev;
    int ret;

    while ((ret = read(get_uinput_fd(), &ev, sizeof(ev))) == sizeof(ev)) {
        #ifdef JOYCOND_DEBUG
        auto now = std::chrono::steady_clock::now();
        debug_print_uinput_event(ev, now, elapsed_since_last_rumble(ev, now, last_rumble));
        #endif

        switch (ev.type) {
            case EV_FF:
                {
                    struct input_event redirectedl = ev;
                    struct input_event redirectedr = ev;

                    if (!rumble_effects.count(ev.code)) {
                        if (ev.code < FF_GAIN)
                            std::cerr << "ERROR: ff_effect with id=" << ev.code << " is not in map\n";
                    } else {
                        redirectedl.code = rumble_effects[ev.code].first.id;
                        redirectedr.code = rumble_effects[ev.code].second.id;
                    }

                    /* Forward this FF event on to the actual devices. Rumble play
                       events with valid effect_id (code < FF_GAIN) are skipped for the joy-con(s)
                       excluded by the configured rumble mode; other FF events
                       (e.g. the gain events the app sends at start) are always
                       forwarded. */
                    const bool play_event = ev.code < FF_GAIN;
                    const bool skip_l = play_event &&
                                       (config.pair_rumble_mode == CombinedRumbleMode::Off || config.pair_rumble_mode == CombinedRumbleMode::Right);
                    const bool skip_r = play_event &&
                                       (config.pair_rumble_mode == CombinedRumbleMode::Off || config.pair_rumble_mode == CombinedRumbleMode::Left);

                    if (physl && !skip_l) {
                        if (write(physl->get_fd(), &redirectedl, sizeof(redirectedl)) != sizeof(redirectedl))
                            std::cerr << "Failed to forward EV_FF to physl\n";
                    }
                    if (physr && !skip_r) {
                        if (write(physr->get_fd(), &redirectedr, sizeof(redirectedr)) != sizeof(redirectedr))
                            std::cerr << "Failed to forward EV_FF to physr\n";
                    }
                    break;
                }

            case EV_UINPUT:
                switch (ev.code) {
                    case UI_FF_UPLOAD:
                        {
                            struct uinput_ff_upload upload = { 0 };
                            struct ff_effect effect = { 0 };
                            struct ff_effect effect_l = { 0 };
                            struct ff_effect effect_r = { 0 };
                            bool allocate_new_effect;

                            upload.request_id = ev.value;
                            if (ioctl(get_uinput_fd(), UI_BEGIN_FF_UPLOAD, &upload))
                                std::cerr << "Failed to get uinput_ff_upload: " << strerror(errno) << std::endl;

                            #ifdef JOYCOND_DEBUG
                            // Debug
                            auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                            debug_print_ff_effect(upload.effect, ts_ms);
                            #endif

                            /* Scale the magnitudes down if requested in the config */
                            if (config.rumble_gain != 1.0)
                                scale_ff_effect(upload.effect, config.rumble_gain);

                            effect = upload.effect;
                            /* Check if already allocated or it needs an update */
                            allocate_new_effect = !rumble_effects.count(effect.id);
                            if (allocate_new_effect)
                                effect.id = -1;

                            /* upload the effect to both devices */
                            upload.retval = 0;
                            if (physl) {
                                if (ioctl(physl->get_fd(), EVIOCSFF, &effect) == -1)
                                    upload.retval = errno;
                                effect_l = effect;
                            }

                            if (physr) {
                                /* reset effect */
                                effect = upload.effect;
                                /* Check if already allocated or it needs an update */
                                if (allocate_new_effect)
                                    effect.id = -1;
                                // TEMPORARILY DISABLED R UP
                                if (ioctl(physr->get_fd(), EVIOCSFF, &effect) == -1)
                                    upload.retval = errno;
                                effect_r = effect;
                            }

                            upload.effect = effect;

                            if (upload.retval)
                                std::cerr << "UI_FF_UPLOAD failed: " << strerror(upload.retval) << std::endl;

                            if (allocate_new_effect)
                                rumble_effects[effect.id] = std::make_pair(effect_l, effect_r);

                            if (ioctl(get_uinput_fd(), UI_END_FF_UPLOAD, &upload))
                                std::cerr << "Failed to end uinput_ff_upload: " << strerror(errno) << std::endl;

                            break;
                        }
                    case UI_FF_ERASE:
                        {
                            struct uinput_ff_erase erase = { 0 };

                            erase.request_id = ev.value;
                            if (ioctl(get_uinput_fd(), UI_BEGIN_FF_ERASE, &erase))
                                std::cerr << "Failed to get uinput_ff_erase: " << strerror(errno) << std::endl;

                            erase.retval = 0;
                            if (physl) {
                                if (ioctl(physl->get_fd(), EVIOCRMFF, erase.effect_id) == -1)
                                    erase.retval = errno;
                            }
                            if (physr) {
                                if (ioctl(physr->get_fd(), EVIOCRMFF, erase.effect_id) == -1)
                                    erase.retval = errno;
                            }

                            if (erase.retval)
                                std::cerr << "UI_FF_ERASE failed: " << strerror(erase.retval) << std::endl;
                            else if (!rumble_effects.count(erase.effect_id))
                                std::cerr << "WARNING: effect_id not in rumble_effects map\n";
                            else
                                rumble_effects.erase(erase.effect_id);

                            if (ioctl(get_uinput_fd(), UI_END_FF_ERASE, &erase))
                                std::cerr << "Failed to end uinput_ff_erase: " << strerror(errno) << std::endl;

                            break;
                        }
                    default:
                        std::cerr << "Unhandled EV_UNINPUT code=" << ev.code <<std::endl;
                        break;
                }
                break;

            case EV_LED:
                if (ev.value == 0) {
                    libevdev_uinput_write_event(uidev, EV_LED, ev.code, !ev.value);
                }
                break;

            default:
                std::cerr << "Unhandled uinput type=" << ev.type << std::endl;
                break;
        }
    }
    if (ret < 0 && errno != EAGAIN) {
        std::cerr << "Failed reading uinput fd; ret=" << strerror(errno) << std::endl;
    } else if (ret > 0) {
        std::cerr << "Uinput incorrect read size of " << ret << std::endl;
    }
}

//public
virt_ctlr_combined::virt_ctlr_combined(std::shared_ptr<phys_ctlr> physl, std::shared_ptr<phys_ctlr> physr, epoll_mgr& epoll_manager, joycond_config const &config) :
    physl(physl),
    physr(physr),
    epoll_manager(epoll_manager),
    config(config),
    subscriber(nullptr),
    virt_evdev(nullptr),
    uidev(nullptr),
    uifd(-1),
    rumble_effects(),
    left_mac(physl->get_mac_addr()),
    right_mac(physr->get_mac_addr())
#ifdef JOYCOND_DEBUG
    ,
    last_rumble(std::chrono::steady_clock::now())
#endif
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

    libevdev_set_name(virt_evdev, "Nintendo Switch Combined Joy-Cons");

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

    // Map the S triggers to these misc. buttons if not connected via serial.
    if (!physl->is_serial_ctlr() && !physr->is_serial_ctlr()) {
        libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_TRIGGER_HAPPY1, NULL);
        libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_TRIGGER_HAPPY2, NULL);
        libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_TRIGGER_HAPPY3, NULL);
        libevdev_enable_event_code(virt_evdev, EV_KEY, BTN_TRIGGER_HAPPY4, NULL);
    }

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

    // Emulate analog triggers and HAT for android
#if defined(ANDROID) || defined(__ANDROID__)
    struct input_absinfo absconfig_fake = { 0 };
    absconfig_fake.minimum = 0;
    absconfig_fake.maximum = 1;
    absconfig_fake.fuzz = 0;
    absconfig_fake.flat = 0;

    libevdev_enable_event_code(virt_evdev, EV_ABS, ABS_Z, &absconfig_fake);
    libevdev_enable_event_code(virt_evdev, EV_ABS, ABS_RZ, &absconfig_fake);

    absconfig_fake.minimum = -1;

    libevdev_enable_event_code(virt_evdev, EV_ABS, ABS_HAT0X, &absconfig_fake);
    libevdev_enable_event_code(virt_evdev, EV_ABS, ABS_HAT0Y, &absconfig_fake);
#endif
    libevdev_enable_event_type(virt_evdev, EV_FF);
    libevdev_enable_event_code(virt_evdev, EV_FF, FF_RUMBLE, NULL);
    libevdev_enable_event_code(virt_evdev, EV_FF, FF_PERIODIC, NULL);
    libevdev_enable_event_code(virt_evdev, EV_FF, FF_SQUARE, NULL);
    libevdev_enable_event_code(virt_evdev, EV_FF, FF_TRIANGLE, NULL);
    libevdev_enable_event_code(virt_evdev, EV_FF, FF_SINE, NULL);
    libevdev_enable_event_code(virt_evdev, EV_FF, FF_GAIN, NULL);

    // Set the product information to a left joy-con's product info (but with virtual bus type)
    libevdev_set_id_vendor(virt_evdev, 0x57e);
    libevdev_set_id_product(virt_evdev, 0x2008); // HACK?
    libevdev_set_id_bustype(virt_evdev, BUS_VIRTUAL);
    libevdev_set_id_version(virt_evdev, 0x0000);

    // Enable LED events
    libevdev_enable_event_type(virt_evdev, EV_LED);
    libevdev_enable_event_code(virt_evdev, EV_LED, 0, NULL);
    libevdev_enable_event_code(virt_evdev, EV_LED, 1, NULL);
    libevdev_enable_event_code(virt_evdev, EV_LED, 2, NULL);
    libevdev_enable_event_code(virt_evdev, EV_LED, 3, NULL);

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

virt_ctlr_combined::~virt_ctlr_combined()
{
    epoll_manager.remove_subscriber(subscriber);

    libevdev_uinput_destroy(uidev);
    close(uifd);
    libevdev_free(virt_evdev);
}

void virt_ctlr_combined::handle_events(int fd)
{
    if (physl && fd == physl->get_fd())
        relay_events(physl);
    else if (physr && fd == physr->get_fd())
        relay_events(physr);
    else if (fd == get_uinput_fd())
        handle_uinput_event();
    else
        std::cerr << "fd=" << fd << " is an invalid fd for this combined controller\n";
}

bool virt_ctlr_combined::contains_phys_ctlr(std::shared_ptr<phys_ctlr> const ctlr) const
{
    return physl == ctlr || physr == ctlr;
}

bool virt_ctlr_combined::contains_phys_ctlr(char const *devpath) const
{
    return (physl && physl->get_devpath() == devpath) || (physr && physr->get_devpath() == devpath);
}

bool virt_ctlr_combined::contains_fd(int fd) const
{
    return (physl && physl->get_fd() == fd) || (physr && physr->get_fd() == fd) ||
            libevdev_uinput_get_fd(uidev) == fd;
}

std::vector<std::shared_ptr<phys_ctlr>> virt_ctlr_combined::get_phys_ctlrs()
{
    std::vector<std::shared_ptr<phys_ctlr>> ctlrs;
    if (physl)
        ctlrs.push_back(physl);
    if (physr)
        ctlrs.push_back(physr);
    return ctlrs;
}

int virt_ctlr_combined::get_uinput_fd()
{
    return libevdev_uinput_get_fd(uidev);
}

void virt_ctlr_combined::remove_phys_ctlr(const std::shared_ptr<phys_ctlr> phys)
{
    if (phys == physl) {
        std::cout << "Removing left joy-con from virtual combined controller\n";
        clear_left_ctlr_button_states(uidev);
        physl = nullptr;
    } else if (phys == physr) {
        std::cout << "Removing right joy-con from virtual combined controller\n";
        clear_right_ctlr_button_states(uidev);
        physr = nullptr;
    } else {
        std::cerr << "ERROR: Attempted to remove non-existant controller from combined joy-cons\n";
        exit(EXIT_FAILURE);
    }
}

void virt_ctlr_combined::add_phys_ctlr(std::shared_ptr<phys_ctlr> phys)
{
    if (phys->get_model() == phys_ctlr::Model::Left_Joycon && !physl) {
        std::cout << "Re-adding left joy-con to virtual combined controller\n";
        physl = phys;
        left_mac = phys->get_mac_addr();
    } else if (phys->get_model() == phys_ctlr::Model::Right_Joycon && !physr) {
        std::cout << "Re-adding right joy-con to virtual combined controller\n";
        physr = phys;
        right_mac = phys->get_mac_addr();
    } else {
        std::cerr << "ERROR: Attempted to add invalid controller to combined joy-cons\n";
        exit(EXIT_FAILURE);
    }

    // re-add all the ff_effects to the reconnected controller
    for (auto& kv : rumble_effects) {
        struct ff_effect* effect;

        if (phys == physl)
            effect = &kv.second.first;
        else
            effect = &kv.second.second;
        effect->id = -1;

        if (ioctl(phys->get_fd(), EVIOCSFF, effect) == -1)
            std::cerr << "ERROR: Failed to reupload ff_ffect: " << strerror(errno) << std::endl;
    }
}

enum phys_ctlr::Model virt_ctlr_combined::needs_model()
{
    enum phys_ctlr::Model model = phys_ctlr::Model::Unknown;

    if (!physl)
        model = phys_ctlr::Model::Left_Joycon;
    else if (!physr)
        model = phys_ctlr::Model::Right_Joycon;
    return model;
}

bool virt_ctlr_combined::no_ctlrs_left()
{
    return !physl && !physr;
}

bool virt_ctlr_combined::mac_belongs(const std::string& mac) const
{
    return mac != "" && (mac == left_mac || mac == right_mac);
}

bool virt_ctlr_combined::set_player_led(int index, bool on)
{
    if (index > 3)
        return false;

    libevdev_uinput_write_event(uidev, EV_LED, index, on);
    return true;
}

bool virt_ctlr_combined::set_all_player_leds(bool on)
{
    for (int i = 0; i < 4; i++) {
        if (!set_player_led(i, on))
            return false;
    }
    return true;
}

bool virt_ctlr_combined::set_player_leds_to_player(int player)
{
    if (player < 1 || player > 4) {
        std::cerr << player << " is not a valid player led value\n";
        return false;
    }

    for (int i = 0; i < player; i++) {
        set_player_led(i, true);
    }
    return true;
}
