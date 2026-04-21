# Getting Started

`zmk-chord-input-system` を、プレーンな ZMK keymap overlay から使い始めるための最小構成をまとめたガイド。
ここに出てくる例は、このモジュール以外の共通キーマップ資産や独自 helper macro を前提にしません。

English version: [getting-started.en.md](getting-started.en.md)

## 事前準備

- OS 側の日本語 IME をローマ字入力モードにする
- chord input layer を default layer の直上に置く
- 先にこの module を ZMK build に追加する

レイヤー順序は重要。ZMK は上位 layer から順に binding を解決するため、chord input layer が通常の momentary layer や function layer より上にあると、それらの layer が処理したいキー入力を chord input 側が先に消費してしまう。

例:

```c
#define LAYER_BASE 0
#define LAYER_CDIS 1
#define LAYER_NUM 2
```

## 薙刀式プリセット

[薙刀式](https://oookaworks.seesaa.net/article/456099128.html) プリセットは、文字 combo まで含んだ形で定義されている。
combo を含む経路を最短で試したい場合には、これがいちばん手早い入口となる。

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

このプリセットに含まれるもの:

- 薙刀式の文字キー定義
- center-shift 用の thumb behavior
- 拗音 combo
- 濁音、半濁音 combo
- 小書き combo
- chord-input layer 上の `&cdis_on` / `&cdis_off`
  必要に応じて layer と IME の同期を手動で取り直せる

注意点:

- このプリセットでは、一部の位置がカーソル移動や backspace などの非かな出力になっている
- `CDIS_NAGINATA_LH` と `CDIS_NAGINATA_RH` は、タップ時に `SPACE` を送る thumb binding
- 例では `&cdis_on` と `&cdis_off` を base layer だけでなく chord-input layer の親指外側にも置いている。これは Naginata layer の中から host IME と layer の同期を取り直せるようにするため
- 実運用では、こうした手動復帰用 binding と `auto-off-keys` を併用すると、特定ショートカットの後は自動で chord-input mode を抜けつつ、必要なときには手動で復帰できる

例中の `CDIS_NAGINATA_*` は `cdis-naginata.dtsi` に定義されている layout macro で、実体は普通の binding。

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

必要なら、これらの macro を使わず、展開後の binding をそのまま keymap に書いてもよい。

この例の thumb binding は、通常の chord-input thumb behavior。かなシフトとして hold することも、単独で tap して `SPACE` を送ることもできる。

## NICOLA プリセット

標準的な [NICOLA](http://nicola.sunicom.co.jp/spec/kikaku.htm) 配列を使いたい場合は `cdis-nicola.dtsi` を使う。

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

このプリセットに含まれるもの:

- NICOLA の文字キー定義
- 左右分割済みの NICOLA layout macro
- 左右の thumb-shift behavior
- `&cdis_on` / `&cdis_off` toggle behavior
- chord-input layer 上の `&cdis_on` / `&cdis_off`
  必要に応じて layer と IME の同期を手動で取り直せる

`CDIS_NICOLA_*` は `cdis-nicola.dtsi` に定義されている layout macro。

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

この定義は、特定のキーボードに物理キー数が足りない場合でも、まず標準 NICOLA 配列をそのまま表すことを優先している。

例では `&cdis_on` と `&cdis_off` を base layer にも chord-input layer の親指周辺にも置いている。これは NICOLA layer の中から host IME と layer の同期を取り直すため。こちらも、手動復帰用 binding と `auto-off-keys` を併用すると実運用しやすくなる。

NICOLA プリセットは標準配列に基づいているため、コンパクトなキーボードでは物理的に置けない位置が含まれることがある。その場合は、標準配列を出発点として実機向けに再配置する形を想定している。

## Hold-Tap を追加する

home row mods のような modifier-on-hold を使いたい場合は、通常の ZMK `hold-tap` で chord input key を包むのではなく、このモジュールの built-in hold-tap を使う。

たとえば次の設定で、`&cdis_si` を tap 時はかな、hold 時は `LSHFT` にできる。

```dts
#include <behaviors/cdis-nicola.dtsi>

&cdis_si {
    hold-behavior = <&kp>;
    hold-param = <LSHFT>;
    tapping-term-ms = <220>;
    quick-tap-ms = <150>;
};
```

`&cdis_si` をすべて変えたくない場合は、別 behavior を定義する。

```dts
#include <zmk-chordis/chord-input-map.h>

/ {
    behaviors {
        CHORD_INPUT_BEHAVIOR_HT(si_lshft, KANA_SI, KANA_A, KANA_ZI, &kp, LSHFT)
    };
};
```

そのうえで、hold を使いたい位置だけ `&cdis_si_lshft` を配置する。

hold 側が有効になると、modifier passthrough も自動で有効になる。つまり hold として解決したあとに続くキーは、かなとして出力されるのではなく、base layer 側の通常キーとして処理される。これによって built-in hold-tap と modifier shortcut を併用できる。

## Modifier Passthrough

modifier passthrough は chord-input の通常機能。Ctrl、Shift、Alt、GUI、または hold-tap 由来の modifier が有効な間、chord-input key はかな入力にならず、base layer 側へフォールスルーする。

`&caps_word` のように、暗黙に modifier を効かせる behavior を使う場合は、`passthrough-behaviors` に列挙する。

```dts
&cdis_config {
    passthrough-behaviors = <&caps_word>;
};
```

## 英数へ戻すための Auto-Off

`auto-off-keys` は passthrough そのものではない。指定したショートカットが押されたときに chord-input mode を抜け、host IME を非かな状態へ戻すための補助機能。

例:

```dts
&cdis_config {
    auto-off-keys = <(MOD_LCTL|MOD_RCTL) X
                     (MOD_LCTL|MOD_RCTL) S
                     (MOD_LALT|MOD_RALT) X>;
};
```

これは、特定ショートカットのあとに chord-input mode に留まりたくない場合に便利。

実運用では、主に次の 2 つの場面で役立つ。

- `C-x ...` や `M-x ...` のような multi-step shortcut
  prefix は chord-input mode のまま送り、その後のキーは通常の英数経路で打ちたい
- IME sync drift の緩和
  アプリケーションが独立に IME を切り替えること自体は防げないが、layer と IME がズレる場面を減らせる

## Passthrough Word

プリセットが `&cdis_pt_word` を提供している場合、そのキーを押すことで、chord-input layer を完全には抜けずに一時的に ASCII passthrough へ切り替えられる。短い英単語や識別子を入れたいときに便利。

ただし、この挙動は host IME が設定された IME 切替キーコードに期待通り反応することを前提にしている。現在のプリセットでは、通常 `LANG2` / `japanese_eisuu` で有効化し、`LANG1` / `japanese_kana` で復帰する。OS、IME、keyboard 設定によっては、期待通りに切り替わらない場合がある。

```dts
some_layer {
    bindings = <
        &cdis_pt_word
    >;
};
```

## 最初の確認

flash 後は、まず次を確認する。

1. base layer から input layer を ON にする
2. OS 側の IME が有効で、ローマ字入力モードになっていることを確認する
3. 単独の文字キーを押してみる
4. thumb-shift や center-shift を伴う入力を試す
5. NICOLA では、親指キーを hold しながら文字キーを押す簡単な入力を試す
6. 薙刀式では、拗音や濁音になる combo を試す

## 次に読むもの

- 詳細なカスタマイズや設定項目は [reference.md](reference.md)
- 英語版 reference は [reference.en.md](reference.en.md)
- 設計や制約の背景は [design.md](design.md)
