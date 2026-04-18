/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * NICOLA passthrough-word behavior.
 *
 * Temporarily switches NICOLA keys to ASCII passthrough mode.
 * While active, NICOLA character keys fall through to the base layer
 * (detected via chordis_is_passthrough_behavior_active()).
 *
 * On activation, taps a configurable IME keycode (default: LANG2 /
 * japanese_eisuu) to switch the IME from kana to alphabet input mode
 * during an active composition.
 *
 * Deactivates automatically when a non-continuation key is pressed.
 * Continuation keys: A-Z, 0-9, modifier keys, and user-configurable
 * continue-list entries.
 *
 * The continue-list and strict-modifiers semantics are compatible with
 * zmk-shift-word.
 */

#define DT_DRV_COMPAT zmk_behavior_chord_input_passthrough_word

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/modifiers.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* zmk_key_param backport (same as zmk-shift-word) */
#ifndef ZMK_KEY_PARAM_DECODE
struct zmk_key_param {
    zmk_mod_flags_t modifiers;
    uint8_t page;
    uint16_t id;
};

#define ZMK_KEY_PARAM_DECODE(param)                                                                \
    (struct zmk_key_param) {                                                                       \
        .modifiers = SELECT_MODS(param), .page = ZMK_HID_USAGE_PAGE(param),                        \
        .id = ZMK_HID_USAGE_ID(param),                                                             \
    }
#endif /* ZMK_KEY_PARAM_DECODE */

struct key_list {
    size_t size;
    struct zmk_key_param keys[];
};

struct behavior_cdis_pt_word_config {
    uint32_t ime_keycode;         /* keycode to tap on activation (e.g. LANG2) */
    uint32_t ime_restore_keycode; /* keycode to tap on deactivation (e.g. LANG1) */
    const struct key_list *continue_keys;
    bool strict_modifiers;
};

struct behavior_cdis_pt_word_data {
    bool active;              /* must be first — read by caps_word_detect.h */
};

static int pt_word_keycode_state_changed_listener(const zmk_event_t *eh);

ZMK_LISTENER(behavior_cdis_pt_word, pt_word_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_cdis_pt_word, zmk_keycode_state_changed);

/* Collect all instances for the listener to iterate */
#define GET_DEV(inst) DEVICE_DT_INST_GET(inst),
static const struct device *devs[] = {DT_INST_FOREACH_STATUS_OKAY(GET_DEV)};

static bool is_alpha(uint16_t usage_page, uint32_t keycode) {
    if (usage_page != HID_USAGE_KEY) {
        return false;
    }
    return keycode >= HID_USAGE_KEY_KEYBOARD_A &&
           keycode <= HID_USAGE_KEY_KEYBOARD_Z;
}

static bool is_numeric(uint16_t usage_page, uint32_t keycode) {
    if (usage_page != HID_USAGE_KEY) {
        return false;
    }
    return ((keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
             keycode <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) ||
            (keycode >= HID_USAGE_KEY_KEYPAD_1_AND_END &&
             keycode <= HID_USAGE_KEY_KEYPAD_0_AND_INSERT)) ||
           keycode == HID_USAGE_KEY_KEYPAD_00 || keycode == HID_USAGE_KEY_KEYPAD_000;
}

static bool pt_word_is_mod(uint16_t usage_page, uint32_t keycode) {
    if (usage_page != HID_USAGE_KEY) {
        return false;
    }
    return (keycode >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL &&
            keycode <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI);
}

static bool key_list_contains(const struct key_list *list, uint16_t usage_page,
                              uint32_t usage_id, zmk_mod_flags_t modifiers) {
    for (int i = 0; i < list->size; i++) {
        const struct zmk_key_param *key = &list->keys[i];
        if (key->page == usage_page && key->id == usage_id &&
            (key->modifiers & modifiers) == key->modifiers) {
            return true;
        }
    }
    return false;
}

static bool pt_word_should_continue(const struct behavior_cdis_pt_word_config *cfg,
                                    struct zmk_keycode_state_changed *ev) {
    zmk_mod_flags_t explicit_mods = zmk_hid_get_explicit_mods();
    zmk_mod_flags_t modifiers = ev->implicit_modifiers | explicit_mods;
    bool bypass_builtin = cfg->strict_modifiers && explicit_mods;

    if (pt_word_is_mod(ev->usage_page, ev->keycode)) {
        return true;
    }

    if (!bypass_builtin) {
        if (is_alpha(ev->usage_page, ev->keycode) ||
            is_numeric(ev->usage_page, ev->keycode)) {
            return true;
        }
    }

    return key_list_contains(cfg->continue_keys, ev->usage_page, ev->keycode, modifiers);
}

