# This module adds small board-agnostic extras used by this zmk-config.

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_ZMK_CORNE_NUMPAD_RGB ../src/numpad_rgb.c)
