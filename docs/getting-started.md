# Getting Started

This guide shows the minimum setup for using `zmk-chord-input-system` from a plain ZMK keymap overlay.
Examples here do not assume any shared keymap framework or custom helper macros outside this module.

## Before You Start

- Set your OS Japanese IME to romaji input mode.
- Put the chord input layer directly above the default layer.
- Add this module to your ZMK build first.

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
                NC_NAGINATA_LT  NC_NAGINATA_RT
                NC_NAGINATA_LM  NC_NAGINATA_RM
                NC_NAGINATA_LB  NC_NAGINATA_RB
                &cdis_on LAYER_NAGINATA  NC_NAGINATA_LH  NC_NAGINATA_RH  &cdis_off LAYER_NAGINATA
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
- `NC_NAGINATA_LH` and `NC_NAGINATA_RH` are thumb bindings that use `SPACE` on tap.
- The example places both `&cdis_on` and `&cdis_off` on the base layer and also on the outer thumb positions of the chord-input layer. Those outer positions are there intentionally so you can resynchronize the layer and the host IME from inside the Naginata layer if they drift apart.

The `NC_NAGINATA_*` names in the example are layout macros defined by `cdis-naginata.dtsi`. They expand to ordinary bindings:

```dts
/* Top row */
NC_NAGINATA_LT = &cdis_ng_kogaki &cdis_ng_ki   &cdis_ng_te  &cdis_ng_si   &cdis_ng_left
NC_NAGINATA_RT = &cdis_ng_right  &cdis_ng_bspc &cdis_ng_ru  &cdis_ng_su   &cdis_ng_he

/* Home row */
NC_NAGINATA_LM = &cdis_ng_ro     &cdis_ng_ke   &cdis_ng_to  &cdis_ng_daku_l  &cdis_ng_xtu
NC_NAGINATA_RM = &cdis_ng_ku     &cdis_ng_daku_r &cdis_ng_i &cdis_ng_u       &cdis_ng_chou

/* Bottom row */
NC_NAGINATA_LB = &cdis_ng_ho     &cdis_ng_hi   &cdis_ng_ha  &cdis_ng_ko      &cdis_ng_hdaku_l
NC_NAGINATA_RB = &cdis_ng_ta     &cdis_ng_hdaku_r &cdis_ng_nn &cdis_ng_ra    &cdis_ng_re

/* Thumbs */
NC_NAGINATA_LH = &cdis_thumb_l SPACE
NC_NAGINATA_RH = &cdis_thumb_r SPACE
```

If you prefer, you can replace those macros with the expanded bindings directly in your keymap.

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
                &cdis_kuten  &cdis_ka   &cdis_ta   &cdis_ko   &cdis_sa
                &cdis_ra     &cdis_ti   &cdis_ku   &cdis_tu   &cdis_comma
                &cdis_u      &cdis_si   &cdis_te   &cdis_ke   &cdis_se
                &cdis_ha     &cdis_to   &cdis_ki   &cdis_i    &cdis_nn
                &cdis_period &cdis_hi   &cdis_su   &cdis_hu   &cdis_he
                &cdis_me     &cdis_so   &cdis_ne   &cdis_ho   &cdis_nakaguro
                                   &cdis_thumb_l SPACE  &cdis_thumb_r SPACE
            >;
        };
    };
};
```

The NICOLA preset gives you:

- Predefined NICOLA character behaviors
- Left and right thumb-shift behaviors
- `&cdis_on` / `&cdis_off` toggle behaviors

If you want an explicit key to turn the layer off, add `&cdis_off LAYER_NICOLA` somewhere in the `nicola_layer`.

## First Checks

After flashing:

1. Turn the input layer on from the base layer.
2. Confirm that the OS IME is active and in romaji mode.
3. Try a plain character key first.
4. Try a thumb-shifted or center-shifted character.
5. On Naginata, try a combo that should produce yoon or dakuten.

## Next Steps

- For customization and advanced settings, see [reference.md](reference.md).
- For design details, see [design.md](design.md).
