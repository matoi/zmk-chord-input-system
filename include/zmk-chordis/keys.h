/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Key ID definitions for zmk-chord-input-system.
 *
 * Each entry is assigned a unique integer ID used in DTS properties
 * (kana = <KANA_KA KANA_GA KANA_NONE>) and as indices into the romaji
 * lookup table in chordis_engine.c.
 *
 * Romaji convention: nihon-shiki (si, ti, tu, hu) with x-prefix for
 * small kana (xa, xtu). "nn" for ん to avoid ambiguity.
 *
 * ASCII symbols and digits are included for use in custom layouts
 * beyond the standard NICOLA character set.
 */

#pragma once

#define KANA_NONE 0

/* === Vowels === */
#define KANA_A   1   /* あ → a   */
#define KANA_I   2   /* い → i   */
#define KANA_U   3   /* う → u   */
#define KANA_E   4   /* え → e   */
#define KANA_O   5   /* お → o   */

/* === K-row === */
#define KANA_KA  6   /* か → ka  */
#define KANA_KI  7   /* き → ki  */
#define KANA_KU  8   /* く → ku  */
#define KANA_KE  9   /* け → ke  */
#define KANA_KO  10  /* こ → ko  */

/* === S-row === */
#define KANA_SA  11  /* さ → sa  */
#define KANA_SI  12  /* し → si  */
#define KANA_SU  13  /* す → su  */
#define KANA_SE  14  /* せ → se  */
#define KANA_SO  15  /* そ → so  */

/* === T-row === */
#define KANA_TA  16  /* た → ta  */
#define KANA_TI  17  /* ち → ti  */
#define KANA_TU  18  /* つ → tu  */
#define KANA_TE  19  /* て → te  */
#define KANA_TO  20  /* と → to  */

/* === N-row === */
#define KANA_NA  21  /* な → na  */
#define KANA_NI  22  /* に → ni  */
#define KANA_NU  23  /* ぬ → nu  */
#define KANA_NE  24  /* ね → ne  */
#define KANA_NO  25  /* の → no  */

/* === H-row === */
#define KANA_HA  26  /* は → ha  */
#define KANA_HI  27  /* ひ → hi  */
#define KANA_HU  28  /* ふ → hu  */
#define KANA_HE  29  /* へ → he  */
#define KANA_HO  30  /* ほ → ho  */

/* === M-row === */
#define KANA_MA  31  /* ま → ma  */
#define KANA_MI  32  /* み → mi  */
#define KANA_MU  33  /* む → mu  */
#define KANA_ME  34  /* め → me  */
#define KANA_MO  35  /* も → mo  */

/* === Y-row === */
#define KANA_YA  36  /* や → ya  */
#define KANA_YU  37  /* ゆ → yu  */
#define KANA_YO  38  /* よ → yo  */

/* === R-row === */
#define KANA_RA  39  /* ら → ra  */
#define KANA_RI  40  /* り → ri  */
#define KANA_RU  41  /* る → ru  */
#define KANA_RE  42  /* れ → re  */
#define KANA_RO  43  /* ろ → ro  */

/* === W + N === */
#define KANA_WA  44  /* わ → wa  */
#define KANA_WO  45  /* を → wo  */
#define KANA_NN  46  /* ん → nn  */

/* === Dakuten (voiced) === */
#define KANA_GA  47  /* が → ga  */
#define KANA_GI  48  /* ぎ → gi  */
#define KANA_GU  49  /* ぐ → gu  */
#define KANA_GE  50  /* げ → ge  */
#define KANA_GO  51  /* ご → go  */
#define KANA_ZA  52  /* ざ → za  */
#define KANA_ZI  53  /* じ → zi  */
#define KANA_ZU  54  /* ず → zu  */
#define KANA_ZE  55  /* ぜ → ze  */
#define KANA_ZO  56  /* ぞ → zo  */
#define KANA_DA  57  /* だ → da  */
#define KANA_DI  58  /* ぢ → di  */
#define KANA_DU  59  /* づ → du  */
#define KANA_DE  60  /* で → de  */
#define KANA_DO  61  /* ど → do  */
#define KANA_BA  62  /* ば → ba  */
#define KANA_BI  63  /* び → bi  */
#define KANA_BU  64  /* ぶ → bu  */
#define KANA_BE  65  /* べ → be  */
#define KANA_BO  66  /* ぼ → bo  */
#define KANA_VU  67  /* ゔ → vu  */

