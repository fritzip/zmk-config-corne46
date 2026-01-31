#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>

/*
 * This module intentionally avoids ZMK's event manager APIs.
 * Some ZMK revisions do not ship <zmk/event_manager.h>, which breaks builds.
 *
 * Instead, we run a lightweight periodic worker that checks whether the NUM
 * layer is active and, if so, writes a per-LED mask to the underglow strip.
 */

#if __has_include(<zmk/keymap.h>)
#include <zmk/keymap.h>
#define HAS_ZMK_KEYMAP 1
#else
#define HAS_ZMK_KEYMAP 0
#endif

#if !DT_HAS_CHOSEN(zmk_underglow)
#error "A zmk,underglow chosen node must be declared"
#endif

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define STRIP_LEN DT_PROP(STRIP_NODE, chain_length)

#ifndef CONFIG_ZMK_CORNE_NUMPAD_RGB_REFRESH_MS
#define CONFIG_ZMK_CORNE_NUMPAD_RGB_REFRESH_MS 40
#endif

#define NUMPAD_RGB_ACTIVE_REFRESH K_MSEC(CONFIG_ZMK_CORNE_NUMPAD_RGB_REFRESH_MS)
#define NUMPAD_RGB_IDLE_REFRESH K_MSEC(200)

/*
 * Mapping assumption:
 * - One WS2812 per physical key.
 * - LED order matches the key positions for each half, grouped row-by-row.
 *
 * If your wiring order differs, we can adjust the mapping arrays below.
 */
#if defined(CONFIG_BOARD_CORNE_CHOC_PRO_LEFT)
static const uint8_t keypos_by_led[STRIP_LEN] = {
    0, 1, 2, 3, 4, 5, 6,
    14, 15, 16, 17, 18, 19, 20,
    28, 29, 30, 31, 32, 33,
    40, 41, 42,
};
#elif defined(CONFIG_BOARD_CORNE_CHOC_PRO_RIGHT)
static const uint8_t keypos_by_led[STRIP_LEN] = {
    7, 8, 9, 10, 11, 12, 13,
    21, 22, 23, 24, 25, 26, 27,
    34, 35, 36, 37, 38, 39,
    43, 44, 45,
};
#else
/* Fallback: unknown board wiring; do nothing safely. */
static const uint8_t keypos_by_led[STRIP_LEN] = {0};
#endif

static bool is_numpad_keypos(uint8_t keypos) {
    /*
     * NUM layer in config/corne_choc_pro.keymap:
     * Row0: N7 N8 N9 at positions 2 3 4
     * Row1: N4 N5 N6 at positions 16 17 18
     * Row2: N1 N2 N3 at positions 30 31 32
     * Thumbs: DOT N0 MINUS at positions 40 41 42
     */
    switch (keypos) {
    case 2:
    case 3:
    case 4:
    case 16:
    case 17:
    case 18:
    case 30:
    case 31:
    case 32:
    case 40:
    case 41:
    case 42:
        return true;
    default:
        return false;
    }
}

static int set_all_off(void) {
    const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
    if (!device_is_ready(strip)) {
        return -ENODEV;
    }

    static struct led_rgb pixels[STRIP_LEN];

    for (int i = 0; i < STRIP_LEN; i++) {
        pixels[i] = (struct led_rgb){.r = 0, .g = 0, .b = 0};
    }

    return led_strip_update_rgb(strip, pixels, STRIP_LEN);
}

static int set_numpad_pattern(void) {
    const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
    if (!device_is_ready(strip)) {
        return -ENODEV;
    }

    static struct led_rgb pixels[STRIP_LEN];

    for (int i = 0; i < STRIP_LEN; i++) {
        if (is_numpad_keypos(keypos_by_led[i])) {
            pixels[i] = (struct led_rgb){.r = 0, .g = 0, .b = 0xFF};
        } else {
            pixels[i] = (struct led_rgb){.r = 0, .g = 0, .b = 0};
        }
    }

    return led_strip_update_rgb(strip, pixels, STRIP_LEN);
}

static bool last_num_active;

static struct k_work_delayable numpad_rgb_work;

static void numpad_rgb_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
    if (!device_is_ready(strip)) {
        (void)k_work_schedule(&numpad_rgb_work, K_SECONDS(1));
        return;
    }

#if !HAS_ZMK_KEYMAP
    /* Can't query active layers on this ZMK revision; fail safely. */
    (void)k_work_schedule(&numpad_rgb_work, K_SECONDS(1));
    return;
#else
    bool num_active = zmk_keymap_layer_active(CONFIG_ZMK_CORNE_NUMPAD_RGB_NUM_LAYER_ID);

    if (num_active) {
        /* Keep writing while NUM is active so it stays overridden. */
        (void)set_numpad_pattern();
        last_num_active = true;
        (void)k_work_schedule(&numpad_rgb_work, NUMPAD_RGB_ACTIVE_REFRESH);
        return;
    }

    /* Leaving NUM: ensure we don't leave stale blue LEDs behind. */
    if (last_num_active) {
        (void)set_all_off();
        last_num_active = false;
    }

    (void)k_work_schedule(&numpad_rgb_work, NUMPAD_RGB_IDLE_REFRESH);
#endif
}

static int numpad_rgb_init(void) {
    k_work_init_delayable(&numpad_rgb_work, numpad_rgb_work_handler);
    (void)k_work_schedule(&numpad_rgb_work, NUMPAD_RGB_IDLE_REFRESH);
    return 0;
}

SYS_INIT(numpad_rgb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
