### This is a fork of [DanielOgorchock/joycond](https://github.com/DanielOgorchock/joycond).

joycond is a linux daemon which uses the evdev devices provided by hid-nintendo (formerly known as hid-joycon) to implement joycon pairing.


# Installation
1. clone the repo
2. Install requirements (`sudo apt install libevdev-dev libudev-dev` or `sudo dnf install libevdev-devel libudev-devel`)
3. `cmake -B build -DCMAKE_BUILD_TYPE=Release`
4. `sudo make -C build install`
5. `sudo systemctl enable --now joycond`

# Uninstallation

1. `sudo systemctl disable --now joycond`
2. `sudo make -C build uninstall`

# Usage
When a joy-con or pro controller is connected via bluetooth or USB, the player LEDs signal that the controller is in pairing mode: player LEDs 2 and 4 light up steadily (top-to-bottom pattern _L_L).

For the pro controller, pressing both triggers will "pair" it.

For the pro controller, pressing Plus and Minus will pair it as a virtual controller.
This is useful when using Steam.

With the joy-cons, to use a single contoller alone, hold ZL and L at the same time (ZR and R for the right joy-con). Alternatively, hold both S triggers at once.

To combine two joy-cons into a virtual input device, press a *single* trigger on both of them at the same time. A new uinput device will be created called "Nintendo Switch Combined Joy-Cons".

Rumble support is now functional for the combined joy-con uinput device.

# Configuration

Optional `/etc/joycond.conf` with `key = value` lines (`#` starts a comment).
A missing file means all built-in defaults apply.

- `swap_ab_xy = true|false` (default: `false`) — on the combined
  controller, swaps the A and B buttons and the X and Y buttons.
- `rumble_gain = 0.0-1.0` (default: `1.0`) — scales the magnitudes of
  uploaded rumble effects (FF_RUMBLE, FF_CONSTANT, FF_PERIODIC) before
  they are forwarded to the physical joy-cons, e.g. `0.5` for half
  strength.
- `pair_rumble_mode = both|left|right|off` (default: `both`) — which
  physical joy-con(s) receive forwarded rumble events on the paired
  (combined) controller. `left` or `right` drive only that joy-con's
  motor, `off` silences both.
