/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * NICOLA character key behavior.
 * Each instance holds up to 5 kana codes (unshifted + up to 4 shift layers)
 * and delegates to the shared NICOLA engine on press/release.
 */

#define DT_DRV_COMPAT zmk_behavior_chord_input

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk-chordis/chordis_engine.h>
#include <zmk-chordis/caps_word_detect.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* Any modifier mask: Ctrl, Shift, Alt, GUI (both sides).
 * NICOLA users never press Shift themselves (thumb-shift instead),
 * so any modifier held means a shortcut — pass through to default layer. */
#define ANY_MODS (MOD_LCTL | MOD_RCTL | MOD_LSFT | MOD_RSFT | \
                  MOD_LALT | MOD_RALT | MOD_LGUI | MOD_RGUI)

/* Modifier passthrough tracking.
 *
 * When a modifier is held and a NICOLA key is pressed, we want the ASCII
 * keycode (from the base layer) to be sent instead of the kana.
 *
 * Two call paths exist:
 *   (a) Keymap layer walk → TRANSPARENT causes fallthrough to base layer.
 *   (b) hold-tap direct invoke → TRANSPARENT is ignored; we must send
 *       the keycode ourselves.  hold-tap passes the tap param in
 *       binding->param1 (e.g. J = 0x0007000D).
 *
 * We distinguish (a) vs (b) by checking binding->param1:
 *   - 0  → called from keymap (zero binding-cells) → return TRANSPARENT
 *   - !0 → called from hold-tap → raise the keycode directly, return OPAQUE
 */

/* Per-position state for modifier passthrough */
enum passthrough_mode {
    PT_NONE,        /* normal NICOLA processing */
    PT_TRANSPARENT, /* returned TRANSPARENT, base layer handles release */
    PT_DIRECT,      /* we sent the keycode directly, we handle release */
};

#define MAX_POSITIONS 128
static uint8_t passthrough_state[MAX_POSITIONS];
static uint32_t passthrough_keycode[MAX_POSITIONS]; /* only valid when PT_DIRECT */

struct behavior_cdis_config {
    uint32_t kana[CHORDIS_KANA_SLOTS]; /* [unshifted, shift0, shift1, ...] */
    uint32_t kana_count;               /* actual number of elements from DT */
    bool                       has_hold;
    struct chordis_hold_config  hold;   /* valid when has_hold == true */
    /* hold-required-key-positions (Slice G). Flexible array member trailing
     * the struct, mirroring ZMK's behavior_hold_tap_config layout. */
    uint32_t                   hold_required_positions_len;
    int32_t                    hold_required_positions[];
};

