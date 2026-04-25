# Reference

This document collects the detailed usage and customization options that were intentionally removed from `README.md`.
Examples use plain DTS and plain ZMK-style keymap snippets.

Japanese version: [reference.md](reference.md)

## Preset Includes

Available preset includes:

- `#include <behaviors/cdis-naginata.dtsi>`
- `#include <behaviors/cdis-nicola.dtsi>`

`cdis-naginata.dtsi` includes built-in combo tables for yoon, dakuten, handakuten, and kogaki.
`cdis-nicola.dtsi` provides the standard NICOLA character and thumb behaviors.

## Core Behaviors

Common behavior names:

- `&cdis_on`: turn a chord input layer on and tap the configured IME-on key
- `&cdis_off`: turn a chord input layer off and tap the configured IME-off key
- `&cdis_thumb_l`, `&cdis_thumb_r`: thumb-shift behaviors
- `&cdis_*`: character behaviors defined by a preset or by your own `CHORD_INPUT_BEHAVIOR*` macro
- `&cdis_pt_word`: temporary ASCII passthrough mode when provided by the preset

## Layer Placement

The chord input layer must be the lowest non-default layer.
This is important because ZMK resolves bindings from higher layers first.

```c
#define LAYER_BASE 0
#define LAYER_CDIS 1
#define LAYER_NUM 2
#define LAYER_FN 3
```

## Rearranging Keys in the Keymap

To change key positions, keep the behavior definitions and rearrange where they are used in `bindings`.

```dts
nicola_layer {
    bindings = <
        &cdis_kuten   &cdis_ka   &cdis_ta   &cdis_ko   &cdis_sa
        &cdis_ra      &cdis_ti   &cdis_ku   &cdis_tu   &cdis_comma
        &cdis_touten  &cdis_dakuten
        &cdis_si      &cdis_u    &cdis_te   &cdis_ke   &cdis_se
        &cdis_ha      &cdis_to   &cdis_ki   &cdis_i    &cdis_nn
        &cdis_period  &cdis_hi   &cdis_su   &cdis_hu   &cdis_he
        &cdis_me      &cdis_so   &cdis_ne   &cdis_ho   &cdis_nakaguro
                            &cdis_thumb_l SPACE  &cdis_thumb_r SPACE
    >;
};
```

## Overriding Kana on Existing Behaviors

After including a preset, you can override the `kana` property of an existing behavior.

```dts
#include <behaviors/cdis-nicola.dtsi>

&cdis_ka {
    kana = <KANA_KA KANA_E KANA_GI>;
};

&cdis_kuten {
    kana = <KANA_WA KANA_SMALL_A KANA_SMALL_A>;
};
```

The `kana` property is an array of 3 to 5 values:

- `<unshifted shift0 shift1>`
- `<unshifted shift0 shift1 shift2>`
- `<unshifted shift0 shift1 shift2 shift3>`

Kana constants are defined in `include/zmk-chordis/keys.h`.

## Defining Custom Character Behaviors

Use the helper macros from `#include <zmk-chordis/chord-input-map.h>`.

```dts
#include <zmk-chordis/keys.h>
#include <zmk-chordis/chord-input-map.h>

/ {
    behaviors {
        CHORD_INPUT_BEHAVIOR(my_wa, KANA_WA, KANA_WO, KANA_NN)
        CHORD_INPUT_BEHAVIOR4(my_ka, KANA_KA, KANA_GA, KANA_E, KANA_KYA)
        CHORD_INPUT_BEHAVIOR5(my_ta, KANA_TA, KANA_DA, KANA_RI, KANA_TYA, KANA_NONE)
    };
};
```

These become `&cdis_my_wa`, `&cdis_my_ka`, and `&cdis_my_ta` in your keymap.

## Mixing Non-Kana Keycodes with Kana Slots

Use `CDIS_KEY()` to place a raw ZMK keycode into a kana slot.

```dts
#include <zmk-chordis/keys.h>
#include <zmk-chordis/chord-input-map.h>

/ {
    behaviors {
        CHORD_INPUT_BEHAVIOR5(my_bspc, CDIS_KEY(BSPC), KANA_SA, KANA_NONE, KANA_NONE, KANA_NONE)
        CHORD_INPUT_BEHAVIOR5(my_left, CDIS_KEY(LEFT), CDIS_KEY(LEFT), KANA_NONE, KANA_NONE, KANA_NONE)
    };
};
```

This is how presets like Naginata place navigation or backspace on character positions.

## Thumb Tap Behavior

Thumb behaviors default to `&kp` and receive the tap keycode from the keymap.

```dts
&cdis_thumb_l SPACE
&cdis_thumb_r SPACE
```

You can override the underlying tap behavior on the node itself:

```dts
&cdis_thumb_r {
    bindings = <&mkp>;
};
```

Then the keymap can pass a mouse button or another parameter that matches the behavior.

## Built-In Hold-Tap for Character Keys

Combo-candidate keys should use the built-in hold-tap support instead of wrapping them in standard ZMK `hold-tap`.

