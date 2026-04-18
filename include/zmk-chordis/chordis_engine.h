/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * NICOLA shared state machine — header.
 * Used by behavior_cdis.c (char keys), behavior_cdis_thumb.c (thumb keys),
 * and behavior_cdis_toggle.c (mode switch flush).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zmk/behavior.h>
#include <zmk-chordis/keys.h>

/* ── State machine states ─────────────────────────────────── */

enum chordis_state_id {
    CHORDIS_IDLE,
    CHORDIS_CHAR_WAIT,        /* char key buffered, waiting for thumb */
    CHORDIS_CHAR_THUMB_WAIT,  /* char+thumb buffered, waiting for 2nd char or release (5.4.2/5.5.2) */
    CHORDIS_THUMB_HELD,       /* thumb held, shift mode active */
    CHORDIS_THUMB_CHAR_WAIT,  /* thumb held + char buffered, waiting for combo partner */
};

enum chordis_thumb_side {
    CHORDIS_SHIFT_0 = 0,   /* e.g. left thumb */
    CHORDIS_SHIFT_1 = 1,   /* e.g. right thumb */
    CHORDIS_SHIFT_2 = 2,   /* e.g. grid shift key A */
    CHORDIS_SHIFT_3 = 3,   /* e.g. grid shift key B */
};

/* Backwards compat aliases */
#define CHORDIS_SIDE_LEFT  CHORDIS_SHIFT_0
#define CHORDIS_SIDE_RIGHT CHORDIS_SHIFT_1

/* Max number of shift types. kana arrays are CHORDIS_MAX_SHIFTS + 1 elements
 * (index 0 = unshifted, index 1..4 = shift 0..3). */
#define CHORDIS_MAX_SHIFTS 4
#define CHORDIS_KANA_SLOTS (CHORDIS_MAX_SHIFTS + 1)

/* ── Configuration (read from DT cdis_config node) ──────────── */

struct chordis_timing_config {
    uint32_t timeout_ms;
    uint32_t sequential_threshold;
    uint32_t sequential_min_overlap_ms;
};

/* ── Hold-tap config (per char key, optional) ─────────────── */

struct chordis_hold_config {
    struct zmk_behavior_binding binding;     /* hold action, e.g. &kp LSHFT */
    uint32_t tapping_term_ms;                /* time to confirm hold */
    uint32_t quick_tap_ms;                   /* repeat-typing protection */
    /* hold-required-key-positions (Slice G).
     * Pointer to a list of key positions that may act as hold partners.
     * Stricter than ZMK's hold-trigger-key-positions: if the first non-self
     * key pressed during the undecided window is NOT in this list, hold is
     * denied immediately and the press resolves as a plain tap. NULL or
     * count==0 disables the restriction (any partner permits hold). */
    const int32_t *hold_required_positions;
    uint32_t       hold_required_positions_count;
};

/* ── Public API ───────────────────────────────────────────── */

/**
 * Initialise the NICOLA engine.
 * Must be called once during module init (from any behavior init callback).
 * Safe to call multiple times (idempotent).
 */
void chordis_engine_init(void);

/**
 * Character key pressed.
 * @param kana  Array of CHORDIS_KANA_SLOTS kana codes
 *              [unshifted, shift0, shift1, shift2, shift3].
 *              Unused shift slots should be KANA_NONE.
 * @param event The binding event (position, timestamp, …).
 * @param hold  Optional hold-tap config; NULL = key has no hold action.
 */
void chordis_on_char_pressed(const uint32_t kana[CHORDIS_KANA_SLOTS],
                            struct zmk_behavior_binding_event event,
                            const struct chordis_hold_config *hold);

/**
 * Returns true if any tracker has a confirmed hold action active.
 * Used by behavior_cdis.c to short-circuit further char processing
 * (passthrough to base layer while a NICOLA-driven mod is held).
 */
bool chordis_is_hold_active(void);

/**
 * Character key released.  Usually a no-op (tap output already sent).
 */
void chordis_on_char_released(struct zmk_behavior_binding_event event);

/**
 * Thumb key pressed.
 * @param side  Which thumb (LEFT / RIGHT).
 * @param tap   The binding to invoke for a plain thumb-tap (e.g. &kp SPACE).
 * @param event The binding event.
 */
void chordis_on_thumb_pressed(enum chordis_thumb_side side,
                             const struct zmk_behavior_binding *tap,
                             struct zmk_behavior_binding_event event);

/**
 * Thumb key released.
 * @param side  Which thumb.
 * @param event The binding event.
 */
void chordis_on_thumb_released(enum chordis_thumb_side side,
                              struct zmk_behavior_binding_event event);

/**
 * Flush the state machine — output any buffered key as unshifted / thumb-tap,
 * cancel timers, and return to IDLE.
 * Called by &cdis_off before deactivating the NICOLA layer.
 */
void chordis_engine_flush(int64_t timestamp);

/**
 * Output a single kana code as HID key events (tap: press+release).
 * Handles plain kana, shifted kana (small), dakuten, handakuten.
 * Can be called from any behavior (e.g. &cdis_kana for thumb tap kana output).
 */
void chordis_output_kana(uint32_t kana_code, int64_t timestamp);

/**
 * Set the active NICOLA layer number.
 * Called by &cdis_on when activating NICOLA mode.
 * Used by the auto-off feature to deactivate the correct layer.
 */
void chordis_set_active_layer(int layer);

/**
 * Clear the active NICOLA layer (set to -1).
 * Called by &cdis_off when deactivating NICOLA mode.
 */
void chordis_clear_active_layer(void);

/* ── Character combo support ────────────────────────────────── */

/**
 * Maximum number of character-character combos (e.g. き+や → きゃ).
 * Set via Kconfig CONFIG_ZMK_CHORDIS_MAX_CHAR_COMBOS.
 */
#ifndef CONFIG_ZMK_CHORDIS_MAX_CHAR_COMBOS
#define CONFIG_ZMK_CHORDIS_MAX_CHAR_COMBOS 64
#endif

/**
 * Register a character combo: when kana_a and kana_b are pressed
 * simultaneously (within the CHAR_WAIT window), output result instead.
 * The combo is bidirectional (a+b and b+a both trigger).
 * Called during DT init (e.g. from a combo config node).
 */
void chordis_register_char_combo(uint32_t kana_a, uint32_t kana_b, uint32_t result);
