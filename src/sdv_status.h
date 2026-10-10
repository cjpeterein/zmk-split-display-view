/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/toolchain.h>

/* dmhuisma's zmk_arbitrary_split_data_channel defaults to 0x0080, so both can coexist */
#define SDV_PSM 0x0081
#define SDV_MTU 32
#define SDV_SLOTS 2
#define SDV_LAYER_LEN 16

/* Everything a peripheral screen shows, sent whole by the central on every change */
struct sdv_status {
    uint8_t battery[SDV_SLOTS];
    bool connected[SDV_SLOTS];
    uint8_t central_battery;
    uint8_t transport;
    bool usb_connected;
    uint8_t profile;
    bool profile_connected;
    bool profile_bonded;
    uint8_t wpm;
    uint8_t mods;
    uint8_t indicators;
    char layer[SDV_LAYER_LEN];
} __packed;

BUILD_ASSERT(sizeof(struct sdv_status) <= SDV_MTU, "The status must fit one L2CAP SDU");