/* === Handakuten (semi-voiced) === */
#define KANA_PA  68  /* ぱ → pa  */
#define KANA_PI  69  /* ぴ → pi  */
#define KANA_PU  70  /* ぷ → pu  */
#define KANA_PE  71  /* ぺ → pe  */
#define KANA_PO  72  /* ぽ → po  */

/* === Small kana (x-prefix) === */
#define KANA_SMALL_A   73  /* ぁ → xa   */
#define KANA_SMALL_I   74  /* ぃ → xi   */
#define KANA_SMALL_U   75  /* ぅ → xu   */
#define KANA_SMALL_E   76  /* ぇ → xe   */
#define KANA_SMALL_O   77  /* ぉ → xo   */
#define KANA_SMALL_YA  78  /* ゃ → xya  */
#define KANA_SMALL_YU  79  /* ゅ → xyu  */
#define KANA_SMALL_YO  80  /* ょ → xyo  */
#define KANA_SMALL_TU  81  /* っ → xtu  */

/* === Punctuation (Japanese) === */
#define KANA_KUTEN     82  /* 。(句点) → .   */
#define KANA_TOUTEN    83  /* 、(読点) → ,   */
#define KANA_COMMA     84  /* ，(コンマ) → , */
#define KANA_PERIOD    85  /* ．(ピリオド) → . */
#define KANA_NAKAGURO  86  /* ・(中点) → /   */
#define KANA_CHOUON    87  /* ー(長音) → -   */

/* === Digits === */
#define KEY_0    88
#define KEY_1    89
#define KEY_2    90
#define KEY_3    91
#define KEY_4    92
#define KEY_5    93
#define KEY_6    94
#define KEY_7    95
#define KEY_8    96
#define KEY_9    97

/* === ASCII symbols (US QWERTY layout) === */
#define KEY_SPACE      98   /*   (space)  */
#define KEY_EXCL       99   /* !          */
#define KEY_DQUOTE     100  /* "          */
#define KEY_HASH       101  /* #          */
#define KEY_DOLLAR     102  /* $          */
#define KEY_PERCENT    103  /* %          */
#define KEY_AMP        104  /* &          */
#define KEY_SQUOTE     105  /* '          */
#define KEY_LPAREN     106  /* (          */
#define KEY_RPAREN     107  /* )          */
#define KEY_ASTER      108  /* *          */
#define KEY_PLUS       109  /* +          */
#define KEY_COMMA      110  /* ,          */
#define KEY_MINUS      111  /* -          */
#define KEY_DOT        112  /* .          */
#define KEY_SLASH      113  /* /          */
#define KEY_COLON      114  /* :          */
#define KEY_SEMI       115  /* ;          */
#define KEY_LT         116  /* <          */
#define KEY_EQ         117  /* =          */
#define KEY_GT         118  /* >          */
#define KEY_QUESTION   119  /* ?          */
#define KEY_AT         120  /* @          */
#define KEY_LBKT       121  /* [          */
#define KEY_BSLH       122  /* \          */
#define KEY_RBKT       123  /* ]          */
#define KEY_CARET      124  /* ^          */
#define KEY_UNDER      125  /* _          */
#define KEY_GRAVE      126  /* `          */
#define KEY_LBRACE     127  /* {          */
#define KEY_PIPE       128  /* |          */
#define KEY_RBRACE     129  /* }          */
#define KEY_TILDE      130  /* ~          */

