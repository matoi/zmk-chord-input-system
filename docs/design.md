# zmk-chord-input-system Design Document

## 1. Overview

zmk-chord-input-system is a ZMK firmware addon module that implements thumb-shift Japanese input as custom ZMK behaviors. It supports NICOLA, shin-geta, and custom layouts with up to 4 shift types and 5 faces per key. Kana is output as romaji sequences via HID, working with any OS romaji IME.

### Goals

- Provide a complete thumb-shift input experience within ZMK firmware
- Support all three NICOLA keystroke patterns: thumb-first, character-first (後追い), and simultaneous
- Support up to 4 shift types (thumb L/R, grid-based middle/ring finger shifts) for diverse Japanese layouts
- Output kana via romaji sequences for maximum OS compatibility
- Be keyboard-agnostic: no hardcoded key positions; works with any split or unibody layout
- Minimize user-side configuration: include one dtsi, place behaviors in keymap

### Non-Goals

- Implementing a full Japanese IME on the keyboard (kana-to-kanji conversion remains on the OS)
- Replacing the OS IME (the OS must have a romaji-capable Japanese IME active)

## 2. Background: How NICOLA Works

NICOLA assigns three kana to each character key:

| Input | Output |
|---|---|
| Character key alone | Unshifted kana |
| Left thumb + character key | Left-shifted kana |
| Right thumb + character key | Right-shifted kana |

The "simultaneous" detection is permissive: thumb-first hold, character-first (後追い), and true simultaneous presses are all accepted within a configurable time window.

### NICOLA Layout

規格書の図1・図2・第4章に基づく。キー位置は段名(B/C/D)+番号で示す。
左領域: B01-B05, C01-C05, D01-D05、右領域: B06-B10, C06-C10, D06-D11。

注: 規格書の正誤表(2018.11)に基づき修正済み:
- B01 単独: ．（ピリオド）※規格図では「，」と誤記
- B10 単独: ・（中点）※規格図では「ぃ」と誤記
- D12（最右）: 単独 ゛（濁点）、シフト ゜（半濁点）

#### Unshifted (単独打鍵)

```
      D01  D02  D03  D04  D05 | D06  D07  D08  D09  D10  D11
      。   か   た   こ   さ  | ら   ち   く   つ   ，   、

      C01  C02  C03  C04  C05 | C06  C07  C08  C09  C10
      う   し   て   け   せ  | は   と   き   い   ん

      B01  B02  B03  B04  B05 | B06  B07  B08  B09  B10
      ．   ひ   す   ふ   へ  | め   そ   ね   ほ   ・
```

備考:
- D01 単独: 。（句点）※シフトも「。」としてよい（規格書備考）
- D10 単独: ，（コンマ）
- D11 単独: 、（読点）
- C01 に「ゔ」を配置する場合は左上（右親指シフト）に配置

#### Left thumb shift (左親指シフト)

左領域は**同側シフト**、右領域は**クロスシフト**。

```
      D01  D02  D03  D04  D05 | D06  D07  D08  D09  D10
      ぁ   え   り   ゃ   れ  | ぱ   ぢ   ぐ   づ   ぴ

      C01  C02  C03  C04  C05 | C06  C07  C08  C09  C10
      を   あ   な   ゅ   も  | ば   ど   ぎ   ぽ   (なし)

      B01  B02  B03  B04  B05 | B06  B07  B08  B09  B10
      ぅ   ー   ろ   や   ぃ  | ぷ   ぞ   ぺ   ぼ   (なし)
```

備考: 右領域+左親指 = 濁音・半濁音が中心（クロスシフト）

#### Right thumb shift (右親指シフト)

左領域は**クロスシフト**、右領域は**同側シフト**。

```
      D01  D02  D03  D04  D05 | D06  D07  D08  D09  D10
      ぁ   が   だ   ご   ざ  | よ   に   る   ま   ぇ

      C01  C02  C03  C04  C05 | C06  C07  C08  C09  C10
      ゔ   じ   で   げ   ぜ  | み   お   の   ょ   っ

      B01  B02  B03  B04  B05 | B06  B07  B08  B09  B10
      ぅ   び   ず   ぶ   べ  | ぬ   ゆ   む   わ   ぉ
```

備考:
- 左領域+右親指 = 濁音が中心（クロスシフト）
- D04 シフト: ゃ（小文字）、C04 シフト: ゅ（小文字）等、字形類似文字の区別は規格書備考参照
- D12（数字段右端）: 単独 ゛、シフト ゜ は実装に含める

## 3. Output Strategy: Romaji Sequences

### Why romaji output

| Method | Keystrokes per kana | Lag | Cross-platform |
|---|---|---|---|
| Romaji sequences | 1-3 (e.g. `shi` = 3) | Imperceptible at USB rate | Any OS with romaji IME (default) |
| JIS kana keycodes | 1 (clean) / 2 (dakuten) | Minimal | Requires JIS kana input mode |
| Unicode direct | N/A | N/A | Not supported by HID |

We choose romaji output. Each kana ID maps to a romaji string (e.g. `KANA_KA` → `"ka"`, `KANA_SHI` → `"si"`). The engine outputs rapid HID press/release pairs for each ASCII character. At USB polling rates (~1ms per event), even a 3-character romaji sequence completes in ~6ms — imperceptible to the user.

### Advantages over JIS kana keycodes

- **No special OS setup**: romaji input is the default mode for Japanese IMEs on all platforms
- **Yōon and compound kana**: multi-character sequences like `kya`, `sha` are output as a single unit — no need for JIS kana's multi-step base+dakuten approach
- **Layout independence**: output is standard ASCII keycodes, no dependency on JIS physical keyboard layout

### Romaji Table

Each kana ID (defined in `keys.h`) maps to a romaji string in `chordis_engine.c`:

```c
static const char * const romaji_table[KANA_COUNT] = {
    [KANA_A]        = "a",
    [KANA_KA]       = "ka",
    [KANA_GA]       = "ga",
    [KANA_SMALL_TU] = "xtu",
    [KANA_KYA]      = "kya",
    // ... (full table in source)
};
```

### Cross-Platform Compatibility

| OS | Romaji input | Setup required |
|---|---|---|
| macOS | Full support | System Settings → Keyboard → Input Sources → Japanese (default is romaji) |
| Windows | Full support | MS-IME → Romaji input mode (default) |
| iOS | Supported | Settings → General → Keyboard → Hardware Keyboard → Japanese |
| Android | Supported | Gboard / other IME with romaji input |

## 4. Architecture

### Custom Behavior: `zmk,behavior-chord-input`

The core of the module is a single custom ZMK behavior implemented in C that handles the NICOLA timing algorithm.

#### Binding Cell の制約

ZMK の `zmk_behavior_binding` 構造体は `param1` と `param2`（各 `uint32_t`）のみを持ち、YAML ベースインクルードも `zero_param.yaml` / `one_param.yaml` / `two_param.yaml` までしか存在しない。**`#binding-cells = <3>` は使用不可。**

#### 設計: zero-param + per-instance 構成（mod-morph パターン）

各キーごとに個別の behavior インスタンスを定義し、3 つのかなコードを DTS プロパティとして保持する:

```dts
/ {
    behaviors {
        cdis_u: cdis_u {
            compatible = "zmk,behavior-chord-input";
            #binding-cells = <0>;
            kana = <KANA_U KANA_WO KANA_VU>;   // unshifted / left / right
        };
        cdis_ka: cdis_ka {
            compatible = "zmk,behavior-chord-input";
            #binding-cells = <0>;
            kana = <KANA_KA KANA_GA KANA_NONE>; // か / が / (none)
        };
        // ... 30 instances total
    };
};
```

キーマップでの使用:

```dts
&cdis_u  &cdis_si   &cdis_te  &cdis_ke  &cdis_se  ...
```

**30 インスタンスの定義が必要**だが、モジュール提供の `nicola.dtsi` にすべてプリセット済みとすることで、ユーザー側の負担はない。

ヘルパーマクロで定義を簡潔に:

```c
#define CHORD_INPUT_BEHAVIOR(name, unshifted, left_shifted, right_shifted) \
    cdis_##name: cdis_##name { \
        compatible = "zmk,behavior-chord-input"; \
        #binding-cells = <0>; \
        kana = <unshifted left_shifted right_shifted>; \
    };
```

#### `zmk,behavior-chord-input-thumb`（親指キー behavior）

親指キー専用の behavior。hold-tap と同じパターンで、`bindings` プロパティにタップ時の behavior を指定し、`#binding-cells = <1>` でそのパラメータをキーマップから渡す:

```dts
/ {
    behaviors {
        cdis_thumb_l: cdis_thumb_l {
            compatible = "zmk,behavior-chord-input-thumb";
            #binding-cells = <1>;
            side = "left";
            bindings = <&kp>;    // タップ時に使う behavior（デフォルト: &kp）
        };
        cdis_thumb_r: cdis_thumb_r {
            compatible = "zmk,behavior-chord-input-thumb";
            #binding-cells = <1>;
            side = "right";
            bindings = <&kp>;
        };
    };
};
```

キーマップでの使用:

```dts
&cdis_thumb_l SPACE   &cdis_thumb_r SPACE
```

ユーザーが behavior をオーバーライドしてマウスボタン等に変更可能:

```dts
&cdis_thumb_r { bindings = <&mkp>; };
// keymap: &cdis_thumb_r MB2
```