The simplest pattern is to extend an existing preset behavior:

```dts
#include <behaviors/cdis-nicola.dtsi>

&cdis_si {
    hold-behavior = <&kp>;
    hold-param = <LSHFT>;
    tapping-term-ms = <220>;
    quick-tap-ms = <150>;
};
```

If you need a separate plain and hold-tap version of the same kana, define a new behavior:

```dts
#include <zmk-chordis/chord-input-map.h>

/ {
    behaviors {
        CHORD_INPUT_BEHAVIOR_HT(si_lshft, KANA_SI, KANA_A, KANA_ZI, &kp, LSHFT)
    };
};
```

Available hold-tap helper variants:

- `CHORD_INPUT_BEHAVIOR_HT`
- `CHORD_INPUT_BEHAVIOR_HT_T`
- `CHORD_INPUT_BEHAVIOR4_HT`
- `CHORD_INPUT_BEHAVIOR4_HT_T`
- `CHORD_INPUT_BEHAVIOR5_HT`
- `CHORD_INPUT_BEHAVIOR5_HT_T`

## Passthrough-Word

When the preset provides `&cdis_pt_word`, you can place it on any layer to temporarily send ASCII through the base layer.

```dts
some_layer {
    bindings = <
        &cdis_pt_word
    >;
};
```

Typical configuration:

```dts
&cdis_pt_word {
    continue-list = <UNDERSCORE BACKSPACE DELETE MINUS SPACE>;
    ime-keycode = <LANG2>;
    ime-restore-keycode = <LANG1>;
};
```

## Auto-Off

Use `auto-off-keys` on `&cdis_config` to leave the chord input layer automatically when certain modifier-key combinations are pressed.

```dts
&cdis_config {
    auto-off-keys = <(MOD_LCTL|MOD_RCTL) X
                     (MOD_LCTL|MOD_RCTL) S
                     (MOD_LALT|MOD_RALT) X>;
};
```

## Passthrough for Implicit-Modifier Behaviors

Some behaviors apply modifiers internally and are not detected by the normal modifier passthrough path.
List them in `passthrough-behaviors` when needed.

```dts
&cdis_config {
    passthrough-behaviors = <&caps_word>;
};
```

## Timing Configuration

Timing options are configured on `&cdis_config`.

```dts
&cdis_config {
    timeout-ms = <150>;
    sequential-threshold = <3>;
    sequential-min-overlap-ms = <20>;
};
```

## Hold-Tap Globals

The hold-tap timing properties have global defaults on `&cdis_config`, used when a per-key behavior leaves the value at the inherit sentinel `0`. This avoids repeating the same value across every home-row-mod key.

```dts
&cdis_config {
    default-tapping-term-ms        = <180>;
    default-quick-tap-ms           = <120>;
    default-require-prior-idle-ms  = <100>;
    hold-after-partner-release-ms  = <30>;
};
```

Resolution order (per knob):

1. Per-key value on the character behavior, if non-zero.
2. Global default on `&cdis_config`.
3. Built-in fallback (`default-tapping-term-ms = 200`, `default-quick-tap-ms = 0`, `default-require-prior-idle-ms = 0`, `hold-after-partner-release-ms = 30`).

`hold-after-partner-release-ms` is a short grace period after a plain combo partner is released during which an HT-undecided peer can still flip the press to a combo. It never makes HOLD commit earlier than `tapping-term-ms`. If the partner release happens near the `tapping-term-ms` boundary, this can delay HOLD slightly beyond the tapping term. Keep it much smaller than `tapping-term-ms`; it is meant to absorb release-order jitter and avoid unintended modifier activation, not to define the primary hold/tap threshold.

For each HT, the HOLD commit deadline is:

```text
max(HT pressed + tapping-term-ms,
    partner released + hold-after-partner-release-ms)
```

When several HT-undecided keys are pending on the same plain partner release, the engine uses the latest effective deadline among them. This prevents a short-window HT from truncating another HT that intentionally waits longer for combo release.

`tapping-term-ms`, `quick-tap-ms`, `require-prior-idle-ms`, and `hold-after-partner-release-ms` may be overridden per key.

To explicitly disable `quick-tap-ms` on a single key while a non-zero global is active, set the per-key value to `<1>` (any non-zero value bypasses inheritance, and `1` is functionally a no-op for repeat protection).

`require-prior-idle-ms` is rolling-input protection: an HT key pressed less than this many milliseconds after the previous key release is forced to tap. The previous release may be from the same physical key. To explicitly disable it on a single key while a non-zero global is active, set that key's value to `<1>`.

## Combo Tables

Naginata includes combo tables automatically through its preset include.
If you are defining your own layout, combo nodes use the `zmk,chord-input-char-combo` compatible.

```dts
/ {
    cdis_combo_syo: cdis_combo_syo {
        compatible = "zmk,chord-input-char-combo";
        kana-a = <KANA_SI>;
        kana-b = <KANA_YO>;
        result = <KANA_SYO>;
    };
};
```

See also:

- [getting-started.md](getting-started.md)
- [design.md](design.md)
