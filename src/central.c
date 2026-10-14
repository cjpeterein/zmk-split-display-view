/*
 * Copyright (c) 2026 Craig Peterein
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/kernel.h>

#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid_indicators.h>
#endif
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_WPM)
#include <zmk/events/wpm_state_changed.h>
#include <zmk/wpm.h>
#endif

#include "sdv_status.h"

/* ponytail: ZMK v0.3 has no public API that maps a connection to the slot its battery events
 * use, so this borrows the internal one from split/bluetooth/central.c */
extern int peripheral_slot_index_for_conn(struct bt_conn *conn);

static struct bt_l2cap_le_chan chans[SDV_SLOTS];
/* ponytail: listeners, BT callbacks and the send work share this without a lock; a torn
 * update only sends one stale frame that the next change corrects */
static struct sdv_status status;
static struct sdv_status sent;

NET_BUF_POOL_FIXED_DEFINE(sdv_tx_pool, SDV_SLOTS * 2, BT_L2CAP_SDU_BUF_SIZE(SDV_MTU),
                          CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static void read_layer(char *layer) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    const char *name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index));

    memset(layer, 0, SDV_LAYER_LEN);
    if (name && *name) {
        strncpy(layer, name, SDV_LAYER_LEN - 1);
    } else {
        snprintf(layer, SDV_LAYER_LEN, "%u", index);
    }
}

static void send_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(send_work, send_cb);

/* Runs from the work queue so the HID listener has applied the key event first */
static void send_cb(struct k_work *work) {
    struct sdv_status now = status;
    bool failed = false;

    now.transport = zmk_endpoints_selected().transport;
    now.profile = zmk_ble_active_profile_index();
    now.profile_connected = zmk_ble_active_profile_is_connected();
    now.profile_bonded = !zmk_ble_active_profile_is_open();
    now.mods = zmk_hid_get_explicit_mods();
    read_layer(now.layer);
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    now.central_battery = zmk_battery_state_of_charge();
#endif
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    now.indicators = zmk_hid_indicators_get_current_profile();
#endif
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    now.usb_connected = zmk_usb_is_powered();
#endif
#if IS_ENABLED(CONFIG_ZMK_WPM)
    now.wpm = zmk_wpm_get_state();
#endif

    if (memcmp(&now, &sent, sizeof(now)) == 0) {
        return;
    }
    for (int i = 0; i < SDV_SLOTS; i++) {
        struct net_buf *buf;

        if (!now.connected[i]) {
            continue;
        }
        buf = net_buf_alloc(&sdv_tx_pool, K_NO_WAIT);
        if (!buf) {
            failed = true;
            continue;
        }
        net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
        net_buf_add_mem(buf, &now, sizeof(now));
        if (bt_l2cap_chan_send(&chans[i].chan, buf) < 0) {
            net_buf_unref(buf);
            failed = true;
        }
    }
    if (failed) {
        k_work_reschedule(&send_work, K_MSEC(100));
    } else {
        sent = now;
    }
}

static void request_send(void) { k_work_schedule(&send_work, K_NO_WAIT); }

static int chan_slot(struct bt_l2cap_chan *chan) { return BT_L2CAP_LE_CHAN(chan) - chans; }

static void chan_connected(struct bt_l2cap_chan *chan) {
    status.connected[chan_slot(chan)] = true;
    request_send();
}

static void chan_disconnected(struct bt_l2cap_chan *chan) {
    status.connected[chan_slot(chan)] = false;
    status.usb_powered[chan_slot(chan)] = false;
    request_send();
}

static int chan_recv(struct bt_l2cap_chan *chan, struct net_buf *buf) {
    if (buf->len == SDV_USB_POWER_LEN) {
        status.usb_powered[chan_slot(chan)] = buf->data[0];
        request_send();
    }
    return 0;
}

static const struct bt_l2cap_chan_ops chan_ops = {
    .connected = chan_connected,
    .disconnected = chan_disconnected,
    .recv = chan_recv,
};

static void sdv_connected(struct bt_conn *conn, uint8_t err) {
    struct bt_conn_info info;
    int slot = peripheral_slot_index_for_conn(conn);

    if (err || bt_conn_get_info(conn, &info) || info.role != BT_CONN_ROLE_CENTRAL || slot < 0 ||
        slot >= SDV_SLOTS) {
        return;
    }
    chans[slot].chan.ops = &chan_ops;
    chans[slot].rx.mtu = SDV_MTU;
    bt_l2cap_chan_connect(conn, &chans[slot].chan, SDV_PSM);
}

BT_CONN_CB_DEFINE(split_display_view_conn) = {
    .connected = sdv_connected,
};

static int sdv_listener(const zmk_event_t *eh) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    const struct zmk_peripheral_battery_state_changed *bat =
        as_zmk_peripheral_battery_state_changed(eh);

    if (bat && bat->source < SDV_SLOTS) {
        status.battery[bat->source] = bat->state_of_charge;
    }
#endif
    request_send();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split_display_view_central, sdv_listener);
ZMK_SUBSCRIPTION(split_display_view_central, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(split_display_view_central, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(split_display_view_central, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(split_display_view_central, zmk_ble_active_profile_changed);
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
ZMK_SUBSCRIPTION(split_display_view_central, zmk_battery_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
ZMK_SUBSCRIPTION(split_display_view_central, zmk_peripheral_battery_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
ZMK_SUBSCRIPTION(split_display_view_central, zmk_hid_indicators_changed);
#endif
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(split_display_view_central, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_WPM)
ZMK_SUBSCRIPTION(split_display_view_central, zmk_wpm_state_changed);
#endif
