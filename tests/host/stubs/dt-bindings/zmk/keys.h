/*
 * Host test stub for <dt-bindings/zmk/keys.h>
 * Provides HID encoding macros + USB HID keyboard usage codes used by
 * chordis_engine.c's romaji output table.
 *
 * Encoding (matches ZMK convention):
 *   bit 24    : implicit modifier flag (1 = LSHFT)
 *   bits 16-23: usage page
 *   bits 0-15 : keycode
 */
#pragma once

#include <stdint.h>

#define HID_USAGE_KEY 0x07

/* chordis_engine.c defines its own HID_KEY/HID_KEY_S in terms of these. */
#define ZMK_HID_USAGE(page, code) (((uint32_t)(page) << 16) | ((uint32_t)(code) & 0xFFFF))
#define LS(key)                   ((1u << 24) | (uint32_t)(key))

/* USB HID Keyboard usage IDs (subset used by romaji output) */
#define HID_USAGE_KEY_KEYBOARD_A 0x04
#define HID_USAGE_KEY_KEYBOARD_B 0x05
#define HID_USAGE_KEY_KEYBOARD_C 0x06
#define HID_USAGE_KEY_KEYBOARD_D 0x07
#define HID_USAGE_KEY_KEYBOARD_E 0x08
#define HID_USAGE_KEY_KEYBOARD_F 0x09
#define HID_USAGE_KEY_KEYBOARD_G 0x0A
#define HID_USAGE_KEY_KEYBOARD_H 0x0B
#define HID_USAGE_KEY_KEYBOARD_I 0x0C
#define HID_USAGE_KEY_KEYBOARD_J 0x0D
#define HID_USAGE_KEY_KEYBOARD_K 0x0E
#define HID_USAGE_KEY_KEYBOARD_L 0x0F
#define HID_USAGE_KEY_KEYBOARD_M 0x10
#define HID_USAGE_KEY_KEYBOARD_N 0x11
#define HID_USAGE_KEY_KEYBOARD_O 0x12
#define HID_USAGE_KEY_KEYBOARD_P 0x13
#define HID_USAGE_KEY_KEYBOARD_Q 0x14
#define HID_USAGE_KEY_KEYBOARD_R 0x15
#define HID_USAGE_KEY_KEYBOARD_S 0x16
#define HID_USAGE_KEY_KEYBOARD_T 0x17
#define HID_USAGE_KEY_KEYBOARD_U 0x18
#define HID_USAGE_KEY_KEYBOARD_V 0x19
#define HID_USAGE_KEY_KEYBOARD_W 0x1A
#define HID_USAGE_KEY_KEYBOARD_X 0x1B
#define HID_USAGE_KEY_KEYBOARD_Y 0x1C
#define HID_USAGE_KEY_KEYBOARD_Z 0x1D

#define HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION       0x1E
#define HID_USAGE_KEY_KEYBOARD_2_AND_AT                0x1F
#define HID_USAGE_KEY_KEYBOARD_3_AND_HASH              0x20
#define HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR            0x21
#define HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT           0x22
#define HID_USAGE_KEY_KEYBOARD_6_AND_CARET             0x23
#define HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND         0x24
#define HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK          0x25
#define HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS  0x26
#define HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS 0x27

#define HID_USAGE_KEY_KEYBOARD_SPACEBAR                       0x2C
#define HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE           0x2D
#define HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS                 0x2E
#define HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE    0x2F
#define HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE  0x30
#define HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE             0x31
#define HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON            0x33
#define HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE           0x34
#define HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE         0x35
#define HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN            0x36
#define HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN        0x37
#define HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK        0x38

/* Navigation / editing keys used by NC_KEY tests */
#define HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE          0x2A
#define HID_USAGE_KEY_KEYBOARD_LEFTARROW                 0x50
#define HID_USAGE_KEY_KEYBOARD_RIGHTARROW                0x4F

#define BACKSPACE (ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE))
#define BSPC      (BACKSPACE)
#define LEFT_ARROW (ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_LEFTARROW))
#define LEFT       (LEFT_ARROW)
#define RIGHT_ARROW (ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_RIGHTARROW))
#define RIGHT       (RIGHT_ARROW)

/* Used by auto_off (compiled out via DT stub) but referenced as bare token */
#define LANG2 0
