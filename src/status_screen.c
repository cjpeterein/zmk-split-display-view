/*
 * Copyright (c) 2024 Marcos Chow Castro
 * Copyright (c) 2026 Craig Peterein
 *
 * SPDX-License-Identifier: MIT
 *
 * The layout, symbols and bongo cat come from
 * https://github.com/mctechnology17/zmk-dongle-display-view. A split
 * peripheral has no HID or keymap state of its own, so everything shown here
 * arrives from the central (central.c) over an L2CAP channel.
 */

#include <string.h>
#include <stdio.h>

#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <lvgl.h>

#include <dt-bindings/zmk/modifiers.h>
#include <zmk/display.h>
#include <zmk/endpoints_types.h>
#include <zmk/event_manager.h>
#include <zmk/events/split_peripheral_status_changed.h>

#include "rotate.h"
#include "sdv_status.h"

/* The panel is mounted in portrait, so the screen is drawn upright and rotated. */
#define PHYS_W DT_PROP(DT_CHOSEN(zephyr_display), width)
#define PHYS_H DT_PROP(DT_CHOSEN(zephyr_display), height)
#define W PHYS_H
#define H PHYS_W
BUILD_ASSERT(W == 68 && H == 160, "The layout is drawn for a 160x68 display such as nice!view");
BUILD_ASSERT(sizeof(lv_color_t) == 1, "Rotation assumes LV_COLOR_DEPTH_1");

#define BONGO_Y 120
#define BONGO_H 26

#define BG lv_color_white()
#define FG lv_color_black()

LV_IMG_DECLARE(sym_usb);
LV_IMG_DECLARE(sym_bt);
LV_IMG_DECLARE(sym_ok);
LV_IMG_DECLARE(sym_nok);
LV_IMG_DECLARE(sym_open);
LV_IMG_DECLARE(sym_1);
LV_IMG_DECLARE(sym_2);
LV_IMG_DECLARE(sym_3);
LV_IMG_DECLARE(sym_4);
LV_IMG_DECLARE(sym_5);
LV_IMG_DECLARE(control_icon);
LV_IMG_DECLARE(shift_icon);
LV_IMG_DECLARE(bongo_cat_none);
LV_IMG_DECLARE(bongo_cat_left1);
LV_IMG_DECLARE(bongo_cat_left2);
LV_IMG_DECLARE(bongo_cat_right1);
LV_IMG_DECLARE(bongo_cat_right2);
LV_IMG_DECLARE(bongo_cat_both1);
LV_IMG_DECLARE(bongo_cat_both1_open);
LV_IMG_DECLARE(bongo_cat_both2);

static const lv_img_dsc_t *sym_num[] = {&sym_1, &sym_2, &sym_3, &sym_4, &sym_5};

struct modifier_symbol {
    uint8_t mods;
    const lv_img_dsc_t *img;
};

#if IS_ENABLED(CONFIG_ZMK_SPLIT_DISPLAY_VIEW_MAC_MODIFIERS)
LV_IMG_DECLARE(opt_icon);
LV_IMG_DECLARE(cmd_icon);
static const struct modifier_symbol modifier_symbols[] = {
    {MOD_LCTL | MOD_RCTL, &control_icon},
    {MOD_LALT | MOD_RALT, &opt_icon},
    {MOD_LGUI | MOD_RGUI, &cmd_icon},
    {MOD_LSFT | MOD_RSFT, &shift_icon},
};
#else
LV_IMG_DECLARE(alt_icon);
LV_IMG_DECLARE(win_icon);
static const struct modifier_symbol modifier_symbols[] = {
    {MOD_LGUI | MOD_RGUI, &win_icon},
    {MOD_LALT | MOD_RALT, &alt_icon},
    {MOD_LCTL | MOD_RCTL, &control_icon},
    {MOD_LSFT | MOD_RSFT, &shift_icon},
};
#endif

static const lv_img_dsc_t *idle_imgs[] = {&bongo_cat_both1_open, &bongo_cat_both1_open,
                                          &bongo_cat_both1_open, &bongo_cat_both1};
static const lv_img_dsc_t *slow_imgs[] = {
    &bongo_cat_left1, &bongo_cat_both1, &bongo_cat_both1, &bongo_cat_right1, &bongo_cat_both1,
    &bongo_cat_both1, &bongo_cat_left1, &bongo_cat_both1, &bongo_cat_both1};
