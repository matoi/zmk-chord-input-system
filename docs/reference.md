# Reference

この文書は、`README.md` から意図的に外した詳細な使い方とカスタマイズ項目をまとめたもの。
例はプレーンな DTS と、プレーンな ZMK 風 keymap snippet で書いている。

English version: [reference.en.md](reference.en.md)

## プリセット Include

利用できるプリセット include:

- `#include <behaviors/cdis-naginata.dtsi>`
- `#include <behaviors/cdis-nicola.dtsi>`

`cdis-naginata.dtsi` には、拗音、濁音、半濁音、小書き用の built-in combo table が含まれる。
`cdis-nicola.dtsi` は、標準 NICOLA の文字 behavior と thumb behavior を提供する。

## 基本 Behavior

よく使う behavior 名:

- `&cdis_on`: chord input layer を ON にし、設定された IME-on key を tap する
- `&cdis_off`: chord input layer を OFF にし、設定された IME-off key を tap する
- `&cdis_thumb_l`, `&cdis_thumb_r`: 親指シフト用 behavior
- `&cdis_*`: プリセット、または `CHORD_INPUT_BEHAVIOR*` macro で定義した文字 behavior
- `&cdis_pt_word`: プリセットが提供する場合の一時的な ASCII passthrough mode

## レイヤー配置

chord input layer は、最下位の non-default layer に置く必要がある。
これは ZMK が上位 layer から順に binding を解決するため。

```c
#define LAYER_BASE 0
#define LAYER_CDIS 1
#define LAYER_NUM 2
#define LAYER_FN 3
```

## Keymap 上での再配置

キー位置を変えたい場合は、behavior 定義そのものではなく、`bindings` 内での配置を組み替える。

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

## 既存 Behavior の Kana を上書きする

プリセットを include したあとでも、既存 behavior の `kana` property は上書きできる。

```dts
#include <behaviors/cdis-nicola.dtsi>

&cdis_ka {
    kana = <KANA_KA KANA_E KANA_GI>;
};

&cdis_kuten {
    kana = <KANA_WA KANA_SMALL_A KANA_SMALL_A>;
};
```

`kana` property は 3 個から 5 個の値を取る配列。

- `<unshifted shift0 shift1>`
- `<unshifted shift0 shift1 shift2>`
- `<unshifted shift0 shift1 shift2 shift3>`

かな定数は `include/zmk-chordis/keys.h` に定義されている。

## 独自の文字 Behavior を定義する

`#include <zmk-chordis/chord-input-map.h>` にある helper macro を使う。

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

これらは keymap 上では `&cdis_my_wa`、`&cdis_my_ka`、`&cdis_my_ta` になる。

## かな面に非かなキーコードを混在させる

生の ZMK keycode をかな面に入れたい場合は `CDIS_KEY()` を使う。

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

薙刀式のように、文字位置に navigation や backspace を置くプリセットは、この仕組みを使っている。

## 親指キーのタップ動作

親指 behavior のデフォルトは `&kp` で、tap 時の keycode は keymap 側から受け取る。

```dts
&cdis_thumb_l SPACE
&cdis_thumb_r SPACE
```

node 側の `bindings` を上書きすれば、tap 時の基底 behavior も変更できる。

```dts
&cdis_thumb_r {
    bindings = <&mkp>;
};
```

この場合、keymap 側からは mouse button など、その behavior に対応した parameter を渡せる。

## 文字キー向け Built-In Hold-Tap

combo candidate key には、通常の ZMK `hold-tap` ではなく、このモジュールの built-in hold-tap を使う。

最も簡単なのは、既存の preset behavior を拡張する方法。

```dts
#include <behaviors/cdis-nicola.dtsi>

&cdis_si {
    hold-behavior = <&kp>;
    hold-param = <LSHFT>;
    tapping-term-ms = <220>;
    quick-tap-ms = <150>;
};
```

同じかなに対して plain 版と hold-tap 版を分けたい場合は、新しい behavior を定義する。

```dts
#include <zmk-chordis/chord-input-map.h>

/ {
    behaviors {
        CHORD_INPUT_BEHAVIOR_HT(si_lshft, KANA_SI, KANA_A, KANA_ZI, &kp, LSHFT)
    };
};
```

