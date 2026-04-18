/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * NICOLA thumb key behavior.
 * Each instance is configured with a side (left/right) and a tap binding
 * (e.g. &kp SPACE). The keymap supplies the tap parameter via binding-cells.
 */

#define DT_DRV_COMPAT zmk_behavior_chord_input_thumb

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk-chordis/chordis_engine.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_cdis_thumb_config {
    enum chordis_thumb_side side;
    const char *tap_behavior_dev;
};

static int cdis_thumb_binding_pressed(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_cdis_thumb_config *cfg = dev->config;

    /* Build the tap binding: device name from DT, param from keymap cell */
    struct zmk_behavior_binding tap = {
        .behavior_dev = cfg->tap_behavior_dev,
        .param1 = binding->param1,
    };

    LOG_DBG(">>> THUMB PRESSED pos=%d side=%d param1=0x%08x dev=%s",
            event.position, cfg->side, binding->param1, cfg->tap_behavior_dev);

    chordis_on_thumb_pressed(cfg->side, &tap, event);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int cdis_thumb_binding_released(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_cdis_thumb_config *cfg = dev->config;

    LOG_DBG(">>> THUMB RELEASED pos=%d side=%d", event.position, cfg->side);

    chordis_on_thumb_released(cfg->side, event);

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_cdis_thumb_driver_api = {
    .binding_pressed = cdis_thumb_binding_pressed,
    .binding_released = cdis_thumb_binding_released,
};

#define NCT_INST(n)                                                                                \
    static const struct behavior_cdis_thumb_config behavior_cdis_thumb_config_##n = {           \
        .side = DT_ENUM_IDX(DT_DRV_INST(n), side),                                                \
        .tap_behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, 0)),               \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_cdis_thumb_config_##n, POST_KERNEL,   \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_cdis_thumb_driver_api);

DT_INST_FOREACH_STATUS_OKAY(NCT_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