static int cdis_binding_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    uint32_t pos = event.position;

    /* Passthrough triggers:
     *   1. external modifier held (existing shortcut path)
     *   2. NICOLA-driven hold action active (e.g. char-key hold-tap →
     *      &kp LSHFT). The engine knows authoritatively whether any
     *      tracker resolved to TRK_HOLD; we trust that over the HID
     *      modifier state to also cover non-modifier hold bindings. */
    if (chordis_is_hold_active() ||
        chordis_is_passthrough_behavior_active() ||
        (zmk_hid_get_explicit_mods() & ANY_MODS)) {
        if (binding->param1 != 0) {
            /* Called from hold-tap — send the keycode directly.
             * param1 is the ZMK encoded keycode (e.g. J with usage page). */
            LOG_DBG("modifier active (0x%02x), direct kp 0x%08x pos=%d",
                    zmk_hid_get_explicit_mods(), binding->param1, pos);
            raise_zmk_keycode_state_changed_from_encoded(
                binding->param1, true, event.timestamp);
            if (pos < MAX_POSITIONS) {
                passthrough_state[pos] = PT_DIRECT;
                passthrough_keycode[pos] = binding->param1;
            }
            return ZMK_BEHAVIOR_OPAQUE;
        }

        /* Called from keymap — let layer walk fall through to base layer */
        LOG_DBG("modifier active (0x%02x), transparent pos=%d",
                zmk_hid_get_explicit_mods(), pos);
        if (pos < MAX_POSITIONS) {
            passthrough_state[pos] = PT_TRANSPARENT;
        }
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    if (pos < MAX_POSITIONS) {
        passthrough_state[pos] = PT_NONE;
    }

    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_cdis_config *cfg = dev->config;

    if (cfg->has_hold) {
        /* Build a local hold config so the engine sees the hrkp pointer
         * (which references the flex array tail of cfg). */
        struct chordis_hold_config hold = cfg->hold;
        hold.hold_required_positions       = cfg->hold_required_positions_len > 0
                                                ? cfg->hold_required_positions
                                                : NULL;
        hold.hold_required_positions_count = cfg->hold_required_positions_len;
        chordis_on_char_pressed(cfg->kana, event, &hold);
    } else {
        chordis_on_char_pressed(cfg->kana, event, NULL);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int cdis_binding_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    uint32_t pos = event.position;
    enum passthrough_mode mode = (pos < MAX_POSITIONS) ? passthrough_state[pos] : PT_NONE;

    if (pos < MAX_POSITIONS) {
        passthrough_state[pos] = PT_NONE;
    }

    switch (mode) {
    case PT_TRANSPARENT:
        LOG_DBG("transparent release pos=%d", pos);
        return ZMK_BEHAVIOR_TRANSPARENT;

    case PT_DIRECT:
        LOG_DBG("direct kp release 0x%08x pos=%d", passthrough_keycode[pos], pos);
        raise_zmk_keycode_state_changed_from_encoded(
            passthrough_keycode[pos], false, event.timestamp);
        return ZMK_BEHAVIOR_OPAQUE;

    default:
        chordis_on_char_released(event);
        return ZMK_BEHAVIOR_OPAQUE;
    }
}

static const struct behavior_driver_api behavior_cdis_driver_api = {
    .binding_pressed = cdis_binding_pressed,
    .binding_released = cdis_binding_released,
};

/*
 * Pad the DT kana array to CHORDIS_KANA_SLOTS with KANA_NONE.
 * DT may provide 3 elements (legacy NICOLA) or up to 5 (with extra shifts).
 */
#define _NC_KANA_PAD_1(n, i) \
    COND_CODE_1(DT_INST_PROP_HAS_IDX(n, kana, i), (DT_INST_PROP_BY_IDX(n, kana, i)), (KANA_NONE))

/* Hold-tap config helpers — guarded by COND_CODE_1 so unused arms don't
 * reference missing DT properties (which would be a build error). */
#define _NC_HOLD_DEV(n) \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, hold_behavior), \
        (DEVICE_DT_NAME(DT_INST_PHANDLE(n, hold_behavior))), (NULL))

#define _NC_HOLD_PARAM(n) \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, hold_param), \
        (DT_INST_PROP(n, hold_param)), (0))

#define NC_INST(n)                                                                                 \
    static const struct behavior_cdis_config behavior_cdis_config_##n = {                       \
        .kana = { _NC_KANA_PAD_1(n, 0), _NC_KANA_PAD_1(n, 1), _NC_KANA_PAD_1(n, 2),             \
                  _NC_KANA_PAD_1(n, 3), _NC_KANA_PAD_1(n, 4) },                                   \
        .kana_count = DT_INST_PROP_LEN(n, kana),                                                   \
        .has_hold   = DT_INST_NODE_HAS_PROP(n, hold_behavior),                                     \
        .hold = {                                                                                  \
            .binding = { .behavior_dev = _NC_HOLD_DEV(n),                                          \
                         .param1 = _NC_HOLD_PARAM(n) },                                            \
            .tapping_term_ms = DT_INST_PROP_OR(n, tapping_term_ms, 200),                           \
            .quick_tap_ms    = DT_INST_PROP_OR(n, quick_tap_ms, 0),                                \
        },                                                                                         \
        .hold_required_positions_len = DT_INST_PROP_LEN(n, hold_required_key_positions),           \
        .hold_required_positions     = DT_INST_PROP(n, hold_required_key_positions),               \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_cdis_config_##n, POST_KERNEL,         \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_cdis_driver_api);

DT_INST_FOREACH_STATUS_OKAY(NC_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