`cdis_thumb_*` の `binding_pressed` / `binding_released` は、文字キー behavior と**共有のグローバルステートマシン**を操作して同時打鍵判定に参加する。

備考: 規格書（3章末尾）は親指キー共用型の組み合わせを「無変換/変換」「空白/変換」の2パターンに限定しているが、本実装では `bindings` プロパティにより任意の behavior を設定可能（`&kp SPACE`, `&mkp MB2` 等）とする。これは規格の意図的な拡張であり、現代のカスタムキーボードの柔軟性を優先する。

#### Devicetree Configuration（グローバル設定ノード）

ステートマシンのタイミングパラメータのみを保持:

```dts
/ {
    cdis_config: cdis_config {
        compatible = "zmk,chord-input-config";
        timeout-ms = <100>;                // 同時打鍵判定時間
        sequential-threshold = <2>;        // 逐次打鍵判定の係数 n
        sequential-min-overlap-ms = <20>;  // ぶれ対策の最小重なり時間
    };
};
```

#### NICOLA レイヤーの運用モデル

NICOLA は**かな入力のベースレイヤー**であり、`&mo` でホールド中だけ有効にするものではない。レイヤー切り替えと OS 側の IME 状態切り替えを**アトミックに**行う専用 behavior `&cdis_on` / `&cdis_off` を使用する。

```
英字モード（デフォルト）              日本語モード（NICOLA）
┌─────────────────────────┐          ┌──────────────────────────┐
│ &kp Q  &kp W  ...       │          │ &cdis_u  &cdis_si   ...      │
│                          │ &cdis_on   │                           │
│ &kp SPC  &kp SPC        │────────→│ &cdis_thumb_l SPC           │
│                          │          │           &cdis_thumb_r SPC  │
│                          │←────────│                           │
│                          │ &cdis_off  │ &cdis_off をキーマップに配置│
└─────────────────────────┘          └──────────────────────────┘
      通常の英字入力                      NICOLA かな入力
      &kp → ZMK 標準動作                 キー単独 → 無シフトかな
      遅延ゼロ                           キー+親指 → シフトかな
                                         親指単独 → スペース等
```

#### `zmk,behavior-chord-input-toggle`（モード切り替え behavior）

レイヤー切り替えと OS IME 切り替えを同時に行うべき等（idempotent）な behavior。`#binding-cells = <1>` でレイヤー番号をキーマップから指定する:

```dts
/ {
    behaviors {
        cdis_on: cdis_on {
            compatible = "zmk,behavior-chord-input-toggle";
            #binding-cells = <1>;
            toggle-action = "on";
            ime-keycode = <LANG1>;   // OS にかなモードを指示
        };
        cdis_off: cdis_off {
            compatible = "zmk,behavior-chord-input-toggle";
            #binding-cells = <1>;
            toggle-action = "off";
            ime-keycode = <LANG2>;   // OS に英字モードを指示
        };
    };
};
```

キーマップでの使用（レイヤー番号を引数で指定）:

```dts
#define LY_NICOLA 1
default_layer { bindings = < ... &cdis_on LY_NICOLA ... >; };
nicola_layer  { bindings = < ... &cdis_off LY_NICOLA ... >; };
```

**レイヤー順序の要件**: NICOLA レイヤーは**デフォルトレイヤーの直上**（最も低い非デフォルトレイヤー番号）に配置すること。ZMK の keymap はレイヤー番号の大きい順に走査するため、NICOLA より上に他のレイヤー（Number/Symbol、Scroll、Function 等）があれば、それらを `&mo` で有効にした際に NICOLA を正しくオーバーライドできる。NICOLA が最上位にあると、他のレイヤーがアクティブでも NICOLA の binding が先にヒットして OPAQUE を返し、目的のレイヤーに到達しない。

```
// 推奨レイヤー順序
#define LY_DFLT 0  // Default (QWERTY)
#define LY_CDIS 1  // Chord Input ← デフォルト直上
#define LY_NMSM 2  // Number/Symbol
#define LY_SHFT 3  // Shift
#define LY_FNCT 4  // Function
// ... 他のレイヤーはすべて NICOLA より上
```

**べき等性**: `&cdis_on` を何度押しても「NICOLA レイヤー ON + IME かなモード」、`&cdis_off` を何度押しても「NICOLA レイヤー OFF + IME 英字モード」。状態のずれが起きない。

**IME キーコード**: `LANG1` (0x90) / `LANG2` (0x91) は macOS・Windows・iOS で共通してサポートされる HID usage であり、OS ごとの設定変更は不要:

| HID キーコード | macOS | Windows (MS-IME) | iOS |
|---|---|---|---|
| `LANG1` (0x90) | かなモード ON | IME ON | 日本語 ON |
| `LANG2` (0x91) | 英字モード ON | IME OFF | 英字 ON |

**レイヤー API**: `zmk_keymap_layer_activate()` / `zmk_keymap_layer_deactivate()` を使用する。`zmk_keymap_layer_to()` は全レイヤーを無効化してから指定レイヤーだけ有効化するため、シンボルレイヤー等が存在する場合に問題となる。`activate` / `deactivate` は個別レイヤーのみを操作し、他のレイヤー状態に影響しない。

**内部動作**:
- `&cdis_on LY_NICOLA` の `binding_pressed`:
  1. `zmk_keymap_layer_activate(param1)` — NICOLA レイヤーを有効化（他レイヤーに影響なし）
  2. `raise_zmk_keycode_state_changed_from_encoded(LANG1, true, ts)` — OS に IME 切り替えを送信
  3. `raise_zmk_keycode_state_changed_from_encoded(LANG1, false, ts)` — release
- `&cdis_off LY_NICOLA` の `binding_pressed`:
  1. ステートマシン強制 flush — CHAR_WAIT/THUMB_WAIT 中ならバッファを単独打鍵/タップとして出力し IDLE にリセット
  2. `zmk_keymap_layer_deactivate(param1)` — NICOLA レイヤーを無効化（他レイヤーに影響なし）
  3. `raise_zmk_keycode_state_changed_from_encoded(LANG2, true, ts)` — OS に英字モードを送信
  4. `raise_zmk_keycode_state_changed_from_encoded(LANG2, false, ts)` — release

**NICOLA アクティブ時**:
- `cdis_*` の `binding_pressed` → ステートマシンが単独打鍵 or 同時打鍵を判定してかなを出力
- `cdis_thumb_*` の `binding_pressed` → ステートマシンに親指押下を通知
- 親指キーはシフト専用。単独タップ時は設定された binding（スペース等）を出力

**NICOLA 非アクティブ時**:
- ZMK 標準動作。NICOLA behavior は一切介入しない。遅延ゼロ。

### Module File Structure

```
zmk-chordis/
├── zephyr/
│   └── module.yml              # Zephyr module descriptor (with dts_root)
├── CMakeLists.txt              # Build config (target_sources_ifdef, split guard)
├── Kconfig                     # Auto-enable via DT_HAS_*_ENABLED
├── dts/
│   ├── behaviors/
│   │   └── nicola.dtsi         # Behavior instances + NICOLA keymap template
│   └── bindings/
│       └── behaviors/
│           ├── zmk,behavior-chord-input.yaml        # 文字キー behavior binding
│           ├── zmk,behavior-chord-input-thumb.yaml   # 親指キー behavior binding
│           ├── zmk,behavior-chord-input-toggle.yaml  # モード切り替え behavior binding
│           ├── zmk,behavior-chord-input-passthrough-word.yaml # 一時的 ASCII パススルー
│           └── zmk,chord-input-config.yaml           # グローバル設定 binding
├── behaviors/
│   ├── chordis_engine.c          # 共有ステートマシン（エンジン）
│   ├── behavior_cdis.c        # 文字キー behavior（修飾キーパススルー含む）
│   ├── behavior_cdis_thumb.c  # 親指キー behavior
│   ├── behavior_cdis_toggle.c # モード切り替え behavior
│   └── behavior_cdis_passthrough_word.c # 一時的 ASCII パススルー
├── include/
│   └── zmk-chordis/
│       ├── chordis_engine.h    # ステートマシン API ヘッダ
│       ├── keys.h             # Kana ID definitions (romaji table indices)
│       ├── chord-input-map.h       # NICOLA → JIS kana mapping (CHORD_INPUT_BEHAVIOR macro)
│       └── keys.h             # Public defines (DAKU, HANDAKU flags)
└── docs/
    └── design.md              # This document
```

### Build System Details

#### zephyr/module.yml

`dts_root: .` が必須。これにより `dts/bindings/` 配下の YAML バインディングが Zephyr ビルドシステムに認識される。

```yaml
build:
  cmake: .
  kconfig: Kconfig
  settings:
    dts_root: .
```

#### CMakeLists.txt

ZMK 本体および zmk-tri-state と同じパターンで `target_sources_ifdef` を使用。`zephyr_library()` ではなく `app` ターゲットに直接ソースを追加する。スプリットキーボードではセントラル側のみでビルド。

```cmake
if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  target_sources_ifdef(
    CONFIG_ZMK_BEHAVIOR_CHORDIS app PRIVATE
    behaviors/behavior_cdis.c
    behaviors/behavior_cdis_thumb.c
    behaviors/behavior_cdis_toggle.c
  )
endif()
```

#### Kconfig

ZMK v0.3 本体と同じ `DT_HAS_*_ENABLED` パターンを使用。デバイスツリーに `zmk,behavior-chord-input` ノードが存在すれば Zephyr が自動で `DT_HAS_ZMK_BEHAVIOR_CHORD_INPUT_ENABLED` を定義し、ビルドが有効化される。

