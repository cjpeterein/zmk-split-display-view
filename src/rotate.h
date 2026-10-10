/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Rotates rows [y0, y1) of a w x h one-byte-per-pixel image by 90 degrees
 * into dst, which is h pixels wide and w pixels tall.
 */
static inline void sdv_rotate(const uint8_t *src, uint8_t *dst, int w, int h, int y0, int y1,
                              bool ccw) {
    for (int y = y0; y < y1; y++) {
        for (int x = 0; x < w; x++) {
            if (ccw) {
                dst[(w - 1 - x) * h + y] = src[y * w + x];
            } else {
                dst[x * h + (h - 1 - y)] = src[y * w + x];
            }
        }
    }
}
