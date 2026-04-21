/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * zmk-chord-input-system helper macros for DT behavior definitions.
 */

#ifndef ZMK_CHORDIS_HELPERS_H
#define ZMK_CHORDIS_HELPERS_H

/*
 * ZMK_NC_PASSTHROUGH: create a passthrough behavior instance.
 * When any modifier is active, falls through to the base layer.
 * Otherwise invokes the specified binding.
 *
 * Usage:
 *   ZMK_NC_PASSTHROUGH(cdis_pt_ctrl, bindings = <&kp LCTRL>;)
 *   ZMK_NC_PASSTHROUGH(cdis_pt_scrl, bindings = <&mo LY_SCRL>;)
 *
 * In keymap:
 *   &cdis_pt_ctrl   // mod active -> base layer key; no mod -> LCTRL
 */
#define ZMK_BEHAVIOR_CORE_cdis_passthrough \
    compatible = "zmk,behavior-chord-input-passthrough"; \
    #binding-cells = <0>

#define ZMK_NC_PASSTHROUGH(name, ...) \
    / { \
        behaviors { \
            name: name { \
                ZMK_BEHAVIOR_CORE_cdis_passthrough; \
                __VA_ARGS__ \
            }; \
        }; \
    };

#endif /* ZMK_CHORDIS_HELPERS_H */
