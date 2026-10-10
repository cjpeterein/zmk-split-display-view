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
  not connected to the central
- active modifiers and the Caps, Num and Scroll Lock indicators
- a bongo cat that types at the central's WPM, and the WPM

A peripheral has no keymap or HID state, so the data comes from the
central through
[dmhuisma/zmk-split-status-relay](https://github.com/dmhuisma/zmk-split-status-relay)
and, for modifiers and indicators, through an extra
[arbitrary split data channel](https://github.com/dmhuisma/zmk_arbitrary_split_data_channel)
that this module adds.

## Supported ZMK

ZMK v0.3 (Zephyr 3.5, LVGL 8.3) with the BLE split transport. Later ZMK
releases move to LVGL 9 and are not supported.

## Install

Add the module and its dependencies to `config/west.yml`:

```yaml
  remotes:
    - name: dmhuisma
      url-base: https://github.com/dmhuisma
    - name: cjpeterein
      url-base: https://github.com/cjpeterein
  projects:
    - name: zmk_arbitrary_split_data_channel
      remote: dmhuisma
      revision: main
    - name: zmk-split-status-relay
      remote: dmhuisma
      revision: main
    - name: zmk-split-display-view
      remote: cjpeterein
      revision: main
```

Add the channels to the devicetree of the central and of every
peripheral, for example in a shared `.dtsi` included by each overlay:

```dts
/ {
    chosen {
        zmk,split-display-view-channel = &asdc1;
    };

    asdc0: split_data_channel {
        compatible = "zmk,arbitrary-split-data-channel";
        channel-id = <1>;
        status = "okay";
    };

    asdc1: split_display_view_channel {
        compatible = "zmk,arbitrary-split-data-channel";
        channel-id = <2>;
        status = "okay";
    };

    split_status_relay {
        compatible = "zmk,split-status-relay";
        asdc-channel = <&asdc0>;
        status = "okay";
    };
};
```

Build each half with the stock `nice_view` shield (it provides the
display node and the LVGL settings) and these options in its `.conf`:

```
CONFIG_ZMK_DISPLAY=y
CONFIG_NICE_VIEW_WIDGET_STATUS=n
CONFIG_ZMK_SPLIT_DISPLAY_VIEW=y
```

On the central, set `CONFIG_ZMK_HID_INDICATORS=y` to relay the lock
indicators and `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y`
so it can relay the battery levels of the halves.

## Kconfig options

| Option | Default | Meaning |
| --- | --- | --- |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW` | n | Use this status screen on a peripheral |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_ROTATE_CCW` | n | Rotate counterclockwise, for a panel mounted the other way up |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_MAC_MODIFIERS` | n | Show macOS modifier symbols |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_SLOT0_LABEL` | `L` | Battery label of the first peripheral the central paired |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_SLOT1_LABEL` | `R` | Battery label of the second |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_CENTRAL_BATTERY` | n | Also show the central's battery |
| `CONFIG_ZMK_SPLIT_DISPLAY_VIEW_MODS_RELAY` | y when the chosen channel exists | Relay modifiers and indicators |

The relay numbers peripherals in the order the central paired them, so
pair the left half first or swap the labels.

## Development

`tests/rotate_test.c` checks the rotation on the host:

```
cc -I src tests/rotate_test.c -o /tmp/rotate_test && /tmp/rotate_test
```