static const lv_img_dsc_t *mid_imgs[] = {&bongo_cat_left2,  &bongo_cat_left1,  &bongo_cat_none,
                                         &bongo_cat_right2, &bongo_cat_right1, &bongo_cat_none};
static const lv_img_dsc_t *fast_imgs[] = {&bongo_cat_both2, &bongo_cat_both1, &bongo_cat_none,
                                          &bongo_cat_none};

struct animation {
    const lv_img_dsc_t **imgs;
    uint8_t count;
    uint16_t duration_ms;
};

static const struct animation animations[] = {
    {idle_imgs, ARRAY_SIZE(idle_imgs), 10000},
    {slow_imgs, ARRAY_SIZE(slow_imgs), 2000},
    {mid_imgs, ARRAY_SIZE(mid_imgs), 500},
    {fast_imgs, ARRAY_SIZE(fast_imgs), 200},
};

static const char *slot_labels[SDV_SLOTS] = {CONFIG_ZMK_SPLIT_DISPLAY_VIEW_SLOT0_LABEL,
                                         CONFIG_ZMK_SPLIT_DISPLAY_VIEW_SLOT1_LABEL};

/* ponytail: listeners write and the display queue reads without a lock; a torn read only
 * shows one stale frame */
static struct sdv_status state;
static bool linked;

static lv_color_t portrait_buf[W * H];
static lv_color_t screen_buf[PHYS_W * PHYS_H];
static lv_obj_t *portrait;
static lv_obj_t *screen_canvas;
static lv_timer_t *bongo_timer;
static const struct animation *animation;
static uint8_t frame;

static void draw_text(int x, int y, int max_w, const lv_font_t *font, lv_text_align_t align,
                      const char *txt) {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = FG;
    dsc.font = font;
    dsc.align = align;
    lv_canvas_draw_text(portrait, x, y, max_w, &dsc, txt);
}

static void draw_img(int x, int y, const lv_img_dsc_t *img) {
    lv_draw_img_dsc_t dsc;
    lv_draw_img_dsc_init(&dsc);
    lv_canvas_draw_img(portrait, x, y, img, &dsc);
}

static void draw_rect(int x, int y, int w, int h, lv_color_t fill, uint8_t border) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = fill;
    dsc.border_color = FG;
    dsc.border_width = border;
    lv_canvas_draw_rect(portrait, x, y, w, h, &dsc);
}

static void draw_output(void) {
    bool usb = state.transport == ZMK_TRANSPORT_USB;

    draw_img(0, 0, &sym_usb);
    draw_img(11, 0, state.usb_connected ? &sym_ok : &sym_nok);
    draw_img(22, 0, &sym_bt);
    draw_img(33, 0, state.profile < ARRAY_SIZE(sym_num) ? sym_num[state.profile] : &sym_nok);
    draw_img(33, 8,
             !state.profile_bonded ? &sym_open : (state.profile_connected ? &sym_ok : &sym_nok));
    draw_rect(usb ? 0 : 22, 15, 17, 2, FG, 0);
}

static void draw_battery_row(int y, const char *label, uint8_t level, bool connected) {
    char txt[6] = "--";

    draw_text(0, y, 8, &lv_font_unscii_8, LV_TEXT_ALIGN_LEFT, label);
    draw_rect(10, y, 22, 8, BG, 1);
    draw_rect(32, y + 2, 2, 4, FG, 0);
    if (connected) {
        draw_rect(12, y + 2, MIN(level, 100) * 18 / 100, 4, FG, 0);
        snprintf(txt, sizeof(txt), "%u", level);
    }
    draw_text(36, y, W - 36, &lv_font_unscii_8, LV_TEXT_ALIGN_LEFT, txt);
}

static void draw_modifiers(void) {
    for (int i = 0; i < ARRAY_SIZE(modifier_symbols); i++) {
        int x = 1 + 17 * i;

        draw_img(x, 80, modifier_symbols[i].img);
        if (state.mods & modifier_symbols[i].mods) {
            draw_rect(x, 96, 14, 2, FG, 0);
        }
    }
}

static void draw_indicators(void) {
    char txt[16] = "";

    if (state.indicators & BIT(1)) {
        strcat(txt, "CAPS ");
    }
    if (state.indicators & BIT(0)) {
        strcat(txt, "NUM ");
    }
    if (state.indicators & BIT(2)) {
        strcat(txt, "SCRL");
    }
    draw_text(0, 102, W, &lv_font_unscii_8, LV_TEXT_ALIGN_LEFT, txt);
}

