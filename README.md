# zmk-chord-input-system

`zmk-chord-input-system` is a ZMK module for Japanese chorded kana input.
It provides a shared input engine plus ready-to-use presets for layouts such as [NICOLA](http://nicola.sunicom.co.jp/spec/kikaku.htm) and [Naginata](https://oookaworks.seesaa.net/article/456099128.html).

## Overview

This module is for ZMK users who want Japanese kana entry rules implemented in firmware instead of in an OS-side remapper.

Highlights:

- Works with plain ZMK keymaps and overlays
- Supports NICOLA, Naginata, and custom chorded kana layouts
- Aims to implement [NICOLA](http://nicola.sunicom.co.jp/spec/kikaku.htm) standard key input rules as faithfully as practical within ZMK firmware, which is a key difference from simply building kana input on top of a generic key-remapper combo feature
- Supports passthrough for normal shortcuts, so modified keys can fall through to the base layer instead of being treated as kana input
- Includes built-in hold-tap support for chord input keys, including combo-aware use cases such as home row mods
- Includes built-in char combo support for layouts that need yoon, dakuten, handakuten, or kogaki rules
- Supports up to 4 shift types and up to 5 faces per character key
- Runs on split and unibody ZMK keyboards

## Limitations and Notes

- The current output backend expects the host OS IME to be in romaji input mode.
- The chord input layer is meant to stay in sync with host-side IME on/off state.
- The chord input layer must be the lowest non-default layer, directly above the default layer.
- This module handles kana entry in firmware, but kana-to-kanji conversion still depends on the host OS IME.
- Some presets include non-kana outputs in the layout, such as cursor or backspace keys.

IME sync matters in practice. `&cdis_on` and `&cdis_off` are intended to switch the chord input layer and the host IME together, but the firmware cannot observe IME changes made independently by the OS or by an application. For example, an editor may turn IME off automatically when entering a minibuffer or command field, while the keyboard still remains on the chord input layer. In that state, the layer and the IME no longer agree, and chord input may produce unexpected output until you turn the layer or IME back to the matching state manually. This cannot be solved completely in firmware, but the module does provide settings that can reduce friction in practice, such as `auto-off-keys` for leaving chord input mode automatically on selected shortcuts. See [docs/reference.md](docs/reference.md) for those configuration options.

Built-in hold-tap is included for a similar practical reason. On combo-candidate keys, wrapping chord input behaviors in standard ZMK `hold-tap` delays event delivery until the hold/tap decision is made, which interferes with combo detection. To make home row mods and similar patterns usable on combo-heavy layouts, hold-tap support is integrated into the chord input behavior itself. See [docs/reference.md](docs/reference.md) for usage details.

For design details and implementation background, see [docs/design.md](docs/design.md).

## Installation

Add this module to your `west.yml`:

```yaml
manifest:
  remotes:
    - name: matoi
      url-base: https://github.com/matoi
  projects:
    - name: zmk-chord-input-system
      remote: matoi
      revision: main
```

You can also add it through `ZMK_EXTRA_MODULES` in your build command if that matches your setup better.

## Quick Start

The example below uses the [Naginata](https://oookaworks.seesaa.net/article/456099128.html) preset because it includes built-in combo behavior as part of the preset.
It is written as a plain ZMK keymap overlay, without any custom shared keymap helpers.

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

What this gives you:

- Predefined Naginata character behaviors
- Built-in yoon, dakuten, handakuten, and kogaki combos from the preset
- Thumb behaviors for center shift
- `&cdis_on` and `&cdis_off` in the example to turn the layer on and off
- Explicit `&cdis_on` / `&cdis_off` positions on the chord-input layer itself, so you can manually resynchronize layer state and IME state if they drift apart

The `NC_NAGINATA_*` names in the example are just layout macros defined by `cdis-naginata.dtsi`. They expand to ordinary bindings like these:

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

So if you prefer, you can replace those macros with the expanded bindings directly in your keymap.

In this example, the outer thumb positions on the chord-input layer are mapped to `&cdis_on LAYER_NAGINATA` and `&cdis_off LAYER_NAGINATA` deliberately, not just as spare bindings. They are there so you can resynchronize layer state and IME state explicitly from the Naginata layer itself if they drift apart.

If you want a [NICOLA](http://nicola.sunicom.co.jp/spec/kikaku.htm)-based setup instead, see [docs/getting-started.md](docs/getting-started.md).

## Documentation

The `docs/` directory in this repository contains the public-facing documentation set for this module.

- [docs/getting-started.md](docs/getting-started.md): plain ZMK setup examples for Naginata and NICOLA
- [docs/reference.md](docs/reference.md): behaviors, macros, customization, and configuration options
- [docs/design.md](docs/design.md): public design overview and architecture notes

## External References

- [NICOLA standard specification](http://nicola.sunicom.co.jp/spec/kikaku.htm)
- [Naginata layout](https://oookaworks.seesaa.net/article/456099128.html)
