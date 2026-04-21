/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Passthrough behavior detection for NICOLA.
 *
 * Two sources of passthrough triggers:
 *
 * 1. Built-in: &cdis_pt_word (zmk,behavior-chord-input-passthrough-word).
 *    Detected automatically when the compatible is present in DTS.
 *
 * 2. User-configured: external behaviors like &caps_word / &shift_word
 *    listed in the cdis-config `passthrough-behaviors` phandle property.
 *    These must have `bool active` as the first field of their device
 *    data struct.
 */

#pragma once

#include <stdbool.h>
#include <zephyr/device.h>

/*
 * Built-in: passthrough-word behavior.
 * Check all instances of zmk,behavior-chord-input-passthrough-word.
 */
#if DT_HAS_COMPAT_STATUS_OKAY(zmk_behavior_chord_input_passthrough_word)

struct cdis_pt_word_data_peek {
    bool active;
};

#define _CDIS_PT_WORD_DEV(node_id) DEVICE_DT_GET(node_id),

static const struct device *cdis_pt_word_devs[] = {
    DT_FOREACH_STATUS_OKAY(zmk_behavior_chord_input_passthrough_word, _CDIS_PT_WORD_DEV)
};

static inline bool chordis_is_pt_word_active(void) {
    for (int i = 0; i < ARRAY_SIZE(cdis_pt_word_devs); i++) {
        const struct device *dev = cdis_pt_word_devs[i];
        if (dev == NULL || dev->data == NULL) continue;
        if (((const struct cdis_pt_word_data_peek *)dev->data)->active) {
            return true;
        }
    }
    return false;
}

#else

static inline bool chordis_is_pt_word_active(void) { return false; }

#endif

/*
 * User-configured: external passthrough behaviors (caps_word, shift_word, etc.)
 * listed in cdis_config { passthrough-behaviors = <&caps_word ...>; };
 */
#define CDIS_CONFIG_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zmk_chord_input_config)

#if DT_NODE_HAS_PROP(CDIS_CONFIG_NODE, passthrough_behaviors)

struct cdis_pt_behavior_data {
    bool active;
};

#define _CDIS_PT_DEV(node_id, prop, idx) \
    DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

static const struct device *cdis_pt_behavior_devs[] = {
    DT_FOREACH_PROP_ELEM(CDIS_CONFIG_NODE, passthrough_behaviors, _CDIS_PT_DEV)
};

static inline bool chordis_is_ext_passthrough_active(void) {
    for (int i = 0; i < ARRAY_SIZE(cdis_pt_behavior_devs); i++) {
        const struct device *dev = cdis_pt_behavior_devs[i];
        if (dev == NULL || dev->data == NULL) continue;
        if (((const struct cdis_pt_behavior_data *)dev->data)->active) {
            return true;
        }
    }
    return false;
}

#else

static inline bool chordis_is_ext_passthrough_active(void) { return false; }

#endif

/* Combined check: any passthrough behavior active? */
static inline bool chordis_is_passthrough_behavior_active(void) {
    return chordis_is_pt_word_active() || chordis_is_ext_passthrough_active();
}