利用できる hold-tap helper variant:

- `CHORD_INPUT_BEHAVIOR_HT`
- `CHORD_INPUT_BEHAVIOR_HT_T`
- `CHORD_INPUT_BEHAVIOR4_HT`
- `CHORD_INPUT_BEHAVIOR4_HT_T`
- `CHORD_INPUT_BEHAVIOR5_HT`
- `CHORD_INPUT_BEHAVIOR5_HT_T`

## Passthrough Word

プリセットが `&cdis_pt_word` を提供している場合、どの layer にでも置いて、一時的に ASCII を base layer 経由で送れる。

```dts
some_layer {
    bindings = <
        &cdis_pt_word
    >;
};
```

典型的な設定:

```dts
&cdis_pt_word {
    continue-list = <UNDERSCORE BACKSPACE DELETE MINUS SPACE>;
    ime-keycode = <LANG2>;
    ime-restore-keycode = <LANG1>;
};
```

## Auto-Off

特定の modifier+key 組み合わせで自動的に chord input layer を抜けたい場合は、`&cdis_config` に `auto-off-keys` を設定する。

```dts
&cdis_config {
    auto-off-keys = <(MOD_LCTL|MOD_RCTL) X
                     (MOD_LCTL|MOD_RCTL) S
                     (MOD_LALT|MOD_RALT) X>;
};
```

## 暗黙 Modifier Behavior 用の Passthrough

一部の behavior は内部で modifier を適用するため、通常の modifier passthrough では検出できません。
必要に応じて、それらを `passthrough-behaviors` に列挙する。

```dts
&cdis_config {
    passthrough-behaviors = <&caps_word>;
};
```

## Timing 設定

タイミング関係の設定は `&cdis_config` で行う。

```dts
&cdis_config {
    timeout-ms = <150>;
    sequential-threshold = <3>;
    sequential-min-overlap-ms = <20>;
};
```

## Hold-Tap グローバル設定

Hold-Tap のタイミング系プロパティは `&cdis_config` にグローバル既定値を持つ。各 behavior 側で値を inherit sentinel `0` のままにすると、グローバル値が適用される。home row mods のように複数キーで同じ値を使うときに、毎回書く必要がなくなる。

```dts
&cdis_config {
    default-tapping-term-ms        = <180>;
    default-quick-tap-ms           = <120>;
    default-require-prior-idle-ms  = <100>;
    hold-after-partner-release-ms  = <60>;
};
```

各 knob の解決順:

1. behavior 側の per-key 値（非 0 のとき）
2. `&cdis_config` のグローバル既定値
3. ビルトインの fallback（`default-tapping-term-ms = 200`, `default-quick-tap-ms = 0`, `default-require-prior-idle-ms = 0`, `hold-after-partner-release-ms = 80`）

`hold-after-partner-release-ms` は、plain combo partner が離されてから HT 未確定キーが combo へ転ぶ猶予時間（ms）。短いほど HOLD 確定が早く（HT 寄り）、長いほど combo を拾いやすい（tap 寄り）。

`hold-after-partner-release-ms` は現状 per-key の上書き不可。per-key で上書きできるのは `tapping-term-ms`、`quick-tap-ms`、`require-prior-idle-ms`。

グローバルで `quick-tap-ms` を有効にしたまま、特定キーだけ無効化したいときは、そのキーに `<1>` を指定する（非 0 値は inherit を抑止し、1 ms は repeat protection としては実質無効）。

`require-prior-idle-ms` は、直前のキー release から指定 ms 未満で押された HT キーを tap に固定する rolling input 対策。直前 release は同じ物理キーでも対象になる。グローバルで有効にしたまま特定キーだけ無効化したい場合は、そのキーに `<1>` を指定する。

## Combo Table

薙刀式は、preset include によって combo table を自動で含む。
独自配列を定義する場合、combo node には `zmk,chord-input-char-combo` compatible を使う。

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
- [getting-started.en.md](getting-started.en.md)
- [design.md](design.md)
