/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * NICOLA kana output behavior.
 * Outputs a single kana code (including dakuten/handakuten multi-keystroke)
 * as a tap. Used as thumb tap binding for kana-on-tap configurations.
 *
 * Usage in DTS:
 *   &cdis_thumb_l { bindings = <&cdis_kana>; };
 * Usage in keymap:
 *   &cdis_thumb_l KANA_GA    // thumb tap outputs が
 */

#define DT_DRV_COMPAT zmk_behavior_chord_input_kana

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk-chordis/chordis_engine.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int nicola_kana_binding_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    LOG_DBG("kana pressed param1=0x%08x", binding->param1);
    chordis_output_kana(binding->param1, event.timestamp);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int nicola_kana_binding_released(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_cdis_kana_driver_api = {
    .binding_pressed = nicola_kana_binding_pressed,
    .binding_released = nicola_kana_binding_released,
};

#define NCK_INST(n)                                                                                \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_cdis_kana_driver_api);

DT_INST_FOREACH_STATUS_OKAY(NCK_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
