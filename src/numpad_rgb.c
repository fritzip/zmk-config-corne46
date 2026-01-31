#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

#if !DT_HAS_CHOSEN(zmk_underglow)
#error "A zmk,underglow chosen node must be declared"
#endif

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define STRIP_LEN DT_PROP(STRIP_NODE, chain_length)

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
static bool underglow_was_on;

static int numpad_rgb_listener(const zmk_event_t *eh) {
#if !IS_ENABLED(CONFIG_ZMK_CORNE_NUMPAD_RGB)
    ARG_UNUSED(eh);
    return -ENOTSUP;
#else
    if (!as_zmk_layer_state_changed(eh)) {
        return -ENOTSUP;
    }

    bool num_active = zmk_keymap_layer_active(CONFIG_ZMK_CORNE_NUMPAD_RGB_NUM_LAYER_ID);

    if (num_active == last_num_active) {
        return 0;
    }

    last_num_active = num_active;

    if (num_active) {
        (void)zmk_rgb_underglow_get_state(&underglow_was_on);

        /* Stop the built-in underglow animator so it doesn't overwrite our per-key pattern. */
        (void)zmk_rgb_underglow_off();

        /* Apply our numpad mask (blue) + background (off). */
        (void)set_numpad_pattern();
    } else {
        /* Restore the user's underglow state (on/off + effect) when leaving NUM. */
        if (underglow_was_on) {
            (void)zmk_rgb_underglow_on();
        } else {
            (void)zmk_rgb_underglow_off();
        }
    }

    return 0;
#endif
}

ZMK_LISTENER(numpad_rgb, numpad_rgb_listener);
ZMK_SUBSCRIPTION(numpad_rgb, zmk_layer_state_changed);