```kconfig
config ZMK_BEHAVIOR_CHORDIS
        bool
        default y
        depends on DT_HAS_ZMK_BEHAVIOR_CHORD_INPUT_ENABLED
```

注: zmk-tri-state は `dt_compat_enabled` パターンを使っているが、ZMK v0.3 本体は `DT_HAS_*_ENABLED` が標準。どちらも動作するが、本体と合わせる。

## 5. Behavior Algorithm: State Machine

NICOLA 規格書 第5章に基づく同時打鍵判定アルゴリズム。実装は `chordis_engine.c` に集約。

### 規格書の判定ルール（要約）

**同時打鍵判定時間**: 50���200ms（規格書では100msを推奨例とする）

**単独打鍵の成立条件**（規格書 5.3）:
1. 文字キーON → 判定時間経過（タイムアウト）→ 単独打鍵成立
2. 文字キーON → 判定時間内に文字キーOFF → 単独打鍵成立
3. 文字キーa ON → 判定時間内に別の文字キーb ON → 文字キーa の単独打鍵成立

**同時打鍵の成立条件**（規格書 5.4）:
- 親指キーON → 判定時間内に文字キーON（図4: Prefix 型）
- 文字キーON → 判定時間内に親指キーON（図5: Postfix 型）

**文字-親指-文字 の振り分け**（規格書 5.4.2）:
- 文字キーa ON → 親指キ��s ON → 文字キーb ON の場合:
  - t1（a→s の時間差）≦ t2（s→b の時間差）→ a+s が同時打鍵、b は単独
  - t1 > t2 → a は単独、s+b が同時打鍵

**逐次打鍵との識別**（規格書 5.5.2）:
- 文字キーON → 親指キーON → 文字キーOFF の場合:
  - t1（文字ON→親指ON）、t2（親指ON→文字OFF）
  - t1 > t2 × n → 逐次打鍵（同時打鍵ではない）
  - t1 ≦ t2 × n → 同時打鍵
  - n は実装が決定（定数係数）
  - t2 が極端に短い場合は t1 によらず逐次打鍵と判定する手法も考慮

### 設計上の考慮: 親指キーは修飾キーとして扱う

規格書の状態遷移図では THUMB_WAIT（親指バッファ中、文字キー待ち）が独立状態として存在するが、実装上 THUMB_WAIT と THUMB_HELD は文字キー到着時の動作が同一（シフトかな出力）であり、タイムアウト後は THUMB_HELD に遷移するだけで何も出力しない。

**親指キーは物理的に押し続けている限り常にシフトとして機能すべき**であり、タイムアウトで出力を確定する必要がない。従って THUMB_WAIT は不要であり、親指キー押下で直接 THUMB_HELD に遷移する。タイマーは **CHAR_WAIT でのみ** 使用する。

### States (4-state)

```
IDLE ──(char key)──→ CHAR_WAIT ──(timeout)──→ output unshifted kana → IDLE
  │                      │
  │                  (thumb arrives within timeout)
  │                      ↓
  │               CHAR_THUMB_WAIT ──(2nd char)──→ 5.4.2 partitioning
  │                      │
  │                  (char release)──→ 5.5.2 sequential detection
  │                      │
  │                  (timeout)──→ output shifted kana → THUMB_HELD
  │
  └──(thumb key)──→ THUMB_HELD ──(char key)──→ output shifted kana (stay)
                        │
                    (thumb release)
                        ↓
                  no char pressed → output thumb tap → IDLE
                  char was pressed → IDLE
                        │
                    (watchdog 1s)──→ IDLE (silent reset)
```

**CHAR_THUMB_WAIT** is an intermediate state where both a character key and a thumb key have been buffered, but the output is not yet decided. This enables spec 5.4.2 partitioning (char-thumb-char timing comparison) and spec 5.5.2 sequential detection (char release timing). If neither a second char nor a char release arrives within the timeout, the buffered char+thumb is output as shifted kana and the machine transitions to THUMB_HELD.

**THUMB_HELD watchdog** (1 second): a safety timer that resets to IDLE if no character key activity occurs while in THUMB_HELD. This mitigates BLE event drops on split keyboards where a thumb release event may be lost, leaving the engine stuck in THUMB_HELD. The watchdog is reset on each character key press. After a silent watchdog reset, if any thumb is still physically held (`thumb_any_held()`), the next character key press re-enters THUMB_HELD, selecting the newest still-held thumb as the shift source.

### Thumb / shift tracking model

The engine tracks three distinct thumb-related pieces of state:

- **Physical held set** (`thumb_slots[SHIFT_0..SHIFT_3]`): every thumb/shift key that is physically down, with per-side press timestamp, tap binding, and position. Multiple thumbs may be held simultaneously; this tracking is independent of the state machine.
- **Logical active thumb** (`active_thumb`): the single shift source used for *new* input. When the active thumb is released while another remains held, it is promoted to the **newest still-held thumb** (by press timestamp), and `thumb_tap_binding`/`thumb_position` are refreshed from the promoted slot. The watchdog re-entry path applies the same rule.
- **Buffered thumb context** (`buffered_thumb_side`): the shift source frozen at entry to `CHAR_THUMB_WAIT` / `THUMB_CHAR_WAIT`. All shifted output and shifted-combo lookups that resolve these buffered interactions use this frozen side — promotion never retroactively changes the combo interpretation of an already-buffered interaction. The field is cleared when the buffered interaction resolves.

The `thumb_used` flag remains global for the whole thumb-active episode: a promoted thumb inherits `thumb_used=true` from its predecessor, so the final release of a used episode never emits a tap even if the originally-pressed thumb was released earlier. No multi-shift composition is introduced — at most one logical shift source is active at any time.

### Hold-Tap Integration Notes

Integrated hold-tap support is implemented inside the nicola engine. One subtle
rule worth calling out here is the **prior plain blocker** used for rolling
input:

- When an HT key is pressed while an older plain key is already active, the HT
  snapshots `blocked_by_prior_plain = true` so it does not lone-promote to HOLD
  just because `tapping_term` elapsed during a roll.
- This blocker is re-checked at `tapping_term` time against the **physical char
  held set**, not only live trackers. That means a plain key that was already
  emitted/freed still blocks lone HOLD until its physical release arrives.
- Once the older plain key is physically released, the HT may promote on its
  own `tapping_term` if no other older plain remains held.

This keeps rolling protection intact while avoiding the bug where an HT could
remain stuck in `TRK_UNDECIDED` forever after the original blocker had already
been released.

### Detailed Transitions

#### IDLE State

| Event | Action | Next State |
|---|---|---|
| Character key press | Buffer key + timestamp, start timer | CHAR_WAIT |
| Thumb key press | Set active thumb, `thumb_used = false` | THUMB_HELD |
| Character key release | 無視（出力済みのかなに対する遅れた release） | IDLE |
| Thumb key release | 無視（出力済みの親指に対する遅れた release） | IDLE |

#### CHAR_WAIT State (文字キー押下済み、親指キー待ち)

| Event | Action | Next State |
|---|---|---|
| Thumb key press (within timeout) | Cancel timer, buffer thumb, start new timer | CHAR_THUMB_WAIT |
| Timer expired | Output unshifted kana | IDLE |
| Another char key press (規格書図3) | Output unshifted kana for buffered, buffer new key | CHAR_WAIT |

#### CHAR_THUMB_WAIT State (文字+親指バッファ中、5.4.2/5.5.2 判定待ち)

| Event | Action | Next State |
|---|---|---|
| 2nd char key press | 5.4.2 partitioning (d1 vs d2 comparison) | CHAR_WAIT or THUMB_HELD |
| Buffered char key release | 5.5.2 sequential detection (t1 vs t2×n) | THUMB_HELD or stay |
| Timer expired | Output shifted kana | THUMB_HELD |

#### THUMB_HELD State (親指キーホールド中)

| Event | Action | Next State |
|---|---|---|
| Character key press | Output shifted kana immediately, `thumb_used = true`, reset watchdog | THUMB_HELD |
| Character key release | 無視（シフトかなは出力済み） | THUMB_HELD |
| Thumb key release (`thumb_used = true`) | — | IDLE |
| Thumb key release (`thumb_used = false`) | Output thumb-tap binding (Space) | IDLE |
| Watchdog timeout (1s) | Silent reset (no output) | IDLE |

Note: After watchdog reset, the physical held set is preserved. If any thumb is still physically held when the next char key arrives in IDLE, the engine re-enters THUMB_HELD using the newest still-held thumb as active.

### 文字-親指-文字 の振り分け（規格書 5.4.2）

CHAR_WAIT 中に親��キーが来た後、さらに短い間隔で文字キーが来る場合:

```c
// 文字a ON (t=0) → 親指s ON (t=t1) → 文字b ON (t=t1+t2)
if (t1 <= t2) {
    // a + s が同時打鍵、b は新たに CHAR_WAIT へ
    output_shifted_kana(a, s);
    buffer_char(b);
} else {
    // a は単独打鍵、s + b が同時打鍵
    output_unshifted_kana(a);
    output_shifted_kana(b, s);
}
```

### 親指-文字-親指 の振り分け（規格書 5.4.2 末尾）

規格書は「親指キーa ON → 文字キー ON → 親指キーb ON」のケースも適切に振り分ける必要があると規定している。THUMB_WAIT 中に文字キーが来た後、さらに短い間隔で別の親指キーが来る場合:

