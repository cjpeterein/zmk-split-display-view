/*
 * Copyright (c) 2026 Craig Peterein
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <arbitrary_split_data_channel.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid_indicators.h>
#endif

#include "mods_relay.h"

static const struct device *channel = DEVICE_DT_GET(DT_CHOSEN(zmk_split_display_view_channel));
static struct sdv_mods_packet sent;

/* Runs from the work queue so the HID listener has applied the key event first */
static void send_cb(struct k_work *work) {
    struct sdv_mods_packet now = {.mods = zmk_hid_get_explicit_mods()};

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    now.indicators = zmk_hid_indicators_get_current_profile();
#endif
    /* ponytail: only changes are sent, so a half that reconnects shows stale state until the
     * next modifier or indicator change */
    if (memcmp(&now, &sent, sizeof(now)) != 0 &&
        asdc_send(channel, (const uint8_t *)&now, sizeof(now), 0) == 0) {
        sent = now;
    }
}

static K_WORK_DEFINE(send_work, send_cb);

static int mods_relay_listener(const zmk_event_t *eh) {
    k_work_submit(&send_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split_display_view_mods_relay, mods_relay_listener);
ZMK_SUBSCRIPTION(split_display_view_mods_relay, zmk_keycode_state_changed);
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
ZMK_SUBSCRIPTION(split_display_view_mods_relay, zmk_hid_indicators_changed);
#endif
