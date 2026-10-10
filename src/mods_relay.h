/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/sys/util.h>

struct sdv_mods_packet {
    uint8_t mods;
    uint8_t indicators;
} __packed;