static void draw_bongo(void) {
    draw_rect(0, BONGO_Y, W, BONGO_H, BG, 0);
    draw_img((W - 50) / 2, BONGO_Y, animation->imgs[frame % animation->count]);
}

static void show_rows(int y0, int y1) {
    bool ccw = IS_ENABLED(CONFIG_ZMK_SPLIT_DISPLAY_VIEW_ROTATE_CCW);
    lv_area_t area = {.x1 = ccw ? y0 : H - y1, .y1 = 0, .x2 = ccw ? y1 - 1 : H - 1 - y0,
                      .y2 = W - 1};

    sdv_rotate((const uint8_t *)portrait_buf, (uint8_t *)screen_buf, W, H, y0, y1, ccw);
    lv_obj_invalidate_area(screen_canvas, &area);
}

static void bongo_tick(lv_timer_t *timer) {
    frame++;
    draw_bongo();
    show_rows(BONGO_Y, BONGO_Y + BONGO_H);
}

static void select_animation(void) {
    int i = state.wpm < 5 ? 0 : state.wpm < 30 ? 1 : state.wpm < 70 ? 2 : 3;

    if (animation != &animations[i]) {
        animation = &animations[i];
        frame = 0;
        lv_timer_set_period(bongo_timer, animation->duration_ms / animation->count);
    }
}

static void draw_all(void) {
    char wpm[12];

    lv_canvas_fill_bg(portrait, BG, LV_OPA_COVER);
    draw_output();
    for (int i = 0; i < SDV_SLOTS; i++) {
        draw_battery_row(20 + 12 * i, slot_labels[i], state.battery[i], state.connected[i]);
    }
#if IS_ENABLED(CONFIG_ZMK_SPLIT_DISPLAY_VIEW_CENTRAL_BATTERY)
    draw_battery_row(44, "C", state.central_battery, true);
#endif
    draw_text(0, 58, W, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER,
              linked ? state.layer : "no link");
    draw_modifiers();
    draw_indicators();
    select_animation();
    draw_bongo();
    snprintf(wpm, sizeof(wpm), "WPM %u", state.wpm);
    draw_text(0, 150, W, &lv_font_unscii_8, LV_TEXT_ALIGN_CENTER, wpm);
    show_rows(0, H);
}

static void redraw_cb(struct k_work *work) { draw_all(); }

static K_WORK_DEFINE(redraw_work, redraw_cb);

static void request_redraw(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &redraw_work);
    }
}

static int sdv_listener(const zmk_event_t *eh) {
    linked = as_zmk_split_peripheral_status_changed(eh)->connected;
    request_redraw();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split_display_view, sdv_listener);
ZMK_SUBSCRIPTION(split_display_view, zmk_split_peripheral_status_changed);

static struct bt_l2cap_le_chan status_chan;

static int status_recv(struct bt_l2cap_chan *chan, struct net_buf *buf) {
    if (buf->len == sizeof(state)) {
        memcpy(&state, buf->data, sizeof(state));
        state.layer[SDV_LAYER_LEN - 1] = '\0';
        request_redraw();
    }
    return 0;
}

static const struct bt_l2cap_chan_ops status_ops = {.recv = status_recv};

static int status_accept(struct bt_conn *conn, struct bt_l2cap_server *server,
                         struct bt_l2cap_chan **chan) {
    if (status_chan.chan.conn) {
        return -ENOMEM;
    }
    status_chan.chan.ops = &status_ops;
    status_chan.rx.mtu = SDV_MTU;
    *chan = &status_chan.chan;
    return 0;
}

static struct bt_l2cap_server status_server = {
    .psm = SDV_PSM,
    .sec_level = BT_SECURITY_L1,
    .accept = status_accept,
};

/* Registered at boot, not with the screen, so the central's first connect is not refused */
static int status_server_init(void) { return bt_l2cap_server_register(&status_server); }

SYS_INIT(status_server_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    portrait = lv_canvas_create(screen);
    lv_canvas_set_buffer(portrait, portrait_buf, W, H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_add_flag(portrait, LV_OBJ_FLAG_HIDDEN);

    screen_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(screen_canvas, screen_buf, PHYS_W, PHYS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(screen_canvas, LV_ALIGN_TOP_LEFT, 0, 0);

    bongo_timer = lv_timer_create(bongo_tick, animations[0].duration_ms, NULL);
    draw_all();

    return screen;
}