static void deactivate(struct behavior_cdis_pt_word_data *data,
                       const struct behavior_cdis_pt_word_config *cfg,
                       int64_t timestamp) {
    data->active = false;
    LOG_DBG("passthrough-word deactivated");

    /* Tap the restore keycode to switch IME back to kana mode */
    if (cfg->ime_restore_keycode != 0) {
        LOG_DBG("passthrough-word: tapping restore keycode 0x%08x",
                cfg->ime_restore_keycode);
        raise_zmk_keycode_state_changed_from_encoded(
            cfg->ime_restore_keycode, true, timestamp);
        raise_zmk_keycode_state_changed_from_encoded(
            cfg->ime_restore_keycode, false, timestamp);
    }
}

static int pt_word_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < ARRAY_SIZE(devs); i++) {
        struct behavior_cdis_pt_word_data *data =
            (struct behavior_cdis_pt_word_data *)devs[i]->data;
        if (!data->active) {
            continue;
        }

        const struct behavior_cdis_pt_word_config *cfg = devs[i]->config;

        if (!pt_word_should_continue(cfg, ev)) {
            LOG_DBG("passthrough-word: deactivating for 0x%02X - 0x%02X",
                    ev->usage_page, ev->keycode);
            deactivate(data, cfg, ev->timestamp);
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

/* Binding press: toggle active state */
static int pt_word_binding_pressed(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_cdis_pt_word_config *cfg = dev->config;
    struct behavior_cdis_pt_word_data *data =
        (struct behavior_cdis_pt_word_data *)dev->data;

    if (data->active) {
        deactivate(data, cfg, event.timestamp);
    } else {
        /* Tap the IME keycode first, before setting active — otherwise
         * the listener would see this keycode and immediately deactivate. */
        if (cfg->ime_keycode != 0) {
            LOG_DBG("passthrough-word: tapping IME keycode 0x%08x", cfg->ime_keycode);
            raise_zmk_keycode_state_changed_from_encoded(
                cfg->ime_keycode, true, event.timestamp);
            raise_zmk_keycode_state_changed_from_encoded(
                cfg->ime_keycode, false, event.timestamp);
        }

        data->active = true;
        LOG_DBG("passthrough-word activated");
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int pt_word_binding_released(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_cdis_pt_word_driver_api = {
    .binding_pressed = pt_word_binding_pressed,
    .binding_released = pt_word_binding_released,
};

#define KEY_LIST_ITEM(i, n, prop) ZMK_KEY_PARAM_DECODE(DT_INST_PROP_BY_IDX(n, prop, i))

#define PROP_KEY_LIST(n, prop)                                                                     \
    COND_CODE_1(DT_NODE_HAS_PROP(DT_DRV_INST(n), prop),                                            \
                ({                                                                                 \
                    .size = DT_INST_PROP_LEN(n, prop),                                             \
                    .keys = {LISTIFY(DT_INST_PROP_LEN(n, prop), KEY_LIST_ITEM, (, ), n, prop)},    \
                }),                                                                                \
                ({.size = 0}))

#define PT_WORD_INST(n)                                                                            \
    static struct behavior_cdis_pt_word_data behavior_cdis_pt_word_data_##n = {                \
        .active = false,                                                                           \
    };                                                                                             \
    static const struct key_list pt_word_continue_list_##n =                                       \
        PROP_KEY_LIST(n, continue_list);                                                           \
    static const struct behavior_cdis_pt_word_config                                             \
        behavior_cdis_pt_word_config_##n = {                                                     \
            .ime_keycode = DT_INST_PROP_OR(n, ime_keycode, 0),                                    \
            .ime_restore_keycode = DT_INST_PROP_OR(n, ime_restore_keycode, 0),                    \
            .continue_keys = &pt_word_continue_list_##n,                                           \
            .strict_modifiers = DT_INST_PROP(n, strict_modifiers),                                 \
        };                                                                                         \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &behavior_cdis_pt_word_data_##n,                     \
                            &behavior_cdis_pt_word_config_##n, POST_KERNEL,                      \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_cdis_pt_word_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PT_WORD_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