```c
// 親指a ON (t=0) → 文字c ON (t=t1) → 親指b ON (t=t1+t2)
if (t1 <= t2) {
    // a + c が同時打鍵、親指b は新たに THUMB_WAIT へ
    output_shifted_kana(c, a);
    buffer_thumb(b);
} else {
    // 親指a は単独タップ、b + c が同時打鍵
    output_thumb_tap(a);
    output_shifted_kana(c, b);
}
```

注: 実用上このパターンの発生頻度は低い（両親指を交互に素早く打つケース）が、規格準拠のため実装する。

### 逐次打鍵との識別（規格書 5.5.2）

親指キー共用型（親指タップ = スペース等）の場合に重要:

```c
// 文字a ON (t=0) → 親指s ON (t=t1) → 文字a OFF (t=t1+t2)
// t1: 文字ON → 親指ON の時間差
// t2: 親指ON → 文字OFF の時間差
if (t1 > t2 * SEQUENTIAL_THRESHOLD_N) {
    // 逐次打鍵: 文字a は単独、親指s も単独
    output_unshifted_kana(a);
    // 親指s は THUMB_WAIT へ
} else {
    // 同時打鍵成立
    output_shifted_kana(a, s);
}
// SEQUENTIAL_THRESHOLD_N は設定可能な定数（規格書では n として実装依存）
```

注: t2 が極端に短い場合（ぶれ）は、t1 によらず逐次打鍵と判定する閾値も設ける。

### Timer Implementation

タイマーは **CHAR_WAIT 状態でのみ** 使用する。親指キーはタイマー不要（押下で直接 THUMB_HELD に遷移）。

Zephyr の `k_work_delayable` API を使用:

```c
static struct k_work_delayable timeout_work;

// CHAR_WAIT 開始時:
k_work_schedule(&timeout_work, K_MSEC(timing.timeout_ms));

// CHAR_WAIT 中に親指キー到着（タイマー不要に）:
k_work_cancel_delayable(&timeout_work);

// タイムアウトハンドラ:
static void timeout_handler(struct k_work *work) {
    if (sm.state == CHORDIS_CHAR_WAIT) {
        output_unshifted(sm.char_timestamp);
        sm.state = CHORDIS_IDLE;
    }
}
```

### 修飾キーパススルー（Modifier Passthrough）

NICOLA レイヤーがアクティブな状態でも、Ctrl+A, Shift+Z 等のショートカットが正しく動作する必要がある。NICOLA ではユーザーが Shift を直接押す操作はない（親指シフトを使う）ため、**いずれかの修飾キーが押されている場合、文字キー behavior はかな処理をスキップしてデフォルトレイヤーの ASCII キーコードを出力**する。

```c
// behavior_cdis.c
#define ANY_MODS (MOD_LCTL | MOD_RCTL | MOD_LSFT | MOD_RSFT | \
                  MOD_LALT | MOD_RALT | MOD_LGUI | MOD_RGUI)
```

#### 2つの呼び出し経路

NICOLA behavior が呼び出される経路は 2 つあり、それぞれパススルーの実装が異なる:

**(a) Keymap レイヤー走査からの直接呼び出し**（`&cdis_to` 等が直接キーマップに配置されている場合）:

`ZMK_BEHAVIOR_TRANSPARENT` を返し、ZMK keymap の走査が次の下位レイヤーに継続する。デフォルトレイヤーの `&kp A` が呼ばれ、Ctrl+A として OS に送信される。`binding->param1 == 0`（zero binding cells）で判別。

**(b) hold-tap からの直接 invoke**（`bindings = <&kp>, <&cdis_to>` の tap 発火）:

hold-tap は `zmk_behavior_invoke_binding()` で tap binding を直接呼び出す。この場合 `ZMK_BEHAVIOR_TRANSPARENT` を返してもレイヤー走査は発生しない。代わりに `binding->param1`（hold-tap が渡す tap パラメータ、例: `J` = 0x0007000D）を使い、`raise_zmk_keycode_state_changed_from_encoded()` でキーコードを直接発行する。

```c
static int cdis_binding_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    if (zmk_hid_get_explicit_mods() & ANY_MODS) {
        if (binding->param1 != 0) {
            // (b) hold-tap 経由: キーコードを直接発行
            raise_zmk_keycode_state_changed_from_encoded(
                binding->param1, true, event.timestamp);
            // position ごとに PT_DIRECT として記録（release で対になる release を発行）
            return ZMK_BEHAVIOR_OPAQUE;
        }
        // (a) keymap 経由: レイヤーフォールスルー
        // position ごとに PT_TRANSPARENT として記録（release でも TRANSPARENT を返す）
        return ZMK_BEHAVIOR_TRANSPARENT;
    }
    // ... normal NICOLA processing
}
```

#### Release の対称性

**press と release で同じパススルー経路を使わなければならない。** press で TRANSPARENT を返した position の release で OPAQUE を返すと、base layer の key release が呼ばれず HID レポートにキーが残留する（stuck key）。

per-position の状態を追跡し、release 時に対応する処理を行う:

| Press 時の判定 | Release 時の動作 |
|---|---|
| PT_TRANSPARENT | `ZMK_BEHAVIOR_TRANSPARENT` を返す（keymap が base layer の release を呼ぶ） |
| PT_DIRECT | `raise_zmk_keycode_state_changed_from_encoded(keycode, false, ...)` を発行 |
| PT_NONE（通常） | `chordis_on_char_released()` を呼ぶ |

### Caps-word パススルー

ZMK の `&caps_word` は **implicit modifiers** を使用してシフトを適用する（`ev->implicit_modifiers |= MOD_LSFT`）。これは `zmk_hid_get_explicit_mods()` には反映されないため、通常の修飾キーパススルー検出では caps-word を検知できない。

caps-word がアクティブな状態で NICOLA 処理を行うと、ローマ字出力（例: "ka"）が caps-word により大文字化（"Ka"）され、IME が正しく変換できなくなる。

**解決策**: `zmk,chord-input-config` の DTS プロパティ `passthrough-behaviors` にパススルー対象の behavior デバイスを列挙する。対象の `data->active` フラグ（struct の先頭フィールド）を直接読み取り、いずれかが `true` であればパススルーを発動する。

```dts
cdis_config: cdis_config {
    compatible = "zmk,chord-input-config";
    passthrough-behaviors = <&shift_word>;  /* or <&caps_word &shift_word> */
};
```

- プロパティ未設定の場合: `chordis_is_passthrough_behavior_active()` は定数 `false` にコンパイルされる（ゼロオーバーヘッド）
- behavior 名をハードコードしない: caps_word、shift_word、その他の互換 behavior に対応可能
- 要件: 対象 behavior の device data 先頭が `bool active` であること（caps_word / shift_word 共通）

この検出は `behavior_cdis.c` と `behavior_cdis_passthrough.c` の両方のパススルー判定に組み込まれている。

### Passthrough-word（一時的 ASCII パススルー）

NICOLA モードの入力中に一時的に半角英字を入力する必要がある場合（例: IME 変換保留中にインラインで英単語を入力）、`&cdis_pt_word` を使用する。

**動作**:

1. `&cdis_pt_word` を押す → `ime-keycode`（デフォルト: `LANG2`）をタップし、IME を英字入力モードに切り替え → `active = true`
2. NICOLA 文字キーは `chordis_is_passthrough_behavior_active()` 経由で検知 → ベースレイヤーにフォールスルー → ASCII キーが出力される
3. 非継続キー（alpha/numeric/modifier/continue-list 以外）を押す → 自動的に `active = false` に戻る

**`passthrough-behaviors` への登録は不要**: `caps_word_detect.h` が `DT_HAS_COMPAT_STATUS_OKAY(zmk_behavior_chord_input_passthrough_word)` で自動検出する。外部 behavior（`&caps_word` / `&shift_word`）のみ `passthrough-behaviors` プロパティへの登録が必要。

**`continue-list` / `strict-modifiers` のセマンティクス**: `zmk-shift-word` と互換。`zmk_key_param` による modifier-aware マッチングを使用し、`strict-modifiers` 有効時は explicit modifier が押されている場合に alpha/numeric の built-in continuation をバイパスする。

**IME キーコード**: DTS プロパティ `ime-keycode`（アクティベーション時）と `ime-restore-keycode`（デアクティベーション時）で設定。macOS の標準日本語 IME では、変換保留中に `LANG2`（japanese_eisuu）を1回タップすると英字入力モードに切り替わり、`LANG1`（japanese_kana）でかなモードに復帰する。`= <0>` でタップを無効化できる。

**イベント順序**: IME キーコードのタップは `active = true` の設定前に行われる。これにより、リスナーが IME キーコードのイベントを「非継続キー」として検出して即座に deactivate することを防ぐ。

```dts
cdis_pt_word: cdis_pt_word {
    compatible = "zmk,behavior-chord-input-passthrough-word";
    #binding-cells = <0>;
    ime-keycode = <LANG2>;
    ime-restore-keycode = <LANG1>;
    continue-list = <UNDERSCORE BACKSPACE DELETE MINUS>;
};
```

### Behavior ベースの協調（グローバルリスナー不要）

すべてを behavior の binding callback で処理する:

