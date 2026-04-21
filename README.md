# zmk-chord-input-system

`zmk-chord-input-system` は、日本語の同時打鍵かな入力を ZMK ファームウェア上で扱うためのモジュール。
[NICOLA](http://nicola.sunicom.co.jp/spec/kikaku.htm) や [薙刀式](https://oookaworks.seesaa.net/article/456099128.html) のような配列向けに、共通の入力エンジンとそのまま使えるプリセットを提供する。

English README: [README.en.md](README.en.md)

## 概要

このモジュールは、OS 側のキーリマッパーではなく、ファームウェア側でかな入力規則を実装したい ZMK ユーザー向け。

Highlights:

- プレーンな ZMK keymap / overlay で使える
- NICOLA、薙刀式、その他の同時打鍵かな配列をサポートする
- [NICOLA 標準規格](http://nicola.sunicom.co.jp/spec/kikaku.htm) のキー入力規則を、ZMK ファームウェアの範囲でできるだけそのまま実装することを目指している。これは汎用キーリマッパーのコンボ機能の上にかな入力を載せるのとの大きな違い
- 通常のショートカット用に passthrough を備え、修飾付き入力時はかな入力ではなくベースレイヤー側へフォールスルーできる
- chord input key 用の built-in hold-tap を備え、home row mods のような combo-aware な用途にも対応できる
- 拗音、濁音、半濁音、小書きなどが必要な配列向けに built-in char combo を備える
- 1 キーあたり最大 4 種類のシフトと最大 5 面を扱える
- split keyboard と unibody keyboard の両方で動く

## 制限と留意事項

- 現在の出力バックエンドは、ホスト OS の IME がローマ字入力モードであることを前提にしている
- chord input layer は、ホスト側の IME の on/off 状態と同期している前提で使う
- chord input layer は default layer の直上、つまり最下位の non-default layer に置く必要がある
- このモジュールが担当するのはファームウェア上のかな入力までで、かな漢字変換は引き続きホスト OS の IME に依存する
- プリセットによっては、カーソル移動や backspace のような非かな出力を配列内に含む

IME 同期は実運用上かなり重要。`&cdis_on` と `&cdis_off` は chord input layer とホスト IME を一緒に切り替えるためのものだが、OS やアプリケーションが独立に IME 状態を変更した場合、その変更をファームウェア側は観測できない。たとえばエディタが minibuffer や command field に入るときに IME を自動で OFF にすると、キーボード側は chord input layer のままなのに、ホスト側だけが英数入力になっていることがある。その状態では layer と IME が一致せず、意図しない出力につながる。これはファームウェアだけでは完全には解決できないが、実運用上の摩擦を減らす設定はある。特に `auto-off-keys` は、特定のショートカット後に自動で chord input mode を抜けることで、multi-step shortcut と IME/layer drift の両方を多少緩和できる。設定項目は [docs/reference.md](docs/reference.md) を参照。

built-in hold-tap も、同じく実運用上の理由で入れてある。combo candidate key を通常の ZMK `hold-tap` で包むと、hold/tap 判定が終わるまでイベント配送が遅延し、combo 判定と衝突する。combo を多く使う配列でも home row mods などを成立させるため、このモジュールでは hold-tap を chord input behavior 自体に統合している。使い方は [docs/reference.md](docs/reference.md) を参照。

設計や実装背景は [docs/design.md](docs/design.md) を参照。

## インストール

`west.yml` にこのモジュールを追加する。

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

ビルド方法によっては、`ZMK_EXTRA_MODULES` で追加してもよい。

## Quick Start

最初の例には [薙刀式](https://oookaworks.seesaa.net/article/456099128.html) プリセットを使う。
薙刀式はプリセットの中に combo 動作も含まれているため、このモジュールの特徴を短く確認しやすい。

例は、共通キーマップや独自 helper を前提にしない、プレーンな ZMK keymap overlay。

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

この例で得られるもの:

- 薙刀式の文字 key behavior
- プリセットに含まれる拗音、濁音、半濁音、小書きの combo
- center shift 用の thumb behavior
- layer の on/off 用の `&cdis_on` と `&cdis_off`
- chord-input layer 内にも置かれた `&cdis_on` / `&cdis_off`
  layer と IME がズレたときに、その場で手動復帰できる

例中の `CDIS_NAGINATA_*` は `cdis-naginata.dtsi` に定義されている layout macro で、展開すると普通の binding になる。

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

必要なら、これらの macro は使わず、展開後の binding をそのまま keymap に書いてもよい。

この例では、chord-input layer の外側の親指位置に `&cdis_on LAYER_NAGINATA` と `&cdis_off LAYER_NAGINATA` を意図的に置いている。これは単なる余りキーではなく、Naginata layer の中から layer と IME の同期を明示的に取り直すため。実運用では、こうした手動復帰用の binding と `auto-off-keys` を併用すると、特定ショートカット後は自動で chord input mode を抜けつつ、必要なときには手動復帰もできる。

NICOLA ベースの設定を使いたい場合は [docs/getting-started.md](docs/getting-started.md) を参照。
English getting-started guide: [docs/getting-started.en.md](docs/getting-started.en.md)

## ドキュメント

この repository の `docs/` には、公開対象のドキュメントを置いている。

- [docs/getting-started.md](docs/getting-started.md): 薙刀式と NICOLA の最小セットアップ例
- [docs/getting-started.en.md](docs/getting-started.en.md): getting started の英語版
- [docs/reference.md](docs/reference.md): behavior、macro、カスタマイズ、設定項目
- [docs/reference.en.md](docs/reference.en.md): reference の英語版
- [docs/design.md](docs/design.md): 公開向けの設計概要とアーキテクチャ

## 外部リンク

- [NICOLA 標準規格](http://nicola.sunicom.co.jp/spec/kikaku.htm)
- [薙刀式](https://oookaworks.seesaa.net/article/456099128.html)
