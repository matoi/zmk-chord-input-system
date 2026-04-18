/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * NICOLA layout → kana mapping macro.
 * Used in nicola.dtsi to define all 30 character key behavior instances.
 */

#pragma once

#include <zmk-chordis/keys.h>

/*
 * Helper macros for defining NICOLA character key behavior instances.
 *
 * CHORD_INPUT_BEHAVIOR(name, unshifted, shift0, shift1)
 *   — 3-face layout (standard NICOLA: unshifted + left/right thumb)
 *
 * CHORD_INPUT_BEHAVIOR4(name, unshifted, shift0, shift1, shift2)
 *   — 4-face layout (e.g. + one grid shift key)
 *
 * CHORD_INPUT_BEHAVIOR5(name, unshifted, shift0, shift1, shift2, shift3)
 *   — 5-face layout (e.g. + two grid shift keys for yōon)
 */
#define CHORD_INPUT_BEHAVIOR(name, unshifted, shift0, shift1) \
    cdis_##name: cdis_##name { \
        compatible = "zmk,behavior-chord-input"; \
        #binding-cells = <0>; \
        kana = <unshifted shift0 shift1>; \
    };

#define CHORD_INPUT_BEHAVIOR4(name, unshifted, shift0, shift1, shift2) \
    cdis_##name: cdis_##name { \
        compatible = "zmk,behavior-chord-input"; \
        #binding-cells = <0>; \
        kana = <unshifted shift0 shift1 shift2>; \
    };

#define CHORD_INPUT_BEHAVIOR5(name, unshifted, shift0, shift1, shift2, shift3) \
    cdis_##name: cdis_##name { \
        compatible = "zmk,behavior-chord-input"; \
        #binding-cells = <0>; \
        kana = <unshifted shift0 shift1 shift2 shift3>; \
    };

/*
 * Hold-tap variants — attach a hold action to a chord-input char key. The
 * engine resolves tap vs hold internally so combos still work on the same key.
 *
 * CHORD_INPUT_BEHAVIOR_HT(name, unshifted, shift0, shift1, hold_bhv, hold_param)
 *   3-face char + hold action. tapping-term-ms defaults to 200, quick-tap-ms
 *   defaults to 0 (set via the YAML binding). Example:
 *
 *     CHORD_INPUT_BEHAVIOR_HT(si_lshft, KANA_SI, KANA_A, KANA_ZI, &kp, LSHFT)
 *
 * CHORD_INPUT_BEHAVIOR4_HT(name, unshifted, shift0, shift1, shift2, hold_bhv, hold_param)
 *   4-face char + hold action.
 *
 * CHORD_INPUT_BEHAVIOR5_HT(name, unshifted, shift0, shift1, shift2, shift3, hold_bhv, hold_param)
 *   5-face char + hold action.
 *
 * CHORD_INPUT_BEHAVIOR_HT_T(name, unshifted, shift0, shift1, hold_bhv, hold_param,
 *                      tapping_term_ms, quick_tap_ms)
 *   Same as CHORD_INPUT_BEHAVIOR_HT but with explicit timing values. Example:
 *
 *     CHORD_INPUT_BEHAVIOR_HT_T(si_lshft, KANA_SI, KANA_A, KANA_ZI, &kp, LSHFT, 220, 150)
 *
 * CHORD_INPUT_BEHAVIOR4_HT_T / CHORD_INPUT_BEHAVIOR5_HT_T
 *   4-face / 5-face variants with explicit timing values.
 */
#define CHORD_INPUT_BEHAVIOR_HT(name, unshifted, shift0, shift1, hold_bhv, hold_param) \
    cdis_##name: cdis_##name { \
        compatible = "zmk,behavior-chord-input"; \
        #binding-cells = <0>; \
        kana = <unshifted shift0 shift1>; \
        hold-behavior = <hold_bhv>; \
        hold-param = <hold_param>; \
    };

#define CHORD_INPUT_BEHAVIOR4_HT(name, unshifted, shift0, shift1, shift2, hold_bhv, hold_param) \
    cdis_##name: cdis_##name { \
        compatible = "zmk,behavior-chord-input"; \
        #binding-cells = <0>; \
        kana = <unshifted shift0 shift1 shift2>; \
        hold-behavior = <hold_bhv>; \
        hold-param = <hold_param>; \
    };

#define CHORD_INPUT_BEHAVIOR5_HT(name, unshifted, shift0, shift1, shift2, shift3, hold_bhv, hold_param) \
    cdis_##name: cdis_##name { \
        compatible = "zmk,behavior-chord-input"; \
        #binding-cells = <0>; \
        kana = <unshifted shift0 shift1 shift2 shift3>; \
        hold-behavior = <hold_bhv>; \
        hold-param = <hold_param>; \
    };

#define CHORD_INPUT_BEHAVIOR_HT_T(name, unshifted, shift0, shift1, hold_bhv, hold_param, tt, qt) \
    cdis_##name: cdis_##name { \
        compatible = "zmk,behavior-chord-input"; \
        #binding-cells = <0>; \
        kana = <unshifted shift0 shift1>; \
        hold-behavior = <hold_bhv>; \
        hold-param = <hold_param>; \
        tapping-term-ms = <tt>; \
        quick-tap-ms = <qt>; \
    };

#define CHORD_INPUT_BEHAVIOR4_HT_T(name, unshifted, shift0, shift1, shift2, hold_bhv, hold_param, tt, qt) \
    cdis_##name: cdis_##name { \
        compatible = "zmk,behavior-chord-input"; \
        #binding-cells = <0>; \
        kana = <unshifted shift0 shift1 shift2>; \
        hold-behavior = <hold_bhv>; \
        hold-param = <hold_param>; \
        tapping-term-ms = <tt>; \
        quick-tap-ms = <qt>; \
    };

#define CHORD_INPUT_BEHAVIOR5_HT_T(name, unshifted, shift0, shift1, shift2, shift3, hold_bhv, hold_param, tt, qt) \
    cdis_##name: cdis_##name { \
        compatible = "zmk,behavior-chord-input"; \
        #binding-cells = <0>; \
        kana = <unshifted shift0 shift1 shift2 shift3>; \
        hold-behavior = <hold_bhv>; \
        hold-param = <hold_param>; \
        tapping-term-ms = <tt>; \
        quick-tap-ms = <qt>; \
    };