1. 文字キー → `&cdis_*` の `binding_pressed` / `binding_released` がステートマシンを操作
2. 親指キー → `&cdis_thumb_*` の `binding_pressed` / `binding_released` がステートマシンを操作
3. 修飾キー → デフォルトレイヤーの `&kp` 等がそのまま動作
4. レイヤー切り替え → ZMK 標準動作（behavior は関与しない）

これにより、レイヤー切り替えやイベント横取りの問題が完全に解消される。詳細は Section 9 を参照。

備考: 規格書の参考セクション（第5章）は「同時打鍵マーカー方式」を推奨しており、英字モード時に同時打鍵検出を ON/OFF する必要がない利点を述べている。本実装ではレイヤー切り替え（`&cdis_on` / `&cdis_off`）により NICOLA behavior と英字入力を完全に分離するため、マーカー方式は不採用。規格書が指摘する「英字入力を高速に行った場合に誤って同時打鍵と判断される」問題は、レイヤー分離により回避済み。

## 6. Kana Output Implementation

### Romaji Output via HID Events

Each kana ID maps to a romaji string in a lookup table (`romaji_table[]` in `chordis_engine.c`). Output is performed by raising `zmk_keycode_state_changed` events — one press/release pair per ASCII character:

```
behavior → chordis_output_kana(kana_id, timestamp)
  → romaji_table[kana_id] → e.g. "ka"
  → raise press 'K', raise release 'K'
  → raise press 'A', raise release 'A'
  → hid_listener processes each synchronously → HID reports sent in order
```

**イベントは同期処理**: `zmk_event_manager_raise()` はリスナーをインラインで順次呼び出す（キューイングなし）。複数のイベントを連続して raise すれば、HID レポートもその順序で確実に送信される。

**タイマーコールバックからの出力**: `k_work_delayable` のコールバックからも同じパターンで安全に出力可能。hold-tap が `zmk_behavior_invoke_binding()` をタイマーコールバック内から呼んでいるのと同じ。

### Output Function

```c
void chordis_output_kana(uint32_t kana_code, int64_t timestamp) {
    uint16_t kana_id = (uint16_t)kana_code;
    if (kana_id == KANA_NONE || kana_id >= KANA_COUNT) return;

    const char *romaji = romaji_table[kana_id];
    if (!romaji) return;

    for (const char *p = romaji; *p; p++) {
        uint32_t keycode = ascii_to_hid(*p);
        raise_zmk_keycode_state_changed_from_encoded(keycode, true, timestamp);
        raise_zmk_keycode_state_changed_from_encoded(keycode, false, timestamp);
    }
}
```

注: すべて即座に press+release する「タップ出力」方式。キーリピートは発生しない（Section 12 参照）。

### Kana ID Encoding

Each kana is a simple integer ID (defined in `keys.h`). IDs are sequential, used as indices into `romaji_table[]`:

```c
#define KANA_NONE       0   // No output
#define KANA_A          1   // あ → "a"
#define KANA_I          2   // い → "i"
#define KANA_U          3   // う → "u"
#define KANA_KA         6   // か → "ka"
#define KANA_GA         51  // が → "ga"
#define KANA_SMALL_A    46  // ぁ → "xa"
#define KANA_KYA        71  // きゃ → "kya"
// ... (full table in keys.h)
```

The romaji table covers all standard kana including yōon (拗音), small kana, and special characters. Unknown or KANA_NONE IDs produce no output.

### Raw Keycode Embedding: `NC_KEY()`

Some layouts (e.g. naginata) need non-kana keys (backspace, cursor arrows) on certain faces of a NICOLA character key. The `NC_KEY()` macro allows embedding a raw ZMK keycode in any kana face slot:

```c
/* Q: unshifted=ヴ, center-shift=Backspace */
CHORD_INPUT_BEHAVIOR5(ng_vu, KANA_VU, NC_KEY(BSPC), KANA_NONE, KANA_NONE, KANA_NONE)

/* T: always Left arrow regardless of shift */
CHORD_INPUT_BEHAVIOR5(ng_left, NC_KEY(LEFT), NC_KEY(LEFT), KANA_NONE, KANA_NONE, KANA_NONE)

/* Modifier-wrapped keycodes also work */
CHORD_INPUT_BEHAVIOR5(ng_del, NC_KEY(LS(BSPC)), KANA_NONE, KANA_NONE, KANA_NONE, KANA_NONE)
```

**Mechanism**: The `kana[]` value space is partitioned by `NC_RAW_KEYCODE_THRESHOLD` (0x10000):

| Range | Interpretation |
|---|---|
| `[0, 0x10000)` | Kana ID — looked up in `romaji_table[]` |
| `[0x10000, 0xFFFFFFFF]` | Raw ZMK encoded keycode — tapped directly via HID |

This partition is natural because ZMK keycodes always include a nonzero HID usage page in bits 16-23 (keyboard page = `0x07`, so the smallest keyboard keycode is `0x00070000`). Kana IDs currently use up to 183 and have room to grow to 65535 without conflict.

`NC_KEY(k)` is defined as `(k)` — a documentation-only wrapper. ZMK keycodes are inherently above the threshold, so the macro serves to signal intent in the DTS source.

## 7. User-Facing API

### Minimal Setup

User's `west.yml`:
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

User's keymap:
```dts
#include <zmk-chordis/nicola.dtsi>

/ {
    keymap {
        compatible = "zmk,keymap";

        default_layer {
            display-name = "QWERTY";
            bindings = <
                &kp Q  &kp W  &kp E  &kp R  &kp T
                &kp Y  &kp U  &kp I  &kp O  &kp P
                // ... (通常の QWERTY 配列)
                &cdis_on LY_NICOLA  &kp SPACE  // NICOLA + IME かな ON
            >;
        };

        nicola_layer {
            display-name = "NICOLA";
            bindings = <
                // Row 1 (top: D段)
                &cdis_dot  &cdis_ka  &cdis_ta  &cdis_ko  &cdis_sa
                &cdis_ra   &cdis_chi &cdis_ku  &cdis_tsu &cdis_com

                // Row 2 (home: C段)
                &cdis_u    &cdis_si  &cdis_te  &cdis_ke  &cdis_se
                &cdis_ha   &cdis_to  &cdis_ki  &cdis_i   &cdis_nn

                // Row 3 (bottom: B段)
                &cdis_dot2 &cdis_hi  &cdis_su  &cdis_fu  &cdis_he
                &cdis_me   &cdis_so  &cdis_ne  &cdis_ho  &cdis_slash

                // Thumb keys: behavior でタップ出力を指定
                &cdis_thumb_l SPACE   &cdis_thumb_r SPACE

                // モード切り替え: &cdis_off で英字 + IME 英字 OFF
                &cdis_off LY_NICOLA
            >;
        };
    };
};
```

### Pre-built NICOLA Keymap Template

`nicola.dtsi` は以下をすべて提供:
- 全 30 キーの文字キー behavior インスタンス（`&cdis_u`, `&cdis_ka`, ...）
- 親指キー behavior インスタンス（`&cdis_thumb_l`, `&cdis_thumb_r`）
- モード切り替え behavior インスタンス（`&cdis_on`, `&cdis_off`）
- グローバル設定ノード（`&cdis_config`）
- JIS かなキーコード定数

```dts
// nicola.dtsi (モジュール提供)
#include <zmk-chordis/keys.h>
#include <zmk-chordis/chord-input-map.h>

/ {
    cdis_config: cdis_config {
        compatible = "zmk,chord-input-config";
        timeout-ms = <100>;
        sequential-threshold = <2>;
        sequential-min-overlap-ms = <20>;
    };

    behaviors {
        // 文字キー (30 instances)
        CHORD_INPUT_BEHAVIOR(u,     KANA_U,     KANA_WO,       KANA_VU)
        CHORD_INPUT_BEHAVIOR(shi,   KANA_SHI,   KANA_A,        KANA_JI)
        CHORD_INPUT_BEHAVIOR(te,    KANA_TE,    KANA_NA,       KANA_DE)
        // ... 30 instances total

        // 親指キー (2 instances)
        cdis_thumb_l: cdis_thumb_l {
            compatible = "zmk,behavior-chord-input-thumb";
            #binding-cells = <1>;
            side = "left";
            bindings = <&kp>;
        };
        cdis_thumb_r: cdis_thumb_r {
            compatible = "zmk,behavior-chord-input-thumb";
            #binding-cells = <1>;
            side = "right";
            bindings = <&kp>;
        };

        // モード切り替え (2 instances, idempotent)
        // レイヤー番号はキーマップで指定: &cdis_on LY_NICOLA, &cdis_off LY_NICOLA
        cdis_on: cdis_on {
            compatible = "zmk,behavior-chord-input-toggle";
            #binding-cells = <1>;
            toggle-action = "on";
            ime-keycode = <LANG1>;
        };
        cdis_off: cdis_off {
            compatible = "zmk,behavior-chord-input-toggle";
            #binding-cells = <1>;
            toggle-action = "off";
            ime-keycode = <LANG2>;
        };
    };
};

### Layout Customization

`nicola.dtsi` は標準 NICOLA 配列を提供するが、ユーザーは DTS のプロパティオーバーライド機構を使って自由にカスタマイズできる。カスタマイズには 3 つのレベルがある:

#### レベル 1: 配置の変更（キーの位置入れ替え）

キーマップで `&cdis_*` の並び順を変えるだけ。behavior 定義の変更は不要:

```dts
nicola_layer {
    bindings = <
        // 標準: &cdis_u  &cdis_si  &cdis_te  &cdis_ke  &cdis_se
        // カスタム: 「う」と「し」を入れ替え
        &cdis_si  &cdis_u  &cdis_te  &cdis_ke  &cdis_se
        // ...
    >;
};
```

#### レベル 2: かな割り当ての変更

`nicola.dtsi` をインクルードした後に、DTS プロパティをオーバーライド:

```dts
#include <behaviors/cdis-nicola.dtsi>

