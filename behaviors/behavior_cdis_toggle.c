/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * NICOLA mode toggle behavior.
 * &cdis_on activates the NICOLA layer + sends LANG1 (IME kana on).
 * &cdis_off flushes the state machine, deactivates the layer, sends LANG2.
 * Both are idempotent.
 */

#define DT_DRV_COMPAT zmk_behavior_chord_input_toggle

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk-chordis/chordis_engine.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

enum cdis_toggle_action {
    NICOLA_TOGGLE_ON,
    NICOLA_TOGGLE_OFF,
};

struct behavior_cdis_toggle_config {
    enum cdis_toggle_action action;
    uint32_t ime_keycode;
};

static int cdis_toggle_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_cdis_toggle_config *cfg = dev->config;
    uint32_t layer = binding->param1;

    LOG_DBG("position %d action %s layer %d ime 0x%02x", event.position,
            cfg->action == NICOLA_TOGGLE_ON ? "on" : "off", layer, cfg->ime_keycode);

    /* Flush state machine before turning off */
    if (cfg->action == NICOLA_TOGGLE_OFF) {
        chordis_engine_flush(event.timestamp);
    }

    int ret;
    if (cfg->action == NICOLA_TOGGLE_ON) {
        ret = zmk_keymap_layer_activate(layer);
        chordis_set_active_layer(layer);
    } else {
        chordis_clear_active_layer();
        ret = zmk_keymap_layer_deactivate(layer);
    }
    LOG_DBG("layer %s layer=%d ret=%d", cfg->action == NICOLA_TOGGLE_ON ? "activate" : "deactivate",
            layer, ret);

    /* Send IME switch keycode (LANG1 or LANG2) */
    raise_zmk_keycode_state_changed_from_encoded(cfg->ime_keycode, true, event.timestamp);
    raise_zmk_keycode_state_changed_from_encoded(cfg->ime_keycode, false, event.timestamp);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int cdis_toggle_binding_released(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    LOG_DBG("position %d", event.position);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_cdis_toggle_driver_api = {
    .binding_pressed = cdis_toggle_binding_pressed,
    .binding_released = cdis_toggle_binding_released,
};

#define NCTOG_INST(n)                                                                              \
    static const struct behavior_cdis_toggle_config behavior_cdis_toggle_config_##n = {         \
        .action = DT_ENUM_IDX(DT_DRV_INST(n), toggle_action),                                     \
        .ime_keycode = DT_INST_PROP(n, ime_keycode),                                               \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_cdis_toggle_config_##n, POST_KERNEL,  \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_cdis_toggle_driver_api);

DT_INST_FOREACH_STATUS_OKAY(NCTOG_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
