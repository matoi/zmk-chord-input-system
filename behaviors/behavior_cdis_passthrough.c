/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * NICOLA passthrough behavior.
 * Wraps an arbitrary binding (e.g. &kp LCTRL, &mo LY_SCRL) and adds
 * modifier-aware passthrough: when any modifier is physically held,
 * returns TRANSPARENT so the base layer key is sent instead.
 *
 * This solves the problem where non-nicola keys on the NICOLA layer
 * (modifiers, layer switches, etc.) block access to base layer keys
 * when used with hold-tap modifiers.
 *
 * Usage in DTS:
 *   cdis_pt_ctrl: cdis_pt_ctrl {
 *       compatible = "zmk,behavior-chord-input-passthrough";
 *       bindings = <&kp LCTRL>;
 *   };
 *
 * In keymap:
 *   &cdis_pt_ctrl   // mod inactive -> &kp LCTRL; mod active -> base layer key
 *
 * Limitation: modifier stacking (e.g. Ctrl+Shift where both are on
 * the NICOLA layer) is not possible -- a held modifier causes all
 * passthrough keys to fall through to the base layer.
 */

#define DT_DRV_COMPAT zmk_behavior_chord_input_passthrough

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zmk-chordis/caps_word_detect.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define ANY_MODS (MOD_LCTL | MOD_RCTL | MOD_LSFT | MOD_RSFT | \
                  MOD_LALT | MOD_RALT | MOD_LGUI | MOD_RGUI)

struct behavior_cdis_passthrough_config {
    struct zmk_behavior_binding binding;
};

/* Per-position state to track how press was handled */
enum pt_mode {
    PT_NORMAL,      /* invoked inner binding */
    PT_TRANSPARENT, /* returned TRANSPARENT, base layer handles it */
};

#define MAX_POSITIONS 128
static uint8_t pt_state[MAX_POSITIONS];

static int cdis_pt_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    uint32_t pos = event.position;

    if (chordis_is_passthrough_behavior_active() ||
        (zmk_hid_get_explicit_mods() & ANY_MODS)) {
        LOG_DBG("passthrough: modifier active (0x%02x), transparent pos=%d",
                zmk_hid_get_explicit_mods(), pos);
        if (pos < MAX_POSITIONS) {
            pt_state[pos] = PT_TRANSPARENT;
        }
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_cdis_passthrough_config *cfg = dev->config;

    LOG_DBG("passthrough: invoking inner binding %s pos=%d",
            cfg->binding.behavior_dev, pos);

    if (pos < MAX_POSITIONS) {
        pt_state[pos] = PT_NORMAL;
    }

    return zmk_behavior_invoke_binding(&cfg->binding, event, true);
}

static int cdis_pt_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    uint32_t pos = event.position;
    enum pt_mode mode = (pos < MAX_POSITIONS) ? pt_state[pos] : PT_NORMAL;

    if (pos < MAX_POSITIONS) {
        pt_state[pos] = PT_NORMAL;
    }

    if (mode == PT_TRANSPARENT) {
        LOG_DBG("passthrough: transparent release pos=%d", pos);
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_cdis_passthrough_config *cfg = dev->config;

    LOG_DBG("passthrough: releasing inner binding %s pos=%d",
            cfg->binding.behavior_dev, pos);

    return zmk_behavior_invoke_binding(&cfg->binding, event, false);
}

static const struct behavior_driver_api behavior_cdis_passthrough_driver_api = {
    .binding_pressed = cdis_pt_binding_pressed,
    .binding_released = cdis_pt_binding_released,
};

#define NCPT_INST(n)                                                                               \
    static const struct behavior_cdis_passthrough_config                                         \
        behavior_cdis_passthrough_config_##n = {                                                 \
            .binding = ZMK_KEYMAP_EXTRACT_BINDING(0, DT_DRV_INST(n)),                             \
        };                                                                                        \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL,                                                  \
                            &behavior_cdis_passthrough_config_##n, POST_KERNEL,                  \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_cdis_passthrough_driver_api);

DT_INST_FOREACH_STATUS_OKAY(NCPT_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