// 「か」キーの右親指シフトを「が」→「ぎ」に変更
&cdis_ka { kana = <KANA_KA KANA_E KANA_GI>; };

// 「ん」キーの右親指シフトに「っ」の代わりに「を」を割り当て
&cdis_nn { kana = <KANA_NN KANA_NONE KANA_WO>; };
```

`kana` プロパティは `<unshifted left-shifted right-shifted>` の 3 要素配列。
利用可能なかな定数は `keys.h` で定義されている（`KANA_KA`, `KANA_GA`, `KANA_SMALL_YA`, `KANA_NONE` 等）。

#### レベル 3: 独自キーの追加定義

`CHORD_INPUT_BEHAVIOR` マクロで完全に新しいキーを定義:

```dts
#include <zmk-chordis/keys.h>
#include <zmk-chordis/chord-input-map.h>

/ { behaviors {
    // 標準配列にない独自のキー定義
    CHORD_INPUT_BEHAVIOR(my_wa, KANA_WA, KANA_WO, KANA_NN)
}; };

// キーマップで使用: &cdis_my_wa
```

`nicola.dtsi` をインクルードせず、すべてのキーを独自に定義することも可能。
これにより、NICOLA 以外の親指シフト配列（飛鳥配列、新下駄配列等）も実装できる。

#### レベル 4: かな以外のキーコードの埋め込み（NC_KEY）

薙刀式のように、配列内にバックスペースやカーソルキーを含むレイアウトでは、`NC_KEY()` で ZMK キーコードを直接かな面に埋め込める:

```dts
#include <dt-bindings/zmk/keys.h>
#include <zmk-chordis/keys.h>
#include <zmk-chordis/chord-input-map.h>

/ { behaviors {
    /* 単打=ヴ、センターシフト=BS */
    CHORD_INPUT_BEHAVIOR5(ng_vu, KANA_VU, NC_KEY(BSPC), KANA_NONE, KANA_NONE, KANA_NONE)

    /* 単打もシフトも左矢印 */
    CHORD_INPUT_BEHAVIOR5(ng_left, NC_KEY(LEFT), NC_KEY(LEFT), KANA_NONE, KANA_NONE, KANA_NONE)
}; };
```

`NC_KEY()` はかなと自由に混在でき、同じキーの別の面にかなと生キーコードを同居させられる。詳細は「Kana ID Encoding > Raw Keycode Embedding: NC_KEY()」を参照。

## 8. Configuration Options

### `zmk,chord-input-config` プロパティ（グローバル設定）

| Property | Type | Default | Description |
|---|---|---|---|
| `timeout-ms` | int | 100 | 同時打鍵判定時間 (ms)。規格書では50-200ms、推奨100ms |
| `sequential-threshold` | int | 2 | 逐次打鍵判定の係数 n（規格書 5.5.2: t1 > t2×n で逐次打鍵）|
| `sequential-min-overlap-ms` | int | 20 | t2 がこの値以下なら無条件で逐次打鍵と判定（ぶれ対策）|

### `zmk,behavior-chord-input` プロパティ（文字キー、per-instance）

| Property | Type | Description |
|---|---|---|
| `kana` | array | 3 要素の配列: `<unshifted left-shifted right-shifted>` |

### `zmk,behavior-chord-input-thumb` プロパティ（親指キー、per-instance）

| Property | Type | Default | Description |
|---|---|---|---|
| `side` | string | (required) | `"left"` or `"right"` |
| `bindings` | phandle | `<&kp>` | タップ時に使う behavior。キーマップの binding-cell でパラメータを渡す |

### `zmk,behavior-chord-input-toggle` プロパティ（モード切り替え、per-instance）

| Property | Type | Default | Description |
|---|---|---|---|
| `toggle-action` | string | (required) | `"on"` or `"off"` |
| `ime-keycode` | int | (required) | OS に送信する IME 切り替えキーコード（`LANG1` / `LANG2`） |

binding-cells: `<layer>` — キーマップからレイヤー番号を指定（例: `&cdis_on LY_NICOLA`）

### `zmk,behavior-chord-input-passthrough-word` プロパティ（一時的 ASCII パススルー、per-instance）

| Property | Type | Default | Description |
|---|---|---|---|
| `ime-keycode` | int | 0 | アクティベーション時にタップする IME 切り替えキーコード（例: LANG2）。0 でタップ無効 |
| `ime-restore-keycode` | int | 0 | デアクティベーション時にタップする IME 復帰キーコード（例: LANG1）。0 でタップ無効 |
| `continue-list` | array | (required) | パススルーを継続するキーコード（alpha/numeric/modifier に追加） |
| `strict-modifiers` | boolean | false | true の場合、explicit modifier 押下時は alpha/numeric も continue-list で判定 |

binding-cells: `<0>` — パラメータなし（例: `&cdis_pt_word`）。トグル動作（再押下で deactivate）。

### Kconfig

デバイスツリーに `zmk,behavior-chord-input` ノードが存在すれば `DT_HAS_ZMK_BEHAVIOR_CHORD_INPUT_ENABLED` が自動で `y` になり、ビルドが有効化される。ユーザーが `.conf` で明示的に設定する必要はない。

```kconfig
config ZMK_BEHAVIOR_CHORDIS
        bool
        default y
        depends on DT_HAS_ZMK_BEHAVIOR_CHORD_INPUT_ENABLED
