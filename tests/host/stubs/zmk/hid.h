/*
 * Host test stub for <zmk/hid.h>
 * Captures keycode emit calls into a global log accessible from tests.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

uint8_t zmk_hid_get_explicit_mods(void);
int     raise_zmk_keycode_state_changed_from_encoded(uint32_t encoded, bool pressed, int64_t timestamp);
