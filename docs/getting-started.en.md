# Getting Started

This guide shows the minimum setup for using `zmk-chord-input-system` from a plain ZMK keymap overlay.
Examples here do not assume any shared keymap framework or custom helper macros outside this module.

Japanese version: [getting-started.md](getting-started.md)

## Before You Start

- Set your OS Japanese IME to romaji input mode.
- Put the chord input layer directly above the default layer.
- Add this module to your ZMK build first.

The layer ordering matters because ZMK resolves bindings from higher layers first. If the chord input layer sits above your normal momentary or function layers, it can consume key presses before those layers get a chance to handle them.

Example layer ordering:

```c
#define LAYER_BASE 0
#define LAYER_CDIS 1
#define LAYER_NUM 2
```

## Naginata Preset

The [Naginata](https://oookaworks.seesaa.net/article/456099128.html) preset includes character combos as part of the preset, so it is the quickest way to try the combo-capable path.

```dts
#include <behaviors/cdis-naginata.dtsi>

#define LAYER_BASE 0
#define LAYER_NAGINATA 1

/ {
    keymap {
        compatible = "zmk,keymap";

        base_layer {
            bindings = <
                &kp Q    &kp W    &kp E    &kp R    &kp T
                &kp Y    &kp U    &kp I    &kp O    &kp P
                &kp A    &kp S    &kp D    &kp F    &kp G
                &kp H    &kp J    &kp K    &kp L    &kp SEMI
                &kp Z    &kp X    &kp C    &kp V    &kp B
                &kp N    &kp M    &kp COMMA &kp DOT &kp FSLH
                                  &cdis_on LAYER_NAGINATA  &kp SPACE  &kp SPACE  &cdis_off LAYER_NAGINATA
            >;
        };

        naginata_layer {
            bindings = <
                CDIS_NAGINATA_LT  CDIS_NAGINATA_RT
                CDIS_NAGINATA_LM  CDIS_NAGINATA_RM
                CDIS_NAGINATA_LB  CDIS_NAGINATA_RB
                &cdis_on LAYER_NAGINATA  CDIS_NAGINATA_LH  CDIS_NAGINATA_RH  &cdis_off LAYER_NAGINATA
            >;
        };
    };
};
```

What the preset includes:

- Predefined Naginata character keys
- Center-shift thumb behaviors
- Yoon combos
- Dakuten and handakuten combos
- Kogaki combos
- Explicit `&cdis_on` / `&cdis_off` positions on the chord-input layer in this example, so you can manually resynchronize layer state and IME state if needed

Notes:

- In this preset, some positions output non-kana keys such as cursor movement or backspace.
- `CDIS_NAGINATA_LH` and `CDIS_NAGINATA_RH` are thumb bindings that use `SPACE` on tap.
- The example places both `&cdis_on` and `&cdis_off` on the base layer and also on the outer thumb positions of the chord-input layer. Those outer positions are there intentionally so you can resynchronize the layer and the host IME from inside the Naginata layer if they drift apart. In practice, it is often useful to combine those explicit bindings with `auto-off-keys`, so selected shortcuts can leave chord-input mode automatically while manual recovery still remains available.

The `CDIS_NAGINATA_*` names in the example are layout macros defined by `cdis-naginata.dtsi`. They expand to ordinary bindings:

```dts
/* Top row */
CDIS_NAGINATA_LT = &cdis_ng_kogaki &cdis_ng_ki   &cdis_ng_te  &cdis_ng_si   &cdis_ng_left
CDIS_NAGINATA_RT = &cdis_ng_right  &cdis_ng_bspc &cdis_ng_ru  &cdis_ng_su   &cdis_ng_he

/* Home row */
CDIS_NAGINATA_LM = &cdis_ng_ro     &cdis_ng_ke   &cdis_ng_to  &cdis_ng_daku_l  &cdis_ng_xtu
CDIS_NAGINATA_RM = &cdis_ng_ku     &cdis_ng_daku_r &cdis_ng_i &cdis_ng_u       &cdis_ng_chou

/* Bottom row */
CDIS_NAGINATA_LB = &cdis_ng_ho     &cdis_ng_hi   &cdis_ng_ha  &cdis_ng_ko      &cdis_ng_hdaku_l
CDIS_NAGINATA_RB = &cdis_ng_ta     &cdis_ng_hdaku_r &cdis_ng_nn &cdis_ng_ra    &cdis_ng_re

/* Thumbs */
CDIS_NAGINATA_LH = &cdis_thumb_l SPACE
CDIS_NAGINATA_RH = &cdis_thumb_r SPACE
```

If you prefer, you can replace those macros with the expanded bindings directly in your keymap.

The thumb bindings in the example use the usual chord-input thumb behavior: hold them as shift keys for kana selection, or tap them by themselves to send their tap keycode, which is `SPACE` in these examples.

## NICOLA Preset

If you want a standard [NICOLA](http://nicola.sunicom.co.jp/spec/kikaku.htm) layout instead of Naginata, use `cdis-nicola.dtsi`.

```dts
#include <behaviors/cdis-nicola.dtsi>

#define LAYER_BASE 0
#define LAYER_NICOLA 1

/ {
    keymap {
        compatible = "zmk,keymap";

        base_layer {
            bindings = <
                &kp Q    &kp W    &kp E    &kp R    &kp T
                &kp Y    &kp U    &kp I    &kp O    &kp P
                &kp A    &kp S    &kp D    &kp F    &kp G
                &kp H    &kp J    &kp K    &kp L    &kp SEMI
                &kp Z    &kp X    &kp C    &kp V    &kp B
                &kp N    &kp M    &kp COMMA &kp DOT &kp FSLH
                                   &cdis_on LAYER_NICOLA  &kp SPACE
            >;
        };

        nicola_layer {
            bindings = <
                CDIS_NICOLA_LT  CDIS_NICOLA_RT
                CDIS_NICOLA_LM  CDIS_NICOLA_RM
                CDIS_NICOLA_LB  CDIS_NICOLA_RB
                &cdis_on LAYER_NICOLA  CDIS_NICOLA_LH  CDIS_NICOLA_RH  &cdis_off LAYER_NICOLA
            >;
        };
    };
};
```

The NICOLA preset gives you:

- Predefined NICOLA character behaviors
- Standard NICOLA layout macros split into left and right blocks
- Left and right thumb-shift behaviors
- `&cdis_on` / `&cdis_off` toggle behaviors
- Explicit `&cdis_on` / `&cdis_off` positions on the chord-input layer in this example, so you can manually resynchronize layer state and IME state if needed

The `CDIS_NICOLA_*` names are layout macros defined by `cdis-nicola.dtsi`:

```dts
/* Top row */
CDIS_NICOLA_LT = &cdis_kuten &cdis_ka &cdis_ta &cdis_ko &cdis_sa
CDIS_NICOLA_RT = &cdis_ra &cdis_ti &cdis_ku &cdis_tu &cdis_comma &cdis_touten &cdis_dakuten

/* Home row */
CDIS_NICOLA_LM = &cdis_u &cdis_si &cdis_te &cdis_ke &cdis_se
CDIS_NICOLA_RM = &cdis_ha &cdis_to &cdis_ki &cdis_i &cdis_nn

/* Bottom row */
CDIS_NICOLA_LB = &cdis_period &cdis_hi &cdis_su &cdis_hu &cdis_he
CDIS_NICOLA_RB = &cdis_me &cdis_so &cdis_ne &cdis_ho &cdis_nakaguro

/* Thumbs */
CDIS_NICOLA_LH = &cdis_thumb_l SPACE
CDIS_NICOLA_RH = &cdis_thumb_r SPACE
```

This definition intentionally follows the standard layout even if a specific keyboard does not have enough physical positions to place every key one-to-one.

The example places both `&cdis_on` and `&cdis_off` on the base layer and also around the thumb bindings inside the chord-input layer. Those positions are there intentionally so you can resynchronize the layer and the host IME from inside the NICOLA layer if they drift apart. In practice, it is often useful to combine those explicit bindings with `auto-off-keys`, so selected shortcuts can leave chord-input mode automatically while manual recovery still remains available.

The NICOLA preset is defined against the standard layout, including positions that some compact keyboards may not have physically. That is intentional: the preset represents the standard arrangement first, and you can then remap or omit positions as needed for your actual keyboard.

## Adding Hold-Tap

If you want home row mods or similar modifier-on-hold behavior, use the built-in hold-tap support on a chord-input key instead of wrapping that key in standard ZMK `hold-tap`.

For example, this makes `&cdis_si` act as kana on tap and `LSHFT` on hold:

```dts
#include <behaviors/cdis-nicola.dtsi>

&cdis_si {
    hold-behavior = <&kp>;
    hold-param = <LSHFT>;
    tapping-term-ms = <220>;
    quick-tap-ms = <150>;
};
```

If you need a separate hold-tap variant without changing every use of `&cdis_si`, define a new behavior:

```dts
#include <zmk-chordis/chord-input-map.h>

/ {
    behaviors {
        CHORD_INPUT_BEHAVIOR_HT(si_lshft, KANA_SI, KANA_A, KANA_ZI, &kp, LSHFT)
    };
};
```

Then use `&cdis_si_lshft` only on the positions where you want the hold action.

When the hold side activates, modifier passthrough follows automatically. In other words, once the key has resolved to its hold behavior, following keys are sent through the base layer path instead of being emitted as kana. This is what makes home row mods and modifier shortcuts usable with the built-in hold-tap behavior.

## Modifier Passthrough

Modifier passthrough is a normal part of chord-input behavior. When Ctrl, Shift, Alt, GUI, or a hold-tap modifier is active, chord-input keys fall through to the base layer path instead of producing kana.

If your setup uses behaviors such as `&caps_word` that apply modifiers implicitly, list them in `passthrough-behaviors` so chord-input keys fall through correctly:

```dts
&cdis_config {
    passthrough-behaviors = <&caps_word>;
};
```

## Auto-Off For English Mode

`auto-off-keys` is not the passthrough mechanism itself. Instead, it is a convenience feature for leaving chord-input mode and returning the host IME to its non-kana state when selected shortcuts are pressed.

For example:

```dts
&cdis_config {
    auto-off-keys = <(MOD_LCTL|MOD_RCTL) X
                     (MOD_LCTL|MOD_RCTL) S
                     (MOD_LALT|MOD_RALT) X>;
};
```

This is useful when you want specific shortcuts to move you back into the normal English/alphanumeric path instead of staying in chord-input mode afterward.

In practice, this helps in two common situations:

- Multi-step shortcuts such as `C-x ...` or `M-x ...`, where you want the prefix to be handled from chord-input mode but the following keys to be typed on the normal alphanumeric path
- IME sync drift, where an application may switch the host IME independently; `auto-off-keys` does not solve that completely, but it can reduce how often the layer and the IME end up out of sync

## Passthrough Word

If your preset provides `&cdis_pt_word`, you can place it on a key to temporarily switch from kana input to ASCII passthrough for a word or short run of text. This is useful when you want to type Latin text without leaving the chord-input layer completely.

This behavior depends on the host IME reacting as expected to the configured IME switch keycodes. In the current presets, that typically means `LANG2` / `japanese_eisuu` on activation and `LANG1` / `japanese_kana` on restore. Depending on the OS, IME, and keyboard settings, temporary ASCII mode may not behave exactly as intended or may not switch modes at all.

```dts
some_layer {
    bindings = <
        &cdis_pt_word
    >;
};
```

## First Checks

After flashing:

1. Turn the input layer on from the base layer.
2. Confirm that the OS IME is active and in romaji mode.
3. Try a plain character key first.
4. Try a thumb-shifted or center-shifted character.
5. On NICOLA, try a simple thumb-shifted input such as holding a thumb key while pressing a character key.
6. On Naginata, try a combo that should produce yoon or dakuten.

## Next Steps

- For customization and advanced settings, see [reference.en.md](reference.en.md).
- For the Japanese reference, see [reference.md](reference.md).
- For design details, see [design.md](design.md).