```

これは ZMK v0.3 本体の behavior と同じパターン（例: `ZMK_BEHAVIOR_HOLD_TAP`）。

## 9. Implementation Considerations

### Behavior 間の協調: 共有ステートマシン（nicola_engine）

`behavior_cdis.c`（文字キー）、`behavior_cdis_thumb.c`（親指キー）、`behavior_cdis_toggle.c`（モード切り替え）は、`chordis_engine.c` に定義された**共有シングルトンステートマシン**を操作する。ヘッダ `chordis_engine.h` が公開 API を定義。

```c
// chordis_engine.c に定義
static struct {
    enum chordis_state_id state;     // IDLE, CHAR_WAIT, THUMB_HELD
    uint32_t buffered_kana[3];      // [unshifted, left-shifted, right-shifted]
    int64_t  char_timestamp;
    enum chordis_thumb_side active_thumb;
    int64_t  thumb_timestamp;
    bool     thumb_used;            // THUMB_HELD 中に文字キーが押されたか
    struct zmk_behavior_binding thumb_tap_binding;  // 親指タップ時の binding
    uint32_t thumb_position;
    struct k_work_delayable timeout_work;
    bool timer_initialized;
} sm;
```

公開 API:
```c
void chordis_engine_init(void);      // 初期化（idempotent）
void chordis_on_char_pressed(const uint32_t kana[3], ...);
void chordis_on_char_released(...);
void chordis_on_thumb_pressed(enum chordis_thumb_side side, const struct zmk_behavior_binding *tap, ...);
void chordis_on_thumb_released(enum chordis_thumb_side side, ...);
void chordis_engine_flush(int64_t timestamp);  // モード切り替え時のバッファ強制出力
```

#### 文字キーの binding callback (`behavior_cdis.c`):

```c
static int cdis_binding_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    // 修飾キーパススルー（2経路、Section 5 参照）
    if (zmk_hid_get_explicit_mods() & ANY_MODS) {
        if (binding->param1 != 0) {
            // hold-tap 経由: キーコードを直接発行
            raise_zmk_keycode_state_changed_from_encoded(
                binding->param1, true, event.timestamp);
            passthrough_state[pos] = PT_DIRECT;
            passthrough_keycode[pos] = binding->param1;
            return ZMK_BEHAVIOR_OPAQUE;
        }
        // keymap 経由: レイヤーフォールスルー
        passthrough_state[pos] = PT_TRANSPARENT;
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    passthrough_state[pos] = PT_NONE;
    const struct behavior_cdis_config *cfg = dev->config;
    chordis_on_char_pressed(cfg->kana, event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int cdis_binding_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    switch (passthrough_state[pos]) {
    case PT_TRANSPARENT: return ZMK_BEHAVIOR_TRANSPARENT;
    case PT_DIRECT:
        raise_zmk_keycode_state_changed_from_encoded(
            passthrough_keycode[pos], false, event.timestamp);
        return ZMK_BEHAVIOR_OPAQUE;
    default:
        chordis_on_char_released(event);
        return ZMK_BEHAVIOR_OPAQUE;
    }
}
```

#### 親指キーの binding callback (`behavior_cdis_thumb.c`):

```c
static int cdis_thumb_binding_pressed(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    const struct behavior_cdis_thumb_config *cfg = dev->config;

    // DT の bindings phandle からデバイス名、keymap の binding-cell から param1
    struct zmk_behavior_binding tap = {
        .behavior_dev = cfg->tap_behavior_dev,
        .param1 = binding->param1,
    };
    chordis_on_thumb_pressed(cfg->side, &tap, event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int cdis_thumb_binding_released(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    const struct behavior_cdis_thumb_config *cfg = dev->config;
    chordis_on_thumb_released(cfg->side, event);
    return ZMK_BEHAVIOR_OPAQUE;
}
```

#### 親指タップの出力 (`chordis_engine.c`):

保存された binding を invoke して任意の behavior（`&kp`, `&mkp` 等）を発火。hold-tap と同じ `zmk_behavior_invoke_binding()` API を使用:

```c
static void output_thumb_tap(int64_t timestamp) {
    struct zmk_behavior_binding_event ev = {
        .position = sm.thumb_position,
        .timestamp = timestamp,
    };
    zmk_behavior_invoke_binding(&sm.thumb_tap_binding, ev, true);   // press
    zmk_behavior_invoke_binding(&sm.thumb_tap_binding, ev, false);  // release
}
```

### グローバルリスナー（最適化、必須ではない）

イベントを消費（`HANDLED`）することは**一切ない**。CHAR_WAIT 中に非 NICOLA キーが押された場合のバッファ flush 最適化のみ:

```c
static int nicola_listener(const zmk_event_t *eh) {
    if (state.state == CHAR_WAIT) {
        struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
        if (ev->state) {
            // 非 NICOLA キー押下 → バッファを即座に flush
            // (NICOLA キーの場合は binding callback が先に処理するため
            //  ここに到達する時点で既に state が変わっている)
            flush_unshifted();
            state.state = IDLE;
        }
    }
    return ZMK_EV_EVENT_BUBBLE;  // 常に BUBBLE: イベントを消費しない
}

ZMK_LISTENER(nicola, nicola_listener);
ZMK_SUBSCRIPTION(nicola, zmk_position_state_changed);
```

注: このリスナーがなくてもタイムアウト（デフォルト100ms）でバッファは flush される。リスナーは応答性向上のための最適化であり、M4 以降で実装。

### Concurrency

- `cdis_*` と `cdis_thumb_*` の binding callback は ZMK keymap から同一スレッドで呼ばれるため、通常の binding 操作間でのレース条件はない
- タイマーコールバック（`k_work_delayable`）はシステムワークキューで実行されるため、ステート遷移時の排他制御が必要
- `k_work_cancel_delayable` の戻り値が `-EINPROGRESS` の場合（コールバックが既に実行中）への対処が必要（hold-tap の実装パターンを参考）

### Memory

- グローバルステートマシン: 1 インスタンス（~64 bytes）
- 文字キー behavior: per-instance config（3 × uint32_t = 12 bytes × 30 = ~360 bytes）
- 親指キー behavior: 2 インスタンス（~32 bytes）
- 合計: ~460 bytes

### Split Keyboard Support

スプリットキーボードでは、親指キーと文字キーが異なる半分に配置される可能性がある。ZMK のスプリットトランスポートはキーイベントをセントラル側で統合するため、binding callback は正しい順序で呼ばれる。ただし、BLE 通信による追加レイテンシ（~10ms）を考慮し、`timeout-ms` は余裕を持って設定すること。

## 10. Accessibility (規格書 第6章)

規格書はアクセシビリティ対応として以下を規定:

### 6.1 緩慢な打鍵操作（ゆっくり打つユーザー）

親指キーを先に押し、後から文字キーを打鍵する操作（Prefix 型）において、親指キー押下中のタイムアウトを無くすことで対応可能。現在の 3-state 設計では親指キー押下で直接 THUMB_HELD に遷移し、タイムアウトなしで待機するため、**標準で対応済み**。

### 6.2 一本指操作

一回の動作で一つのキーしか打鍵できない場合。親指キー単独打鍵の後に文字キーを押した場合も同時打鍵と認める方式。

実装: 親指キーのタップ後に一定時間内の文字キー入力をシフトかなとして扱う sticky-thumb モード（将来対応）。

## 11. Key Types (規格書 参考 第3章)

規格書では4種類のキーを定義:

| キー種別 | 同時打鍵関与 | リピート | 例 |
|---|---|---|---|
| 親指キー | あり | なし | 親指左、親指右 |
| 文字キー | あり | あり | かな文字キー (B01-D11) |
| ダイレクトリピートキー | なし | あり | Space, Tab, Esc, BS, Return |
| ダイレクトノンリピートキー | なし | なし | Ctrl, Shift, Alt |

本実装では、`&cdis_*` バインディングが付いたキーが「文字キー」、`&cdis_thumb_l` / `&cdis_thumb_r` が付いたキーが「親指キー」、それ以外はすべて「ダイレクトキー」（パススルー）として扱う。

## 12. Key Repeat（規格書 参考 第7章）

規格書の参考セクションはキーリピートについて以下を規定:

- **文字キー**: リピートあり（キーホールド中にリピート）
- **親指キー**: リピートなし
- **同時打鍵時**: シフトかなのリピート（文字or親指の片方が離されたら終了）
- **リピート中の新キー**: 新しいキーのリピートが優先

### 本実装でのリピート方針: リピート非対応（M1〜M4）

初期実装では、すべてのかな出力を**即座に press+release する「タップ出力」方式**とし、OS 側のキーリピートは発生しない:

```c
// output_kana() は press → release を即座に実行
raise_zmk_keycode_state_changed_from_encoded(encoded, true, timestamp);   // press
raise_zmk_keycode_state_changed_from_encoded(encoded, false, timestamp);  // release
```

**理由**:
- リピート対応には、出力中のキーコードの記憶、`binding_released` での release タイミング管理、CHAR_WAIT 中の早期確定 vs タイムアウトでの挙動分岐など、ステートマシンの複雑度が大幅に増す
- 特に濁音・半濁音（2キーストローク）は press 状態を維持できないため、リピート対応が本質的に困難
- 実用上、かな入力でキーリピートを多用する場面は限定的（同じかなの連続入力は稀）
- NICOLA 規格書のリピート仕様は「参考」（normative ではない）

**将来対応**: リピートが必要と判明した場合、清音の単独打鍵に限り press 維持方式を追加する（M5 以降で検討）。

## 13. Character-Character Combos and Hold Detection

### 背景: NICOLA 仕様の設計的強み

NICOLA 仕様は「文字キーとシフトキー（親指キー）の役割を完全に分離」している。この設計には以下の利点がある:

- **文字キー同士は干渉しない**: 高速ロールオーバー（前のキーを離す前に次を押す）で誤判定が起きない
- **「待ち」の対象が親指のみ**: CHAR_WAIT のタイムアウト1つで同時打鍵判定が完結する
- **逐次判定がシンプル**: 5.5.2 のオーバーラップ分析（文字+親指のみ）で十分

### 文字キー同士のコンボが必要な配列

薙刀式や新下駄式では、文字キー同士の同時押しで拗音を生成する（例: し + る → しょ）。さらに、濁音拗音では3キー同時押しが必要になる（例: し + る + 濁点 → じょ）。

薙刀式では濁点キー（F/J）・半濁点キー（B/M）も通常の文字キー（behavior-nicola-char）として扱い、KANA_DAKUTEN / KANA_HANDAKUTEN マーカーをかなスロットに配置する。濁音・半濁音はコンボテーブルで処理され、グリッドシフトは使用しない。これにより F/J/B/M にセンターシフト面（ま/の/。/み）を持たせることができる。

この「文字キーが文字でもありコンボのパーツでもある」二重の役割が、NICOLA 仕様の前提と根本的に矛盾する:

| | NICOLA | 文字コンボあり |
|---|---|---|
| 文字キーの役割 | 単打 or 親指でシフト | 単打 or 親指シフト or コンボのパーツ |
| 文字キー押下後の「待ち」 | 親指の到着のみ | 親指 AND 別の文字キーの到着 |
| 文字キーリリースの意味 | 判定に影響（5.5.2） | **コンボ不要の意思表示** |

### タイムアウト方式の問題

NICOLA の同時打鍵判定時間（100ms）をそのまま文字コンボに適用すると:

1. **タイミングがシビア**: 3キー同時押し（濁音 + 文字 + 文字）では、全キーが100ms以内に到着しなければならない。実際のタイピングでは各キー間に50-150ms かかることが多く、100ms 窓に収まらないことがある
2. **タイムアウト値を大きくすると遅延が増加**: 全ての文字キーの単打出力が遅くなる

### 解決: ホールド検出方式（combo candidate 限定）

コンボテーブルに登録されたかな ID を持つキー（combo candidate）に限り、タイムアウトではなくキーの物理的なホールド状態で判定する:

- **combo candidate のキーが物理的に押されている間**: タイムアウトを再起動し続け、確定しない（コンボパートナーや親指の到着を待つ）
- **キーがリリースされた時**: コンボパートナーが来ていなければ即確定（単打 or シフト出力）
- **コンボパートナーが到着した時**: コンボとして即確定

これにより:
- **タイミングに依存しない**: キーを hold している限り、何秒後でもコンボが成立する
- **単打は速くなる**: リリース駆動なので、タップなら50-80ms で出力される（タイムアウト100ms より早い）
- **非 combo candidate キーは影響を受けない**: 従来通り100ms タイムアウト駆動
- **combo 未登録なら完全に無影響**: 全てのチェックが `combo_count > 0` でスキップされる

### combo_pending: 2キー以上のバッファリング

2つ目のキーが到着して直接コンボが見つかった場合、または `has_potential_chain()` が true（3キーチェーンの可能性がある）の場合、即出力せず `combo_pending` 状態に入る。この間:

- **3つ目のキー押下 (char_pressed)**: チェーンコンボを試行。3つのキーの全ペアリング（(1,2)+3, (1,3)+2, (2,3)+1）を中間結果経由で探索する
- **キーリリース (char_released)**: コンボを即解決（直接コンボがあれば出力、なければ両方を個別出力）。リリース = 「そのキーは参加完了」の意思表示
- **親指到着 (thumb_pressed)**: シフト付きコンボを試行（`lookup_shifted_combo`）。なければ直接コンボ。なければ両方をシフト出力
- **タイムアウト**: キーがホールドされていれば延長、されていなければコンボ解決

### 状態遷移への影響

ホールド検出は以下の状態のタイムアウトハンドラに適用される:

| 状態 | 条件 | ホールド時の動作 |
|---|---|---|
| CHAR_WAIT | char held + combo candidate | タイムアウト再起動（safety: 1s） |
| THUMB_CHAR_WAIT | char held + combo candidate | タイムアウト再起動（safety: 1s） |

CHAR_THUMB_WAIT は親指が既に到着しているため、NICOLA 仕様通りのタイムアウト処理を行う（ホールド延長なし）。

char_released による確定は combo candidate に限定:

| 状態 | combo candidate | 非 combo candidate |
|---|---|---|
| CHAR_WAIT (単体) | リリース時に即確定 | 従来通り（タイムアウト or 親指到着で判定） |
| CHAR_WAIT (combo_pending) | リリース時にコンボ解決 | — |
| THUMB_CHAR_WAIT | リリース時に shifted 確定 | （この状態には combo candidate のみ遷移） |

### コンボ探索

`lookup_any_combo()` は両キーの全かなスロット（最大5面）を総当たりで探索する。これにより、キーの単打面とシフト面にまたがるコンボ（例: し[単打=SI] + る[センターシフト=YO] → しょ）を検出できる。

`lookup_shifted_combo()` はシフトキーが active な場合に使用し、active シフト面を優先しつつ全スロットを探索する。優先順位:

1. shifted_a + any face of b（例: じ[濁音面] + よ[センター面] → じょ）
2. any face of a + shifted_b
3. any combination（fallback）

### チェーンコンボ

3キー同時押し（例: し + る + 濁点 → じょ）は、2段階のコンボをチェーンして解決する:

1. combo(し, る) = しょ（中間結果）
2. combo(しょ, 濁点) = じょ（最終結果）

3つのキーの全ペアリングを試行する: (1,2)+3, (1,3)+2, (2,3)+1。これにより入力順に依存しない。

`has_potential_chain()` は、直接コンボが見つからない2キーの組み合わせに対して、将来3つ目のキーが到着した際にチェーンが成立しうるかを事前に判定する。これにより、不要な combo_pending を避けつつ、る+濁点 のような「直接コンボはないがチェーン候補」のペアを正しくバッファできる。

### コンボテーブル構成（薙刀式）

| 種類 | 数 | 例 |
|---|---|---|
| 拗音 | 33 | SI+YO→SYO, KI+YA→KYA |
| 濁音 | 21 | DAKUTEN+SI→ZI, DAKUTEN+KA→GA |
| 半濁音 | 5 | HANDAKUTEN+HA→PA |
| 濁音拗音 | 12 | DAKUTEN+SYO→ZYO, DAKUTEN+KYA→GYA |
| 半濁音拗音 | 3 | HANDAKUTEN+HYA→PYA |
| **合計** | **74** | CONFIG_ZMK_CHORDIS_MAX_CHAR_COMBOS = 80 |

### 設計原則

combo candidate なキーは NICOLA 仕様に完全には従わない。これは意図的な設計判断であり、NICOLA 仕様の拡張として位置づける:

- **NICOLA 仕様**: 文字キーリリース後も100ms以内なら親指でシフト可能（5.5.2 の逐次判定が適用される）
- **combo candidate**: 文字キーリリース = 即確定。リリース後に親指が来てもシフトされない

この違いは、文字コンボの「hold で意思表示」というパラダイムから必然的に生じる。combo candidate キーにとっては、hold/release が同時打鍵判定の一次的な指標であり、タイムアウトは BLE イベント欠落時の安全装置（safety timeout, 1s）に過ぎない。

## 14. Known Limitations

### マルチステップキーボードショートカット

修飾キーパススルーは**修飾キーが物理的に押されている間のみ**有効。修飾キーを途中で離すマルチステップシーケンス（例: Emacs の `C-x g` — Ctrl+X を押して離し、次に G を押す）では、2打目の G が修飾なしの NICOLA 入力として処理され、かなが出力される。

これはファームウェアの本質的な制約である。キーボードは OS アプリケーションが prefix key sequence の途中であるかどうかを知る手段を持たない。

**検討した対処法と評価:**

| 方法 | 評価 |
|---|---|
| 修飾キーを押し続ける（`C-x C-g`） | 変更不要。多くの Emacs コマンドはこの形式を受け付ける |
| アプリ側で rebind（`C-x C-g` → magit-status 等） | 確実だがアプリごとに設定が必要 |
| 「次の1キーをパススルー」behavior | 汎用だが余分な1打鍵が必要 |
| modifier 解放後の短時間パススルー（sticky） | 通常のかな入力直後に誤爆するリスクがあり非推奨 |

**推奨**: アプリ側で modifier-held バインディングに統一するか、必要なコマンドを rebind する。

### OS 側の IME 状態変更とレイヤーの非同期

`&cdis_on` / `&cdis_off` はキーボードのレイヤー状態と OS の IME 状態をアトミックに切り替えるが、**OS アプリケーションが独自に IME 状態を変更した場合**、キーボード側のレイヤー状態と乖離する。

例: Emacs でミニバッファ遷移時に IME を自動 OFF にする設定の場合、OS は英字モードに切り替わるが、キーボードは NICOLA レイヤーが有効なまま。NICOLA behavior が出力するローマ字 HID シーケンスは IME がアクティブであることを前提としているため、英字モードでは「ka」「si」等のローマ字がそのまま入力される。

OS → キーボードへの IME 状態通知の仕組みは HID プロトコルに存在しないため、ファームウェア側での自動同期は不可能。ユーザーは `&cdis_off` で手動切り替えを行うか、アプリ側の自動 IME 切り替えを無効にする必要がある。

### HID で表現できない文字の直接入力

HID プロトコルにはキーコードとして定義されたキーしか送信できず、任意の Unicode 文字を直接入力する仕組みがない。JIS かな入力モードでも、鉤括弧 `「」` は入力できるが、二重鉤括弧 `『』` や特殊記号は IME の変換候補を経由する必要がある。

OS ごとに Unicode 直接入力の方法は存在する（macOS の Unicode Hex Input: Option+コードポイント、Linux: Ctrl+Shift+U）が、いずれも日本語 IME とは別の入力ソースであり、IME がアクティブな状態では使用できない。ファームウェアからマクロとしてこれらのキーシーケンスを送信すること自体は技術的に可能だが、入力ソースの切り替えを挟む必要があり実用的でない。

**Karabiner-Elements 等の OS ユーザーランドツールとの連携** は現実的な解決策になりうる。ファームウェアから未使用の HID キーコード列（シグナル）を送信し、Karabiner がそれを捕捉して任意の文字を挿入する、という役割分担が考えられる。ただし、これは zmk-chord-input-system の範囲外であり、ユーザーが個別に設定する必要がある。

| レイヤー | 役割 | 例 |
|---|---|---|
| ZMK (ファームウェア) | 同時打鍵判定、かな→HID 変換 | KANA_SI → `s`, `i` |
| OS IME | ローマ字→かな→漢字変換 | `si` → し → 死/四/... |
| Karabiner 等 | HID で表現不可能な文字の挿入、アプリ固有処理 | シグナル → 『 |

## 15. Testing Plan

1. **Unit test**: State machine transitions (IDLE → CHAR_WAIT → output, THUMB_WAIT → THUMB_HELD, 振り分け判定, 逐次打鍵識別)
2. **Integration test**: Build with a test keymap on native_posix
3. **Manual test**: On a physical split keyboard with macOS/Windows JIS kana input
4. **Timing test**: Verify dakuten event output speed, split-half latency, 同時打鍵判定精度

## 16. Milestones

1. **M1: Module skeleton** - Build system, Kconfig, empty behavior that compiles ✅
2. **M2: Kana code table and romaji output** - Complete `keys.h` with all kana IDs, romaji lookup table in engine ✅
3. **M3: Basic behavior** - 3-state machine (IDLE/CHAR_WAIT/THUMB_HELD), modifier passthrough, hold-tap 互換（dual passthrough）, レイヤー順序要件 ✅
4. **M4: Full state machine** - 4-state (IDLE/CHAR_WAIT/CHAR_THUMB_WAIT/THUMB_HELD), 振り分け判定 (5.4.2)、逐次打鍵識別 (5.5.2)、multi-shift (up to 4)、THUMB_HELD watchdog、auto-off ✅
5. **M5: Character-character combos** - Combo engine (`lookup_any_combo` / `lookup_shifted_combo` / `has_potential_chain`), DT-driven combo registration, hold detection for combo candidates, THUMB_CHAR_WAIT state, chained 3-key combos, naginata dakuten/handakuten refactor from grid shift to char+combo ✅
6. **M6: Integrated hold-tap** - Hold-tap modifier support for combo candidate keys within nicola engine, including release-gap settle, per-position HT timers, rolling prior-plain protection, and passthrough/base-layer emission ✅
7. **M7: Keymap templates** - Pre-built dtsi for NICOLA standard, naginata, and shin-geta-based layouts
8. **M8: Documentation & testing** - README, usage guide, cross-platform testing
