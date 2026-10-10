# zmk-split-display-view

A ZMK status screen for the display on a split peripheral half. It ports
the layout, symbols and bongo cat of
[mctechnology17/zmk-dongle-display-view](https://github.com/mctechnology17/zmk-dongle-display-view)
(MIT, Copyright (c) 2024 Marcos Chow Castro) from a dongle to the halves,
which is useful when a dongle is the split central and the halves have
the displays.

The screen is drawn in portrait and rotated onto a 160x68 one-bit display
such as nice!view. It shows:

- USB and BLE output of the central, the active BLE profile and its state
- the battery level of each half (and optionally of the central)
- the name of the highest active layer, or `no link` when the half is
  not connected to the central. A name too wide for the large font is
  drawn in the small font, which fits 8 characters; longer names are
  clipped
- active modifiers and the Caps, Num and Scroll Lock indicators
- a bongo cat that types at the central's WPM, and the WPM

A peripheral has no keymap or HID state, so the central sends all of it
to each peripheral as one small status message over a Bluetooth L2CAP
channel (PSM 0x0081) on the existing split connection. The central side
of this module does that, so it needs no other modules. It replaces
[dmhuisma/zmk-split-status-relay](https://github.com/dmhuisma/zmk-split-status-relay)
and
[zmk_arbitrary_split_data_channel](https://github.com/dmhuisma/zmk_arbitrary_split_data_channel),
which earlier versions of this module used.

## Supported ZMK

ZMK v0.3 (Zephyr 3.5, LVGL 8.3) with the BLE split transport. Later ZMK
releases move to LVGL 9 and are not supported. The central and the
peripherals must run the same version of this module.

## Install

Add the module to `config/west.yml`:

```yaml
  remotes:
    - name: cjpeterein
      url-base: https://github.com/cjpeterein
  projects:
    - name: zmk-split-display-view
      remote: cjpeterein
      revision: main
```

Build each half with the stock `nice_view` shield (it provides the
display node and the LVGL settings) and these options in its `.conf`:

```
CONFIG_ZMK_DISPLAY=y
CONFIG_NICE_VIEW_WIDGET_STATUS=n
CONFIG_ZMK_SPLIT_DISPLAY_VIEW=y
```

The central needs no configuration: building it with the module enables
`CONFIG_ZMK_SPLIT_DISPLAY_VIEW_CENTRAL`, which turns on WPM, HID
indicators and fetching the battery levels of the halves unless the
config turns them off.

When upgrading from the relay modules, remove them from `west.yml` and
remove their devicetree nodes and the `zmk,split-display-view-channel`
chosen node from the overlays.

## Kconfig options

| Option | Default | Meaning |
| --- | --- | --- |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW` | n | Use this status screen on a peripheral |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_ROTATE_CCW` | n | Rotate counterclockwise, for a panel mounted the other way up |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_MAC_MODIFIERS` | n | Show macOS command and option symbols in place of Windows and Alt |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_MIRROR_MODIFIERS` | n | Show the modifiers as Shift, Ctrl, Alt, GUI instead of GUI, Alt, Ctrl, Shift, to mirror the left half on the right |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_SLOT0_LABEL` | `L` | Battery label of the first peripheral the central paired |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_SLOT1_LABEL` | `R` | Battery label of the second |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_CENTRAL_BATTERY` | n | Also show the central's battery |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_CENTRAL` | y on a BLE split central | Send the status to the peripherals |

The halves share one `.conf` in most user configs, so set the mirror
option for the right half only, for example with
`cmake-args: -DCONFIG_ZMK_SPLIT_DISPLAY_VIEW_MIRROR_MODIFIERS=y` in its
`build.yaml` entry.

The central numbers peripherals in the order it paired them, so pair
the left half first or swap the labels.

## Development

`tests/rotate_test.c` checks the rotation on the host:

```
cc -I src tests/rotate_test.c -o /tmp/rotate_test && /tmp/rotate_test
```

`.github/workflows/ci.yml` runs that check and builds the ZMK v0.3
Corne in `test-config/` with the module: the left half as a split
central and the right half as a peripheral with this screen.
