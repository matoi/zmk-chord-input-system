/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * NICOLA character-character combo registration.
 * Each DT node with compatible "zmk,chord-input-char-combo" registers
 * a combo entry in the engine's combo table at init time.
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <zmk-chordis/chordis_engine.h>
#include <zmk-chordis/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define DT_DRV_COMPAT zmk_chord_input_char_combo

#define REGISTER_COMBO(n)                                             \
    chordis_register_char_combo(                                       \
        DT_INST_PROP(n, kana_a),                                     \
        DT_INST_PROP(n, kana_b),                                     \
        DT_INST_PROP(n, result));

static int nicola_char_combo_init(void) {
    DT_INST_FOREACH_STATUS_OKAY(REGISTER_COMBO)
    return 0;
}

SYS_INIT(nicola_char_combo_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
