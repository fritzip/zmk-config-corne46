# This module adds small board-agnostic extras used by this zmk-config.

zephyr_library()
zephyr_library_sources(
  ../src/numpad_rgb.c
)