/* === Yōon (拗音) === */
/* K-row yōon */
#define KANA_KYA 131  /* きゃ → kya */
#define KANA_KYU 132  /* きゅ → kyu */
#define KANA_KYO 133  /* きょ → kyo */
/* S-row yōon */
#define KANA_SYA 134  /* しゃ → sya */
#define KANA_SYU 135  /* しゅ → syu */
#define KANA_SYO 136  /* しょ → syo */
/* T-row yōon */
#define KANA_TYA 137  /* ちゃ → tya */
#define KANA_TYU 138  /* ちゅ → tyu */
#define KANA_TYO 139  /* ちょ → tyo */
/* N-row yōon */
#define KANA_NYA 140  /* にゃ → nya */
#define KANA_NYU 141  /* にゅ → nyu */
#define KANA_NYO 142  /* にょ → nyo */
/* H-row yōon */
#define KANA_HYA 143  /* ひゃ → hya */
#define KANA_HYU 144  /* ひゅ → hyu */
#define KANA_HYO 145  /* ひょ → hyo */
/* M-row yōon */
#define KANA_MYA 146  /* みゃ → mya */
#define KANA_MYU 147  /* みゅ → myu */
#define KANA_MYO 148  /* みょ → myo */
/* R-row yōon */
#define KANA_RYA 149  /* りゃ → rya */
#define KANA_RYU 150  /* りゅ → ryu */
#define KANA_RYO 151  /* りょ → ryo */
/* Voiced yōon (G-row) */
#define KANA_GYA 152  /* ぎゃ → gya */
#define KANA_GYU 153  /* ぎゅ → gyu */
#define KANA_GYO 154  /* ぎょ → gyo */
/* Voiced yōon (Z-row) */
#define KANA_ZYA 155  /* じゃ → zya */
#define KANA_ZYU 156  /* じゅ → zyu */
#define KANA_ZYO 157  /* じょ → zyo */
/* Voiced yōon (D-row) */
#define KANA_DYA 158  /* ぢゃ → dya */
#define KANA_DYU 159  /* ぢゅ → dyu */
#define KANA_DYO 160  /* ぢょ → dyo */
/* Voiced yōon (B-row) */
#define KANA_BYA 161  /* びゃ → bya */
#define KANA_BYU 162  /* びゅ → byu */
#define KANA_BYO 163  /* びょ → byo */
/* Semi-voiced yōon (P-row) */
#define KANA_PYA 164  /* ぴゃ → pya */
#define KANA_PYU 165  /* ぴゅ → pyu */
#define KANA_PYO 166  /* ぴょ → pyo */

/* === Extended kana (外来音) === */
#define KANA_FA  167  /* ふぁ → fa  */
#define KANA_FI  168  /* ふぃ → fi  */
#define KANA_FE  169  /* ふぇ → fe  */
#define KANA_FO  170  /* ふぉ → fo  */
#define KANA_TI2 171  /* てぃ → thi */
#define KANA_DI2 172  /* でぃ → dhi */
#define KANA_TU2 173  /* とぅ → twu */
#define KANA_DU2 174  /* どぅ → dwu */
#define KANA_WI  175  /* うぃ → whi */
#define KANA_WE  176  /* うぇ → whe */
#define KANA_WO2 177  /* うぉ → who */
#define KANA_SYE 178  /* しぇ → sye */
#define KANA_ZYE 179  /* じぇ → zye */
#define KANA_TYE 180  /* ちぇ → tye */
#define KANA_SMALL_WA 181  /* ゎ → xwa */

/* === Combo markers (not output directly; used for combo matching) === */
#define KANA_DAKUTEN    182  /* 濁音マーカー (F/J keys in naginata) */
#define KANA_HANDAKUTEN 183  /* 半濁音マーカー (B/M keys in naginata) */
#define KANA_KOGAKI     184  /* 小書きマーカー (Q key in naginata) */

#define KANA_COUNT 185

/*
 * NC_KEY — embed a raw ZMK keycode in a kana face slot.
 *
 * Kana IDs occupy [0, NC_RAW_KEYCODE_THRESHOLD).  ZMK encoded keycodes
 * always include a nonzero HID usage page in bits 16-23, so their
 * smallest value is 0x00010000 — well above any kana ID.
 *
 * output_kana() checks: value >= threshold → tap the keycode directly
 * instead of performing romaji lookup.
 *
 * The threshold is set at 0x10000 (65536), leaving room for kana IDs
 * to grow from the current 184 up to 65535.
 *
 * Usage:
 *   CHORD_INPUT_BEHAVIOR5(ng_left, NC_KEY(LEFT), NC_KEY(LEFT), ...)
 *   CHORD_INPUT_BEHAVIOR5(ng_vu,   KANA_VU,      NC_KEY(BSPC), ...)
 */
#define NC_RAW_KEYCODE_THRESHOLD 0x10000
#define NC_KEY(k)                (k)
