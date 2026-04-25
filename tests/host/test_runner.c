/*
 * Host test runner for nicola_engine.
 *
 * Builds and runs scenarios in-process. Each scenario:
 *   1. Resets engine + mock state
 *   2. Drives input via chordis_on_char_pressed/released, nicola_on_thumb_*
 *   3. Optionally advances time by firing pending timers
 *   4. Asserts captured event log matches expected
 *
 * Run with `make` (see Makefile in this directory).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "mock_runtime.h"

#include <zmk-chordis/chordis_engine.h>
#include <zmk-chordis/keys.h>
#include <dt-bindings/zmk/keys.h>     /* BSPC, LEFT, RIGHT etc. for NC_KEY tests */

/* ── HID encoding (matches stubs/dt-bindings/zmk/keys.h) ──────── */

#define HID_PAGE_KEY 0x07
#define ENC(code)    (((uint32_t)HID_PAGE_KEY << 16) | ((uint32_t)(code) & 0xFFFF))
#define ENC_S(code)  ((1u << 24) | ENC(code))

/* ASCII → USB HID usage. Only letters and space needed for these tests. */
static uint32_t ascii_to_encoded(char c) {
    if (c >= 'a' && c <= 'z') return ENC(0x04 + (c - 'a'));
    if (c == ' ')             return ENC(0x2C);
    return 0;
}

/* ── Test harness ─────────────────────────────────────────── */

static int         tests_run    = 0;
static int         tests_failed = 0;
static const char *current_test = NULL;
static bool        current_failed = false;

/* Clear engine-internal physical-key state that flush() intentionally
 * preserves (thumb_physically_held). Without this, tests that press thumb
 * without releasing poison the next test's IDLE case (re-enters THUMB_HELD). */
static void test_state_cleanup(void) {
    chordis_engine_flush(0);
    /* Release both sides — noop if not active, but always clears thumb_physically_held */
    struct zmk_behavior_binding_event ev = { .position = 0, .timestamp = 0 };
    chordis_on_thumb_released(CHORDIS_SHIFT_0, ev);
    chordis_on_thumb_released(CHORDIS_SHIFT_1, ev);
    chordis_on_thumb_released(CHORDIS_SHIFT_2, ev);
    chordis_on_thumb_released(CHORDIS_SHIFT_3, ev);
    /* Restore HT globals to engine static defaults so tests that mutate
     * them never leak into subsequent cases. */
    chordis_engine_set_globals(200, 0, 30);
    chordis_engine_set_require_prior_idle_global(0);
}

#define TEST_BEGIN(name) do {                                \
    current_test = (name);                                   \
    current_failed = false;                                  \
    tests_run++;                                             \
    test_state_cleanup();                                    \
    mock_reset();                                            \
} while (0)

#define TEST_END() do {                                      \
    if (!current_failed) fprintf(stderr, "PASS: %s\n", current_test); \
} while (0)

#define FAIL(fmt, ...) do {                                  \
    fprintf(stderr, "FAIL: %s — " fmt "\n", current_test, ##__VA_ARGS__); \
    mock_dump_log();                                         \
    tests_failed++;                                          \
    current_failed = true;                                   \
    return;                                                  \
} while (0)

#define CHECK(cond, fmt, ...) do {                           \
    if (!(cond)) FAIL(fmt, ##__VA_ARGS__);                   \
} while (0)

/* Assert that a single raw keycode tap (press+release) was emitted,
 * starting at mock_event_log[offset]. Returns the next offset. */
static int expect_raw_keycode_at(int offset, uint32_t encoded, const char *label) {
    if (offset + 1 >= mock_event_count) {
        fprintf(stderr, "FAIL: %s — expected raw keycode 0x%08x (%s) but log ended at idx=%d (count=%d)\n",
                current_test, encoded, label, offset, mock_event_count);
        mock_dump_log();
        tests_failed++;
        current_failed = true;
        return offset;
    }
    if (mock_event_log[offset].kind     != MOCK_EVT_KEYCODE_PRESS   ||
        mock_event_log[offset + 1].kind != MOCK_EVT_KEYCODE_RELEASE ||
        mock_event_log[offset].value    != encoded                  ||
        mock_event_log[offset + 1].value != encoded) {
        fprintf(stderr, "FAIL: %s — at idx %d: expected raw keycode 0x%08x (%s), "
                "got kind=%d/0x%08x + kind=%d/0x%08x\n",
                current_test, offset, encoded, label,
                mock_event_log[offset].kind, mock_event_log[offset].value,
                mock_event_log[offset + 1].kind, mock_event_log[offset + 1].value);
        mock_dump_log();
        tests_failed++;
        current_failed = true;
        return offset;
    }
    return offset + 2;
}

/* Convenience: assert exactly one raw keycode tap was emitted (total 2 events). */
static void expect_raw_keycode(uint32_t encoded, const char *label) {
    expect_raw_keycode_at(0, encoded, label);
    if (!current_failed && mock_event_count != 2) {
        fprintf(stderr, "FAIL: %s — expected exactly 2 events for raw keycode %s, got %d\n",
                current_test, label, mock_event_count);
        mock_dump_log();
        tests_failed++;
        current_failed = true;
    }
}

/* Assert the captured keycode events starting at offset match the given
 * romaji string. Each character expands to PRESS+RELEASE of its encoded
 * keycode. Returns the next unread offset. */
static int expect_romaji_at(int offset, const char *expected) {
    int idx = offset;
    for (const char *p = expected; *p; p++) {
        uint32_t enc = ascii_to_encoded(*p);
        if (!enc) {
            fprintf(stderr, "test bug: unmappable ASCII '%c'\n", *p);
            tests_failed++;
            current_failed = true;
            return idx;
        }
        if (idx + 1 >= mock_event_count) {
            fprintf(stderr, "FAIL: %s — expected '%s' but log ended at idx=%d (count=%d)\n",
                    current_test, expected, idx, mock_event_count);
            mock_dump_log();
            tests_failed++;
            current_failed = true;
            return idx;
        }
        if (mock_event_log[idx].kind     != MOCK_EVT_KEYCODE_PRESS   ||
            mock_event_log[idx + 1].kind != MOCK_EVT_KEYCODE_RELEASE ||
            mock_event_log[idx].value    != enc                      ||
            mock_event_log[idx + 1].value != enc) {
            fprintf(stderr, "FAIL: %s — at char '%c' (idx %d): expected press/release of 0x%08x\n",
                    current_test, *p, idx, enc);
            mock_dump_log();
            tests_failed++;
            current_failed = true;
            return idx;
        }
        idx += 2;
    }
    return idx;
}

/* Assert the captured keycode events match the given romaji string.
 * Each character expands to PRESS+RELEASE of its encoded keycode. */
static void expect_romaji(const char *expected) {
    int idx = expect_romaji_at(0, expected);
    if (idx != mock_event_count) {
        fprintf(stderr, "FAIL: %s — trailing events (expected %d, got %d)\n",
                current_test, idx, mock_event_count);
        mock_dump_log();
        tests_failed++;
        current_failed = true;
    }
}

/* ── Input helpers ────────────────────────────────────────── */

static void press_kana(const uint32_t kana[CHORDIS_KANA_SLOTS], uint32_t position, int64_t ts) {
    mock_advance_time(ts);
    struct zmk_behavior_binding_event ev = { .position = position, .timestamp = ts };
    chordis_on_char_pressed(kana, ev, NULL);
}

__attribute__((unused))
static void press_kana_ht(const uint32_t kana[CHORDIS_KANA_SLOTS], uint32_t position,
                          int64_t ts, const struct chordis_hold_config *hold) {
    mock_advance_time(ts);
    struct zmk_behavior_binding_event ev = { .position = position, .timestamp = ts };
    chordis_on_char_pressed(kana, ev, hold);
}

__attribute__((unused))
static void press_char_ht(uint32_t k0, uint32_t position, int64_t ts,
                          const struct chordis_hold_config *hold) {
    uint32_t kana[CHORDIS_KANA_SLOTS] = { k0, KANA_NONE, KANA_NONE, KANA_NONE, KANA_NONE };
    press_kana_ht(kana, position, ts, hold);
}

static void press_char(uint32_t k0, uint32_t position, int64_t ts) {
    uint32_t kana[CHORDIS_KANA_SLOTS] = { k0, KANA_NONE, KANA_NONE, KANA_NONE, KANA_NONE };
    press_kana(kana, position, ts);
}

__attribute__((unused))
static void press_char_with_shift(uint32_t k0, uint32_t kshift0, uint32_t position, int64_t ts) {
    uint32_t kana[CHORDIS_KANA_SLOTS] = { k0, kshift0, KANA_NONE, KANA_NONE, KANA_NONE };
    press_kana(kana, position, ts);
}

static void release_char(uint32_t position, int64_t ts) {
    mock_advance_time(ts);
    struct zmk_behavior_binding_event ev = { .position = position, .timestamp = ts };
    chordis_on_char_released(ev);
}

static struct zmk_behavior_binding dummy_thumb_tap = {
    .behavior_dev = "dummy", .param1 = 0x20, .param2 = 0,  /* 0x20 = fake SPACE marker */
};

static void press_thumb(enum chordis_thumb_side side, uint32_t position, int64_t ts) {
    mock_advance_time(ts);
    struct zmk_behavior_binding_event ev = { .position = position, .timestamp = ts };
    chordis_on_thumb_pressed(side, &dummy_thumb_tap, ev);
}

static void release_thumb(enum chordis_thumb_side side, uint32_t position, int64_t ts) {
    mock_advance_time(ts);
    struct zmk_behavior_binding_event ev = { .position = position, .timestamp = ts };
    chordis_on_thumb_released(side, ev);
}

/* ── Scenarios ────────────────────────────────────────────── */

/*
 * 1. Single char, no thumb, timeout fires → unshifted "si"
 */
static void test_single_char_unshifted(void) {
    TEST_BEGIN("single char unshifted (si)");
    press_char(KANA_SI, 10, 0);
    CHECK(mock_event_count == 0, "no output before timeout");
    CHECK(mock_fire_pending_timer(), "timer should have fired");
    expect_romaji("si");
    TEST_END();
}

/*
 * 2. Char pressed then released before timeout (non-combo candidate) →
 *    release does NOT trigger output; timeout still fires → unshifted "ka"
 */
static void test_char_release_before_timeout(void) {
    TEST_BEGIN("char released before timeout (ka)");
    press_char(KANA_KA, 10, 0);
    release_char(10, 50);
    CHECK(mock_event_count == 0, "no output on release (non-combo candidate)");
    CHECK(mock_fire_pending_timer(), "timer should fire");
    expect_romaji("ka");
    TEST_END();
}

/*
 * 3. Char + thumb (simultaneous) with no 2nd char → timeout → shifted output.
 *    kana[1] (shift-0 face) = KANA_KA, so output "ka".
 */
static void test_char_thumb_timeout(void) {
    TEST_BEGIN("char+thumb, no 2nd char, timeout → shifted");
    uint32_t kana[CHORDIS_KANA_SLOTS] = { KANA_SI, KANA_KA, KANA_NONE, KANA_NONE, KANA_NONE };
    press_kana(kana, 10, 0);
    press_thumb(CHORDIS_SHIFT_0, 30, 20);
    CHECK(mock_event_count == 0, "no output yet");
    CHECK(mock_fire_pending_timer(), "timeout should fire");
    expect_romaji("ka");  /* shifted face of the key */
    TEST_END();
}

/*
 * 4. Thumb alone, no char → thumb release → tap_binding fired (binding invoke).
 */
static void test_thumb_tap_alone(void) {
    TEST_BEGIN("thumb alone → tap binding");
    press_thumb(CHORDIS_SHIFT_0, 30, 0);
    CHECK(mock_event_count == 0, "no output on thumb press");
    release_thumb(CHORDIS_SHIFT_0, 30, 50);
    CHECK(mock_event_count == 2, "expected BIND_PRESS + BIND_RELEASE (got %d)", mock_event_count);
    CHECK(mock_event_log[0].kind == MOCK_EVT_BINDING_PRESS,   "[0] should be BIND_PRESS");
    CHECK(mock_event_log[1].kind == MOCK_EVT_BINDING_RELEASE, "[1] should be BIND_RELEASE");
    TEST_END();
}

/*
 * 5. Thumb held, then char pressed (non-combo-candidate): immediate shifted output.
 */
static void test_thumb_held_then_char(void) {
    TEST_BEGIN("thumb held → char → immediate shifted");
    press_thumb(CHORDIS_SHIFT_0, 30, 0);
    uint32_t kana[CHORDIS_KANA_SLOTS] = { KANA_SI, KANA_KA, KANA_NONE, KANA_NONE, KANA_NONE };
    press_kana(kana, 10, 50);
    /* Since no combos registered, not a combo candidate → immediate output */
    expect_romaji("ka");
    TEST_END();
}

/*
 * 6. 5.4.2 partition: char1 → thumb → char2 with d1 <= d2.
 *    char1+thumb = shifted, char2 starts new CHAR_WAIT.
 *    After char2's timeout fires, char2 = unshifted.
 */
static void test_spec_542_partition_d1_le_d2(void) {
    TEST_BEGIN("5.4.2 partition d1<=d2");
    uint32_t k1[CHORDIS_KANA_SLOTS] = { KANA_SI, KANA_KA, KANA_NONE, KANA_NONE, KANA_NONE };
    press_kana(k1, 10, 0);
    press_thumb(CHORDIS_SHIFT_0, 30, 20);  /* d1 = 20 */
    press_char(KANA_TO, 11, 60);          /* d2 = 40, so d1 <= d2 → first char shifted */
    /* After char2 is buffered in CHAR_WAIT, its timeout eventually fires */
    CHECK(mock_fire_pending_timer(), "timer should fire");
    expect_romaji("kato");  /* "ka" (shifted) + "to" (unshifted) */
    TEST_END();
}

/*
 * 7. 5.4.2 partition: char1 → thumb → char2 with d1 > d2.
 *    char1 unshifted, char2+thumb shifted.
 */
static void test_spec_542_partition_d1_gt_d2(void) {
    TEST_BEGIN("5.4.2 partition d1>d2");
    press_char(KANA_SI, 10, 0);           /* char1 — no shift face, will be unshifted */
    press_thumb(CHORDIS_SHIFT_0, 30, 50);  /* d1 = 50 */
    uint32_t k2[CHORDIS_KANA_SLOTS] = { KANA_TO, KANA_KA, KANA_NONE, KANA_NONE, KANA_NONE };
    press_kana(k2, 11, 60);               /* d2 = 10, d1 > d2 → char2 shifted */
    expect_romaji("sika");
    TEST_END();
}

/*
 * 8. Flush during CHAR_WAIT → buffered char output as unshifted.
 */
static void test_flush_char_wait(void) {
    TEST_BEGIN("flush during CHAR_WAIT");
    press_char(KANA_SI, 10, 0);
    chordis_engine_flush(100);
    expect_romaji("si");
    TEST_END();
}

/*
 * 9. Flush during THUMB_HELD (no char used) → thumb tap binding fired.
 */
static void test_flush_thumb_held_unused(void) {
    TEST_BEGIN("flush during THUMB_HELD (unused)");
    press_thumb(CHORDIS_SHIFT_0, 30, 0);
    chordis_engine_flush(100);
    CHECK(mock_event_count == 2, "expected 2 events (BIND press+release)");
    CHECK(mock_event_log[0].kind == MOCK_EVT_BINDING_PRESS,   "[0] BIND_PRESS");
    CHECK(mock_event_log[1].kind == MOCK_EVT_BINDING_RELEASE, "[1] BIND_RELEASE");
    TEST_END();
}

/*
 * 10. Thumb released after char was output (thumb_used=true) → no tap binding.
 */
static void test_thumb_release_after_use(void) {
    TEST_BEGIN("thumb release after use → no tap binding");
    press_thumb(CHORDIS_SHIFT_0, 30, 0);
    uint32_t kana[CHORDIS_KANA_SLOTS] = { KANA_SI, KANA_KA, KANA_NONE, KANA_NONE, KANA_NONE };
    press_kana(kana, 10, 50);  /* shifted "ka" output */
    int count_before = mock_event_count;
    release_thumb(CHORDIS_SHIFT_0, 30, 100);
    CHECK(mock_event_count == count_before, "no events added on thumb release (thumb_used)");
    TEST_END();
}

/*
 * 11. Releasing a non-active thumb must not clear the active thumb's
 *     physical-held state. After watchdog reset, the next char should
 *     re-enter THUMB_HELD and emit the shifted face immediately.
 */
static void test_non_active_thumb_release_preserves_active_thumb(void) {
    TEST_BEGIN("non-active thumb release preserves active thumb re-entry");
    uint32_t kana[CHORDIS_KANA_SLOTS] = { KANA_SI, KANA_KA, KANA_NONE, KANA_NONE, KANA_NONE };

    press_thumb(CHORDIS_SHIFT_0, 30, 0);
    press_thumb(CHORDIS_SHIFT_1, 31, 10);      /* ignored as second thumb */
    release_thumb(CHORDIS_SHIFT_1, 31, 20);    /* must not clear active thumb */

    CHECK(mock_fire_pending_timer(), "watchdog should fire");
    CHECK(mock_event_count == 0, "watchdog reset should be silent");

    press_kana(kana, 10, 30);
    expect_romaji("ka");
    TEST_END();
}

/*
 * Multi-thumb overlap: active thumb released while another remains held →
 * promote newest remaining thumb. The next char must be emitted using the
 * promoted side's face.
 */
static void test_overlap_promote_newest_on_active_release(void) {
    TEST_BEGIN("overlap: active release promotes newest; next char uses promoted side");
    /* Distinct faces: unshifted, SHIFT_0 = KANA_KA, SHIFT_1 = KANA_TO */
    uint32_t kana[CHORDIS_KANA_SLOTS] = { KANA_SI, KANA_KA, KANA_TO, KANA_NONE, KANA_NONE };

    press_thumb(CHORDIS_SHIFT_0, 30, 0);
    press_thumb(CHORDIS_SHIFT_1, 31, 10);    /* ignored logically; recorded physically */
    release_thumb(CHORDIS_SHIFT_0, 30, 20);  /* active released; promote to SHIFT_1 */
    /* Still in THUMB_HELD — promotion does not cancel the watchdog. */
    press_kana(kana, 10, 30);
    /* Promoted active thumb is SHIFT_1 → shift-1 face = "to" */
    expect_romaji("to");
    TEST_END();
}

/*
 * Multi-thumb overlap: non-active thumb release does not stop the
 * tap-on-final-release semantic. A then B pressed, B released first (non-
 * active, no char pressed), then A released → A is still the sole held
 * thumb, thumb_used=false → tap fires.
 */
static void test_overlap_tap_on_last_release(void) {
    TEST_BEGIN("overlap: non-active released first, then active alone → tap");
    press_thumb(CHORDIS_SHIFT_0, 30, 0);
    press_thumb(CHORDIS_SHIFT_1, 31, 10);
    release_thumb(CHORDIS_SHIFT_1, 31, 20);  /* non-active, physical-only update */
    CHECK(mock_event_count == 0, "no output yet");
    release_thumb(CHORDIS_SHIFT_0, 30, 30);  /* last thumb, unused → tap */
    CHECK(mock_event_count == 2, "expected tap binding (2 events)");
    CHECK(mock_event_log[0].kind == MOCK_EVT_BINDING_PRESS,   "[0] BIND_PRESS");
    CHECK(mock_event_log[1].kind == MOCK_EVT_BINDING_RELEASE, "[1] BIND_RELEASE");
    TEST_END();
}

/*
 * Multi-thumb overlap: promoted thumb inherits thumb_used=true.
 * A pressed, char emitted (thumb_used=true), B pressed, A released
 * (promotion to B with used-state preserved), B released → NO tap.
 */
static void test_overlap_promotion_inherits_used_state(void) {
    TEST_BEGIN("overlap: promotion inherits thumb_used; no tap on final release");
    uint32_t kana[CHORDIS_KANA_SLOTS] = { KANA_SI, KANA_KA, KANA_TO, KANA_NONE, KANA_NONE };

    press_thumb(CHORDIS_SHIFT_0, 30, 0);
    press_kana(kana, 10, 10);               /* shifted "ka", thumb_used=true */
    int n = mock_event_count;                /* "ka" = 4 HID events (press+release per char) */

    press_thumb(CHORDIS_SHIFT_1, 31, 20);    /* second thumb, ignored logically */
    release_thumb(CHORDIS_SHIFT_0, 30, 30);  /* promote to SHIFT_1, used stays true */
    CHECK(mock_event_count == n, "no new events on promotion (had %d, now %d)",
          n, mock_event_count);
    release_thumb(CHORDIS_SHIFT_1, 31, 40);  /* last thumb, but thumb_used=true → no tap */
    CHECK(mock_event_count == n, "no tap fired (thumb_used inherited) (had %d, now %d)",
          n, mock_event_count);
    TEST_END();
}

/*
 * Buffered-state stability (CHAR_THUMB_WAIT): the shift source is frozen at
 * buffered_thumb_side. After entering CHAR_THUMB_WAIT via A, press B then
 * release A so active_thumb is promoted to B. The timeout that resolves the
 * buffered char must still use A's face.
 */
static void test_buffered_char_thumb_wait_stable_across_promotion(void) {
    TEST_BEGIN("CHAR_THUMB_WAIT: buffered side frozen across promotion");
    /* SHIFT_0 face = KANA_KA ("ka"), SHIFT_1 face = KANA_TO ("to") */
    uint32_t kana[CHORDIS_KANA_SLOTS] = { KANA_SI, KANA_KA, KANA_TO, KANA_NONE, KANA_NONE };

    press_kana(kana, 10, 0);                /* CHAR_WAIT */
    press_thumb(CHORDIS_SHIFT_0, 30, 20);    /* enter CHAR_THUMB_WAIT with buffered=SHIFT_0 */
    press_thumb(CHORDIS_SHIFT_1, 31, 30);    /* second thumb, ignored */
    release_thumb(CHORDIS_SHIFT_0, 30, 40);  /* promotion to SHIFT_1; buffered stays SHIFT_0 */
    /* Timeout (char_timeout) resolves buffered as shifted → A face "ka" */
    CHECK(mock_fire_pending_timer(), "buffered timeout should fire");
    expect_romaji("ka");
    TEST_END();
}

/*
 * Buffered-state stability (CHAR_THUMB_WAIT) via thumb release: releasing
 * the active (and only buffered-source) thumb must commit the buffered char
 * using the frozen side, and if another thumb is still held, promote it
 * without re-resolving the buffered interaction.
 */
static void test_buffered_char_thumb_wait_release_active_with_replacement(void) {
    TEST_BEGIN("CHAR_THUMB_WAIT: active release with replacement uses frozen side");
    uint32_t kana[CHORDIS_KANA_SLOTS] = { KANA_SI, KANA_KA, KANA_TO, KANA_NONE, KANA_NONE };

    press_kana(kana, 10, 0);
    press_thumb(CHORDIS_SHIFT_0, 30, 20);    /* CHAR_THUMB_WAIT, buffered=SHIFT_0 */
    press_thumb(CHORDIS_SHIFT_1, 31, 30);
    release_thumb(CHORDIS_SHIFT_0, 30, 40);  /* commit: uses SHIFT_0 face "ka"; then promote */
    expect_romaji("ka");
    /* A subsequent char should use the promoted SHIFT_1 face. */
    press_kana(kana, 11, 50);
    /* promoted active_thumb = SHIFT_1 → "to" */
    /* expect: "ka" then "to" concatenated */
    expect_romaji("kato");
    TEST_END();
}

/*
 * CHAR_THUMB_WAIT d1<=d2 path returns to CHAR_WAIT while the thumb is still
 * physically held. buffered_thumb_side must be cleared so the NEXT thumb
 * press starts a fresh interaction. This is a state-only invariant; we
 * exercise it by confirming that after the path completes, a fresh thumb
 * can pair with the new CHAR_WAIT without surprises.
 */
static void test_char_thumb_wait_d1_le_d2_clears_buffered(void) {
    TEST_BEGIN("CHAR_THUMB_WAIT d1<=d2 return to CHAR_WAIT clears buffered side");
    uint32_t k1[CHORDIS_KANA_SLOTS] = { KANA_SI, KANA_KA, KANA_NONE, KANA_NONE, KANA_NONE };
    press_kana(k1, 10, 0);
    press_thumb(CHORDIS_SHIFT_0, 30, 20);    /* d1=20, CHAR_THUMB_WAIT */
    press_char(KANA_TO, 11, 60);            /* d2=40, d1<=d2 → char1 shifted, CHAR_WAIT */
    /* char1 was emitted as SHIFT_0 face = "ka". char2 buffered as plain. */
    CHECK(mock_fire_pending_timer(), "char2 timeout should fire");
    expect_romaji("kato");
    TEST_END();
}

/*
 * Watchdog re-entry with overlap: both thumbs held when watchdog fires.
 * After reset, the next char must use the newest held thumb (SHIFT_1).
 */
static void test_watchdog_reentry_picks_newest_held(void) {
    TEST_BEGIN("watchdog re-entry picks newest held thumb");
    uint32_t kana[CHORDIS_KANA_SLOTS] = { KANA_SI, KANA_KA, KANA_TO, KANA_NONE, KANA_NONE };

    press_thumb(CHORDIS_SHIFT_0, 30, 0);
    press_thumb(CHORDIS_SHIFT_1, 31, 10);    /* newer */
    CHECK(mock_fire_pending_timer(), "watchdog should fire");
    CHECK(mock_event_count == 0, "watchdog silent");
    press_kana(kana, 10, 30);
    /* newest held is SHIFT_1 → shift-1 face "to" */
    expect_romaji("to");
    TEST_END();
}

/*
 * 11. 5.5.2 sequential typing detection: very short overlap → sequential.
 *     char pressed long, thumb just tapped briefly → unshifted, thumb enters THUMB_HELD.
 *     Then flush to observe thumb_used=false → tap binding.
 */
static void test_spec_552_sequential_short_overlap(void) {
    TEST_BEGIN("5.5.2 sequential (short overlap)");
    press_char(KANA_SI, 10, 0);
    press_thumb(CHORDIS_SHIFT_0, 30, 100);  /* t1 = 100 */
    release_char(10, 105);                 /* t2 = 5 — below min_overlap (20) → sequential */
    /* Expect unshifted "si" output */
    expect_romaji("si");
    TEST_END();
}

/* ── Combo scenarios ─────────────────────────────────────── *
 * These require a char combo to be registered first. Combos are registered
 * once in main() before any test runs, so is_combo_candidate() returns true
 * for participating kana IDs across all tests. This changes some earlier
 * behavior (combo candidates use release-driven output) — tests that care
 * must use non-combo-candidate kana for their inputs.
 *
 * Registered for tests: KANA_SI + KANA_YO → KANA_SYO (fictional yō-on)
 */
static void register_test_combos(void) {
    /* We use fictional combos with non-overlapping kana to avoid polluting
     * other tests. KANA_HI + KANA_YU → KANA_HYU (a real yō-on mapping). */
    chordis_register_char_combo(KANA_HI, KANA_YU, KANA_HYU);
    /* Chain combo: HYU + DAKUTEN → BYU (びゅ). Used by D-3b to verify
     * 3-key chain resolves while one of the participants is HT-UNDECIDED. */
    chordis_register_char_combo(KANA_HYU, KANA_DAKUTEN, KANA_BYU);
    /* Kogaki combos: KOGAKI + vowel/ya/yu/yo/tu/wa → small kana */
    chordis_register_char_combo(KANA_KOGAKI, KANA_YO, KANA_SMALL_YO);
    chordis_register_char_combo(KANA_KOGAKI, KANA_A,  KANA_SMALL_A);
    chordis_register_char_combo(KANA_KOGAKI, KANA_TU, KANA_SMALL_TU);
}

/*
 * 12. Char + char combo, released → combo kana output.
 *     Press HI, press YU, release YU → combo resolves as HYU ("hyu").
 */
static void test_char_combo_basic(void) {
    TEST_BEGIN("char combo: hi+yu → hyu");
    press_char(KANA_HI, 10, 0);
    press_char(KANA_YU, 11, 30);
    release_char(11, 60);
    /* Combo resolution on release — should output "hyu" */
    expect_romaji("hyu");
    release_char(10, 80);
    TEST_END();
}

/*
 * 13. Combo candidate alone (HI), then timeout → unshifted "hi".
 *     Combo candidates use release-driven output when combos registered,
 *     so we release to trigger the output path.
 */
static void test_combo_candidate_alone(void) {
    TEST_BEGIN("combo candidate alone → unshifted on release");
    press_char(KANA_HI, 10, 0);
    release_char(10, 50);
    /* Release of a combo candidate in CHAR_WAIT triggers immediate unshifted */
    expect_romaji("hi");
    TEST_END();
}

/*
 * 14. Two combo candidates that don't form a combo → both unshifted.
 *     HI + KA (KA not registered as combo partner) → no combo match.
 *     At release of first key, combo_pending is resolved — since no
 *     combo exists, both are output separately.
 */
static void test_combo_no_match(void) {
    TEST_BEGIN("combo candidates, no match → separate output");
    press_char(KANA_HI, 10, 0);
    press_char(KANA_KA, 11, 30);  /* KA is not a combo candidate — no pending */
    /* HI is buffered, KA arrives. Engine sees KA is not a combo candidate.
     * Actually the path: in CHAR_WAIT, press_char enters combo lookup.
     * lookup_any_combo(HI, KA) = NONE. has_potential_chain also false.
     * → flush HI as unshifted, buffer KA. */
    /* After second press, HI should have been flushed. */
    /* First char (HI) is now output as unshifted. KA is in CHAR_WAIT. */
    release_char(11, 60);   /* KA is not combo candidate, release doesn't output */
    CHECK(mock_fire_pending_timer(), "timer should fire for KA");
    expect_romaji("hika");
    TEST_END();
}

/* ── Hold-tap scenarios ──────────────────────────────────── */

/* A distinguishable binding for hold action — verified by binding events
 * in the mock log. param1 is opaque to the engine; tests just check that
 * BIND_PRESS / BIND_RELEASE with this value appear at the right times. */
#define HOLD_BINDING_PARAM 0xCAFE0001

/* Base-layer binding param used by D-1.5 passthrough tests. */
#define BASE_BINDING_PARAM 0xBA5E0001

static const struct chordis_hold_config hold_lshft_200 = {
    .binding = { .behavior_dev = "hold_dummy",
                 .param1 = HOLD_BINDING_PARAM, .param2 = 0 },
    .tapping_term_ms = 200,
    .quick_tap_ms    = 0,
};

static const struct chordis_hold_config hold_lshft_hapr_10 = {
    .binding = { .behavior_dev = "hold_dummy",
                 .param1 = HOLD_BINDING_PARAM, .param2 = 0 },
    .tapping_term_ms = 200,
    .quick_tap_ms    = 0,
    .hold_after_partner_release_ms = 10,
};

/*
 * 15. Hold-tap key tapped (released before tapping_term) → kana output, no hold.
 */
static void test_holdtap_tap(void) {
    TEST_BEGIN("hold-tap: tap (release < tapping_term) → kana");
    /* Use KANA_KA which is not a combo candidate. */
    press_char_ht(KANA_KA, 10, 0, &hold_lshft_200);
    release_char(10, 50);
    /* Release path should output unshifted "ka" and free the tracker. */
    expect_romaji("ka");
    CHECK(mock_event_count == 4, "expected 4 events (k+a press/release), got %d", mock_event_count);
    CHECK(!chordis_is_hold_active(), "no hold active after tap");
    TEST_END();
}

/*
 * 16. Hold-tap key held past tapping_term → tapping_term_timer fires →
 *     hold_binding press; release → hold_binding release; no kana.
 */
static void test_holdtap_hold(void) {
    TEST_BEGIN("hold-tap: hold past tapping_term → mod activates");
    press_char_ht(KANA_KA, 10, 0, &hold_lshft_200);

    /* Fire char_timeout (delay 100). It should extend (hold in flight). */
    CHECK(mock_fire_pending_timer(), "char_timeout should fire");
    CHECK(mock_event_count == 0, "no output yet (extended)");

    /* Fire tapping_term_timer (delay 200). Should resolve to TRK_HOLD and
     * invoke hold_binding press. */
    CHECK(mock_fire_pending_timer(), "tapping_term_timer should fire");
    CHECK(mock_event_count == 1, "expected 1 event (BIND_PRESS), got %d", mock_event_count);
    CHECK(mock_event_log[0].kind  == MOCK_EVT_BINDING_PRESS, "[0] BIND_PRESS");
    CHECK(mock_event_log[0].value == HOLD_BINDING_PARAM,     "[0] hold binding");

    /* Verify is_hold_active returns true while held */
    CHECK(chordis_is_hold_active(), "hold should be active");

    /* Now release the key — should fire hold_binding release. */
    release_char(10, 500);
    CHECK(mock_event_count == 2, "expected 2 events after release, got %d", mock_event_count);
    CHECK(mock_event_log[1].kind  == MOCK_EVT_BINDING_RELEASE, "[1] BIND_RELEASE");
    CHECK(mock_event_log[1].value == HOLD_BINDING_PARAM,       "[1] hold binding");

    CHECK(!chordis_is_hold_active(), "hold should be cleared after release");
    TEST_END();
}

/*
 * 17. Hold-tap on a combo-candidate key, held past tapping_term → mod fires.
 *     This verifies hold-tap works even when the kana is also a combo candidate
 *     (the timeout extension path covers both reasons).
 */
static void test_holdtap_combo_candidate_hold(void) {
    TEST_BEGIN("hold-tap on combo-candidate, held → mod activates");
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_200);  /* HI is a combo candidate */
    CHECK(mock_fire_pending_timer(), "char_timeout fires");
    CHECK(mock_event_count == 0, "no output yet");
    CHECK(mock_fire_pending_timer(), "tapping_term fires");
    CHECK(mock_event_count == 1, "BIND_PRESS expected");
    CHECK(mock_event_log[0].kind == MOCK_EVT_BINDING_PRESS, "BIND_PRESS");
    release_char(10, 500);
    CHECK(mock_event_count == 2, "BIND_RELEASE expected");
    CHECK(mock_event_log[1].kind == MOCK_EVT_BINDING_RELEASE, "BIND_RELEASE");
    TEST_END();
}

/*
 * 18. Slice D-1: HT + combo partner, both released within hold-after-partner-release window
 *     → combo "hyu" wins (HT release cancels hold-commit, eager combo resolve).
 *
 *     Sequence: press HI[HT], press YU, release YU, release HI.
 *     YU release schedules hold-commit (HT undecided + plain release).
 *     HI release runs combo_pending path: find_ht_undecided excludes
 *     released, YU is plain → no HT undecided remaining → combo resolves
 *     immediately. tracker_free(YU) cancels its hold_commit_timer.
 */
static void test_holdtap_combo_partner_combo_wins(void) {
    TEST_BEGIN("D-1: HT + partner, both released → combo");
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_200);
    press_char(KANA_YU, 11, 30);
    release_char(11, 50);   /* plain released first → hold-commit scheduled */
    CHECK(mock_event_count == 0, "no output yet (hold-commit pending)");
    release_char(10, 60);   /* HT released → eager combo resolve */
    expect_romaji("hyu");
    CHECK(!chordis_is_hold_active(), "no hold activated");
    TEST_END();
}

/*
 * 19. Slice D-1: HT + combo partner, only plain released, hold-commit fires
 *     while HT still held → HT → HOLD, plain dropped (passthrough placeholder).
 *
 *     Verifies BIND_PRESS for hold binding fires from hold_commit_handler.
 *     Verifies BIND_RELEASE on HT release.
 */
static void test_holdtap_combo_partner_hold_commit_to_hold(void) {
    TEST_BEGIN("D-1: HT + partner, hold-commit fires → HOLD + base passthrough");
    /* D-1.5: register base-layer binding for plain B's position so hold-commit
     * can emit the passthrough press+release. */
    mock_set_keymap_binding(11, "kp_dummy", BASE_BINDING_PARAM);
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_200);
    press_char(KANA_YU, 11, 30);
    release_char(11, 180);  /* plain released near boundary → grace extends HOLD */
    CHECK(mock_event_count == 0, "no output yet");

    CHECK(mock_fire_pending_timer(), "tapping-term should defer first");
    CHECK(mock_event_count == 0, "still no output after defer");
    CHECK(mock_fire_pending_timer(), "hold-commit should fire");
    /* Expect: HT BIND_PRESS, base BIND_PRESS, base BIND_RELEASE */
    CHECK(mock_event_count == 3, "expected 3 events, got %d", mock_event_count);
    CHECK(mock_event_log[0].kind  == MOCK_EVT_BINDING_PRESS, "[0] BIND_PRESS");
    CHECK(mock_event_log[0].value == HOLD_BINDING_PARAM,     "[0] hold binding");
    CHECK(mock_event_log[0].timestamp == 210,                "[0] hold timestamp");
    CHECK(mock_event_log[1].kind  == MOCK_EVT_BINDING_PRESS,   "[1] base BIND_PRESS");
    CHECK(mock_event_log[1].value == BASE_BINDING_PARAM,       "[1] base binding");
    CHECK(mock_event_log[2].kind  == MOCK_EVT_BINDING_RELEASE, "[2] base BIND_RELEASE");
    CHECK(mock_event_log[2].value == BASE_BINDING_PARAM,       "[2] base binding");
    CHECK(chordis_is_hold_active(), "hold should be active after hold-commit");

    /* Release HT — should fire BIND_RELEASE and return to IDLE. */
    release_char(10, 500);
    CHECK(mock_event_count == 4, "expected 4 events, got %d", mock_event_count);
    CHECK(mock_event_log[3].kind  == MOCK_EVT_BINDING_RELEASE, "[3] BIND_RELEASE");
    CHECK(mock_event_log[3].value == HOLD_BINDING_PARAM,       "[3] hold binding");
    CHECK(!chordis_is_hold_active(), "hold cleared after release");
    TEST_END();
}

/*
 * 20. Slice D-1: HT released before partner → combo resolves immediately.
 *     This is the existing eager combo path; hold-after-partner-release window doesn't apply
 *     because the released tracker IS the HT.
 */
static void test_holdtap_combo_partner_ht_first(void) {
    TEST_BEGIN("D-1: HT released first → combo immediate");
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_200);
    press_char(KANA_YU, 11, 30);
    release_char(10, 50);  /* HT released first → combo resolve immediately */
    expect_romaji("hyu");
    CHECK(!chordis_is_hold_active(), "no hold activated");
    release_char(11, 80);  /* clean-up release on plain (already freed) */
    TEST_END();
}

/* Second hold config with a distinct binding param so multi-mod tests can
 * verify that BOTH HTs were resolved (not just one twice). */
#define HOLD_BINDING_PARAM_2 0xCAFE0002

static const struct chordis_hold_config hold_lctl_200 = {
    .binding = { .behavior_dev = "hold_dummy",
                 .param1 = HOLD_BINDING_PARAM_2, .param2 = 0 },
    .tapping_term_ms = 200,
    .quick_tap_ms    = 0,
};

static const struct chordis_hold_config hold_lctl_hapr_70 = {
    .binding = { .behavior_dev = "hold_dummy",
                 .param1 = HOLD_BINDING_PARAM_2, .param2 = 0 },
    .tapping_term_ms = 200,
    .quick_tap_ms    = 0,
    .hold_after_partner_release_ms = 70,
};

/*
 * 21. Slice D-2: two HT-UNDECIDED keys coexist, plain combo partner B
 *     released → hold-commit resolves BOTH HTs to HOLD (multi-mod, e.g. Cmd+Shift).
 *
 *     HI[HT] → KA[HT] (no combo with HI) → YU[plain combo partner of HI]
 *     → release YU → hold-commit fires → both HTs become HOLD.
 */
static void test_holdtap_multi_mod_hold_commit(void) {
    TEST_BEGIN("D-2: two HT held, hold-commit resolves both → HOLD + base passthrough");
    /* D-1.5: register base-layer binding for plain B's position. */
    mock_set_keymap_binding(12, "kp_dummy", BASE_BINDING_PARAM);
    press_char_ht(KANA_HI, 10,  0, &hold_lshft_200);  /* HT1 */
    press_char_ht(KANA_KA, 11,  5, &hold_lctl_200);   /* HT2 — KA non-combo */
    press_char(KANA_YU, 12, 10);                       /* plain combo partner of HI */
    release_char(12, 195);                             /* hold-commit after both tapping terms */
    CHECK(mock_event_count == 0, "no output yet");

    CHECK(mock_fire_pending_timer(), "first tapping-term should defer");
    CHECK(mock_event_count == 0, "still no output after first defer");
    CHECK(mock_fire_pending_timer(), "second tapping-term should defer");
    CHECK(mock_event_count == 0, "still no output after second defer");
    CHECK(mock_fire_pending_timer(), "hold-commit should fire");

    /* Expect: 2 HT BIND_PRESS + 1 base BIND_PRESS + 1 base BIND_RELEASE = 4 events. */
    int ht_presses = 0;
    int base_presses = 0;
    int base_releases = 0;
    uint32_t ht_seen[2] = { 0, 0 };
    for (int i = 0; i < mock_event_count; i++) {
        struct mock_event *e = &mock_event_log[i];
        if (e->kind == MOCK_EVT_BINDING_PRESS && e->value == BASE_BINDING_PARAM) {
            base_presses++;
        } else if (e->kind == MOCK_EVT_BINDING_RELEASE && e->value == BASE_BINDING_PARAM) {
            base_releases++;
        } else if (e->kind == MOCK_EVT_BINDING_PRESS) {
            if (ht_presses < 2) ht_seen[ht_presses] = e->value;
            ht_presses++;
        }
    }
    CHECK(ht_presses == 2, "expected 2 HT BIND_PRESS, got %d", ht_presses);
    CHECK(mock_event_log[0].timestamp == 225, "hold-commit timestamp should be 225");
    CHECK(base_presses == 1, "expected 1 base BIND_PRESS, got %d", base_presses);
    CHECK(base_releases == 1, "expected 1 base BIND_RELEASE, got %d", base_releases);
    bool got_p1 = (ht_seen[0] == HOLD_BINDING_PARAM   || ht_seen[1] == HOLD_BINDING_PARAM);
    bool got_p2 = (ht_seen[0] == HOLD_BINDING_PARAM_2 || ht_seen[1] == HOLD_BINDING_PARAM_2);
    CHECK(got_p1 && got_p2, "both hold bindings should fire");
    CHECK(chordis_is_hold_active(), "hold should be active");

    /* Release both HTs — each fires BIND_RELEASE, then state returns IDLE. */
    release_char(11, 600);
    release_char(10, 700);
    CHECK(!chordis_is_hold_active(), "no hold after both released");
    TEST_END();
}

/*
 * 22. Slice D-2: two HT-UNDECIDED keys, both released as taps (within
 *     tapping_term, no plain partner) → both kana output, no mod.
 *
 *     HI[HT] → KA[HT] (no combo) → release HI → release KA.
 *     Press path: KA arriving doesn't flush HI (HI is HT-UNDECIDED).
 *     HI release outputs "hi" but KA tracker remains; tracker_count > 0
 *     so state stays CHAR_WAIT. KA release outputs "ka".
 */
static void test_holdtap_multi_mod_both_tap(void) {
    TEST_BEGIN("D-2: two HT, both tapped → both kana, no mod");
    press_char_ht(KANA_HI, 10, 0,  &hold_lshft_200);
    press_char_ht(KANA_KA, 11, 5,  &hold_lctl_200);
    release_char(10, 30);  /* HI released first as tap */
    release_char(11, 50);  /* KA released as tap */
    expect_romaji("hika");
    CHECK(!chordis_is_hold_active(), "no hold");
    TEST_END();
}

/*
 * 23. Slice D-1.5: hold-commit with no base-layer binding registered for plain B
 *     should still resolve HT → HOLD without crashing or emitting passthrough.
 */
static void test_holdtap_hold_commit_no_base_binding(void) {
    TEST_BEGIN("D-1.5: hold-commit with no base binding → HT only");
    /* Note: no mock_set_keymap_binding() — keymap is empty after mock_reset */
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_200);
    press_char(KANA_YU, 11, 30);
    release_char(11, 180);
    CHECK(mock_fire_pending_timer(), "tapping-term should defer first");
    CHECK(mock_event_count == 0, "still no output");
    CHECK(mock_fire_pending_timer(), "hold-commit should fire");
    /* Only the HT BIND_PRESS — no base passthrough */
    CHECK(mock_event_count == 1, "expected 1 event, got %d", mock_event_count);
    CHECK(mock_event_log[0].kind  == MOCK_EVT_BINDING_PRESS, "[0] BIND_PRESS");
    CHECK(mock_event_log[0].value == HOLD_BINDING_PARAM,     "[0] hold binding");
    CHECK(chordis_is_hold_active(), "hold should be active");

    release_char(10, 500);
    CHECK(!chordis_is_hold_active(), "hold cleared");
    TEST_END();
}

/*
 * 24. Slice D-3a: HT released after plain partner B was released but BEFORE
 *     the hold-commit timer fired. The hold-after-partner-release window should let HT escape
 *     to combo resolution rather than HOLD. Verifies tracker_free cancels
 *     the dangling hold-commit timer (no spurious hold-commit fire after combo).
 */
static void test_holdtap_ht_release_hold_commit_recovery(void) {
    TEST_BEGIN("D-3a: HT released within hold-commit gap → combo");
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_200);
    press_char(KANA_YU, 11, 30);
    release_char(11, 50);   /* plain released → hold-commit scheduled (fires at 130) */
    CHECK(mock_event_count == 0, "no output yet (hold-commit pending)");
    release_char(10, 70);   /* HT released BEFORE hold-commit fires → combo */
    expect_romaji("hyu");
    CHECK(!chordis_is_hold_active(), "no hold activated");
    /* The hold-commit timer should have been cancelled by tracker_free.
     * Firing pending should be a no-op (return false). */
    CHECK(!mock_fire_pending_timer(), "no stray hold-commit timer should remain");
    TEST_END();
}

/*
 * Globals (cdis_config fallback chain):
 *
 *   per-key tapping_term_ms == 0  → inherit timing.default_tapping_term_ms
 *   per-key quick_tap_ms    == 0  → inherit timing.default_quick_tap_ms
 *   per-key hold_after_partner_release_ms == 0
 *       → inherit timing.hold_after_partner_release_ms
 */

/* Hold config with sentinel-0 timing fields → engine substitutes globals. */
static const struct chordis_hold_config hold_lshft_inherit = {
    .binding = { .behavior_dev = "hold_dummy",
                 .param1 = HOLD_BINDING_PARAM, .param2 = 0 },
    .tapping_term_ms = 0,  /* inherit */
    .quick_tap_ms    = 0,  /* inherit */
};

/* Per-key inherits → uses default_tapping_term_ms (150). */
static void test_holdtap_globals_inherit_tapping_term(void) {
    TEST_BEGIN("globals: per-key tt=0 → inherits default_tapping_term_ms");
    chordis_engine_set_globals(150, CHORDIS_KEEP, CHORDIS_KEEP);
    press_char_ht(KANA_KA, 10, 0, &hold_lshft_inherit);
    CHECK(mock_fire_pending_timer(), "char_timeout should fire first");
    CHECK(!chordis_is_hold_active(), "no hold before inherited tapping term");
    CHECK(mock_fire_pending_timer(), "inherited tapping_term timer should fire");
    CHECK(chordis_is_hold_active(), "HOLD active via inherited 150ms tapping term");
    release_char(10, 300);
    TEST_END();
}

/* With no DT-provided global override, the engine fallback remains 200ms. */
static void test_holdtap_globals_fallback_tapping_term_200(void) {
    TEST_BEGIN("globals: fallback default_tapping_term_ms is 200");
    press_char_ht(KANA_KA, 10, 0, &hold_lshft_inherit);
    CHECK(mock_fire_pending_timer(), "char_timeout should fire first");
    CHECK(!chordis_is_hold_active(), "no hold before fallback tapping term");
    CHECK(mock_fire_pending_timer(), "fallback tapping_term timer should fire");
    CHECK(chordis_is_hold_active(), "HOLD active via fallback 200ms tapping term");
    release_char(10, 300);
    TEST_END();
}

/* Per-key explicit value > 0 → wins over global. */
static void test_holdtap_globals_per_key_overrides(void) {
    TEST_BEGIN("globals: per-key tt explicit overrides global");
    chordis_engine_set_globals(50 /* short global */, CHORDIS_KEEP, CHORDIS_KEEP);
    /* hold_lshft_200 has tapping_term_ms=200 explicitly — must beat the
     * 50ms global. The lone-hold path fires char_timeout first (extends),
     * then tapping_term. */
    press_char_ht(KANA_KA, 10, 0, &hold_lshft_200);
    CHECK(mock_fire_pending_timer(), "char_timeout should fire (extend)");
    CHECK(mock_event_count == 0, "no output yet");
    CHECK(mock_fire_pending_timer(), "tapping_term timer should fire");
    CHECK(chordis_is_hold_active(), "HOLD active (per-key 200 won)");
    release_char(10, 300);
    TEST_END();
}

/* Global hold_after_partner_release_ms is consumed when scheduling the
 * hold-commit timer. Verify a non-default global value flows through. */
static void test_holdtap_globals_hold_after_partner_release(void) {
    TEST_BEGIN("globals: hold_after_partner_release_ms drives commit timer");
    chordis_engine_set_globals(CHORDIS_KEEP, CHORDIS_KEEP, 40 /* short */);
    mock_set_keymap_binding(11, "kp_dummy", BASE_BINDING_PARAM);
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_200);
    press_char(KANA_YU, 11, 30);
    release_char(11, 180);  /* near tapping-term: global grace extends to 220 */
    CHECK(mock_fire_pending_timer(), "tapping-term should defer first");
    CHECK(mock_event_count == 0, "still no output after defer");
    CHECK(mock_fire_pending_timer(), "hold-commit timer should fire");
    CHECK(mock_event_log[0].timestamp == 220,
          "hold commit timestamp should be partner release + global grace");
    CHECK(chordis_is_hold_active(), "HOLD activated via short global window");
    release_char(10, 500);
    TEST_END();
}

/* If the partner is released early, hold-after-partner-release must not make
 * HOLD commit before the HT key's tapping-term. The HT can still release
 * after the small grace window and before tapping-term to produce combo. */
static void test_holdtap_hold_after_partner_release_does_not_preempt_tapping_term(void) {
    TEST_BEGIN("globals: hold-after-partner-release never preempts tapping-term");
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_hapr_10);
    press_char(KANA_YU, 11, 30);
    release_char(11, 50);
    release_char(10, 100);
    expect_romaji("hyu");
    CHECK(!chordis_is_hold_active(), "no hold activated");
    TEST_END();
}

/* Near tapping-term, hold-after-partner-release may extend HOLD commit a
 * little so release-order jitter still resolves as combo. */
static void test_holdtap_hold_after_partner_release_extends_near_tapping_term(void) {
    TEST_BEGIN("globals: hold-after-partner-release extends near tapping-term");
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_hapr_10);
    press_char(KANA_YU, 11, 30);
    release_char(11, 195);
    release_char(10, 202);
    expect_romaji("hyu");
    CHECK(!chordis_is_hold_active(), "no hold activated");
    TEST_END();
}

/* Per-key hold_after_partner_release_ms beats the global default for the HT
 * that is pending on this plain partner. */
static void test_holdtap_per_key_hold_after_partner_release(void) {
    TEST_BEGIN("globals: per-key hold_after_partner_release_ms overrides global");
    chordis_engine_set_globals(CHORDIS_KEEP, CHORDIS_KEEP, 50 /* global */);
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_hapr_10);
    press_char(KANA_YU, 11, 30);
    release_char(11, 195);
    CHECK(mock_fire_pending_timer(), "tapping-term should defer first");
    CHECK(mock_event_count == 0, "still no output after defer");
    CHECK(mock_fire_pending_timer(), "hold-commit should fire at per-key deadline");
    CHECK(mock_event_log[0].timestamp == 205,
          "per-key grace should commit at 205ms");
    TEST_END();
}

/* When several HTs can be resolved by the same plain partner release, use the
 * longest per-key grace so a short-window HT cannot truncate a longer one. */
static void test_holdtap_per_key_hold_after_partner_release_max(void) {
    TEST_BEGIN("globals: multi-HT hold_after_partner_release_ms uses max");
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_hapr_10);
    press_char_ht(KANA_KA, 11, 5, &hold_lctl_hapr_70);
    press_char(KANA_YU, 12, 10);
    release_char(12, 195);
    CHECK(mock_fire_pending_timer(), "first tapping-term should defer");
    CHECK(mock_fire_pending_timer(), "second tapping-term should defer");
    CHECK(mock_fire_pending_timer(), "hold-commit should finally fire");
    CHECK(mock_event_count == 2, "expected 2 HT binds without base binding, got %d",
          mock_event_count);
    CHECK(mock_event_log[0].timestamp == 265,
          "max effective deadline should commit at 265ms");
    TEST_END();
}

/* Per-key quick_tap_ms = 0 inherits a non-zero global → repeat suppression
 * applies. */
static void test_holdtap_globals_inherit_quick_tap(void) {
    TEST_BEGIN("globals: per-key qt=0 inherits default_quick_tap_ms");
    chordis_engine_set_globals(CHORDIS_KEEP, 150 /* qt window */, CHORDIS_KEEP);
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_inherit);
    release_char(10, 20);
    expect_romaji("hi");

    /* Re-press 60ms after release (< inherited qt=150). Quick-tap
     * suppression must drop has_hold even past tapping_term. */
    press_char_ht(KANA_HI, 10, 80, &hold_lshft_inherit);
    for (int i = 0; i < 4 && mock_fire_pending_timer(); i++) {
        if (chordis_is_hold_active()) break;
    }
    CHECK(!chordis_is_hold_active(), "inherited qt must suppress hold");
    release_char(10, 350);
    TEST_END();
}

/*
 * require-prior-idle-ms: rolling-input gate.
 *
 *   When configured (>0), an HT key pressed within N ms after any key's
 *   release loses its hold capability and resolves as plain tap.
 *   Independent from the prior-plain blocker (which only fires while a
 *   plain key is still physically held).
 */

/* HT config that opts into the prior-idle gate explicitly (per-key 100). */
static const struct chordis_hold_config hold_lshft_prior_idle_100 = {
    .binding = { .behavior_dev = "hold_dummy",
                 .param1 = HOLD_BINDING_PARAM, .param2 = 0 },
    .tapping_term_ms       = 200,
    .quick_tap_ms          = 0,
    .require_prior_idle_ms = 100,
};

/* Helper: press + release a non-combo char, fire its char_timeout to
 * emit, ending in IDLE with last_key_release_ts = release_ts. */
static void prior_idle_seed_release(int64_t release_ts) {
    press_char(KANA_KA, 9, 0);
    release_char(9, release_ts);
    CHECK(mock_fire_pending_timer(), "char_timeout fires");
    expect_romaji("ka");
}

/* Press after a sufficiently long idle gap → HOLD allowed. */
static void test_holdtap_prior_idle_gap_satisfied(void) {
    TEST_BEGIN("prior-idle: gap >= window → HOLD allowed");
    prior_idle_seed_release(10);
    /* Press HT 300ms later — gap 290 ms >> 100 ms gate. */
    press_char_ht(KANA_KE, 10, 300, &hold_lshft_prior_idle_100);
    CHECK(mock_fire_pending_timer(), "char_timeout fires (extends)");
    CHECK(mock_fire_pending_timer(), "tapping_term fires");
    CHECK(chordis_is_hold_active(), "HOLD activated (gap satisfied)");
    release_char(10, 800);
    TEST_END();
}

/* Press within the gate → forced tap. */
static void test_holdtap_prior_idle_gap_violated(void) {
    TEST_BEGIN("prior-idle: gap < window → forced tap");
    prior_idle_seed_release(50);
    /* Gap 30 ms < 100 ms gate. */
    press_char_ht(KANA_KE, 10, 80, &hold_lshft_prior_idle_100);
    for (int i = 0; i < 4 && mock_fire_pending_timer(); i++) {
        if (chordis_is_hold_active()) break;
    }
    CHECK(!chordis_is_hold_active(), "prior-idle must suppress lone HOLD");
    release_char(10, 500);
    TEST_END();
}

/* The idle gate is based on the most recent release globally, including the
 * same physical key. This is distinct from quick-tap, which is position
 * history and can be disabled independently. */
static void test_holdtap_prior_idle_same_key_release_counts(void) {
    TEST_BEGIN("prior-idle: same-key prior release counts");
    press_char_ht(KANA_KE, 10, 0, &hold_lshft_prior_idle_100);
    release_char(10, 20);
    CHECK(mock_fire_pending_timer(), "char_timeout fires");
    expect_romaji("ke");

    /* Gap 60 ms < 100 ms gate. quick_tap_ms is 0, so only prior-idle can
     * suppress HOLD here. */
    press_char_ht(KANA_KE, 10, 80, &hold_lshft_prior_idle_100);
    for (int i = 0; i < 4 && mock_fire_pending_timer(); i++) {
        if (chordis_is_hold_active()) break;
    }
    CHECK(!chordis_is_hold_active(), "same-key prior release must suppress HOLD");
    release_char(10, 500);
    TEST_END();
}

/* Per-key value (50) beats global (200) and allows HOLD at 80ms gap. */
static void test_holdtap_prior_idle_per_key_overrides(void) {
    TEST_BEGIN("prior-idle: per-key explicit value beats global");
    chordis_engine_set_require_prior_idle_global(200 /* large global */);
    static const struct chordis_hold_config hold_pi_50 = {
        .binding = { .behavior_dev = "hold_dummy",
                     .param1 = HOLD_BINDING_PARAM, .param2 = 0 },
        .tapping_term_ms       = 200,
        .quick_tap_ms          = 0,
        .require_prior_idle_ms = 50,
    };
    prior_idle_seed_release(10);
    /* Gap 90 ms: 50 (per-key) <= 90 < 200 (global). Per-key wins → allow. */
    press_char_ht(KANA_KE, 10, 100, &hold_pi_50);
    CHECK(mock_fire_pending_timer(), "char_timeout fires");
    CHECK(mock_fire_pending_timer(), "tapping_term fires");
    CHECK(chordis_is_hold_active(), "HOLD allowed (per-key 50 won)");
    release_char(10, 500);
    TEST_END();
}

/* Per-key 0 inherits global → forced tap when global is non-zero. */
static void test_holdtap_prior_idle_inherits_global(void) {
    TEST_BEGIN("prior-idle: per-key=0 inherits non-zero global");
    chordis_engine_set_require_prior_idle_global(150);
    prior_idle_seed_release(30);
    /* Gap 70 ms < inherited 150 ms → forced tap. */
    press_char_ht(KANA_KE, 10, 100, &hold_lshft_inherit);
    for (int i = 0; i < 4 && mock_fire_pending_timer(); i++) {
        if (chordis_is_hold_active()) break;
    }
    CHECK(!chordis_is_hold_active(), "inherited gate must suppress HOLD");
    release_char(10, 500);
    TEST_END();
}

/*
 * 25. Slice D-3b: 3-key chain combo while HT is undecided.
 *     HI[HT] + YU + DAKUTEN → chain (HI+YU=HYU, HYU+DAKUTEN=BYU) → "byu".
 *     Per design doc:「3 キー同時押しが実現したら mod のホールドは起こらない」
 *     The HT mod must NOT activate; the tapping_term timer must be cancelled.
 */
static void test_holdtap_chain_combo_with_ht(void) {
    TEST_BEGIN("D-3b: 3-key chain combo with HT → no mod");
    press_char_ht(KANA_HI, 10, 0,  &hold_lshft_200);
    press_char(KANA_YU,      11, 20);   /* combo_pending */
    press_char(KANA_DAKUTEN, 12, 40);   /* 3rd key → chain → BYU */
    expect_romaji("byu");
    CHECK(!chordis_is_hold_active(), "no hold should be active");
    /* tapping_term timer should have been cancelled by tracker_free.
     * Firing pending should be a no-op (no stray hold activation). */
    CHECK(!mock_fire_pending_timer(), "no stray tapping_term timer");
    CHECK(!chordis_is_hold_active(), "still no hold after time advance");
    TEST_END();
}

/* HT-aware press helper that also provides a shifted face. Used by Slice E
 * tests where the shifted output (kana[side+1]) needs to be non-NONE. */
static void press_char_ht_shift(uint32_t k0, uint32_t kshift0, uint32_t position,
                                int64_t ts, const struct chordis_hold_config *hold) {
    uint32_t kana[CHORDIS_KANA_SLOTS] = { k0, kshift0, KANA_NONE, KANA_NONE, KANA_NONE };
    press_kana_ht(kana, position, ts, hold);
}

/*
 * 26. Slice E: thumb arrives within timeout_ms while HT-UNDECIDED →
 *     existing CHAR_THUMB_WAIT path, output shifted on thumb release.
 */
static void test_holdtap_thumb_within_timeout(void) {
    TEST_BEGIN("E: HT + thumb within timeout → shifted");
    press_char_ht_shift(KANA_KA, KANA_HI, 10, 0, &hold_lshft_200);
    press_thumb(CHORDIS_SHIFT_0, 30, 50);    /* elapsed=50 < timeout_ms=100 */
    release_thumb(CHORDIS_SHIFT_0, 30, 80);  /* commit shifted (HI face) */
    expect_romaji("hi");
    CHECK(!chordis_is_hold_active(), "no hold");
    release_char(10, 100);
    TEST_END();
}

/*
 * 27. Slice E: thumb arrives AFTER timeout_ms while HT-UNDECIDED →
 *     hold extension must NOT let CHAR_THUMB_WAIT shift the stale char.
 *     HT key flushes as unshifted; thumb starts a fresh THUMB_HELD.
 */
static void test_holdtap_thumb_after_timeout(void) {
    TEST_BEGIN("E: HT + thumb after timeout → unshifted, fresh thumb");
    press_char_ht_shift(KANA_KA, KANA_HI, 10, 0, &hold_lshft_200);
    press_thumb(CHORDIS_SHIFT_0, 30, 120);   /* elapsed=120 >= timeout_ms=100 */
    /* KA should be flushed as unshifted "ka" immediately */
    expect_romaji("ka");
    CHECK(!chordis_is_hold_active(), "no hold (HT was tap-flushed)");
    /* HT physical release is now a no-op (tracker freed). */
    release_char(10, 130);
    /* Thumb release alone (no char arrived) → thumb tap binding fires */
    release_thumb(CHORDIS_SHIFT_0, 30, 200);
    bool found_tap = false;
    for (int i = 0; i < mock_event_count; i++) {
        if (mock_event_log[i].kind == MOCK_EVT_BINDING_PRESS &&
            mock_event_log[i].value == 0x20) {
            found_tap = true;
            break;
        }
    }
    CHECK(found_tap, "thumb tap binding should fire on lone release");
    TEST_END();
}

/* Hold config with quick_tap_ms = 150 — used for Slice F repeat protection. */
static const struct chordis_hold_config hold_lshft_qt150 = {
    .binding = { .behavior_dev = "hold_dummy",
                 .param1 = HOLD_BINDING_PARAM, .param2 = 0 },
    .tapping_term_ms = 200,
    .quick_tap_ms    = 150,
};

/*
 * 28. Slice F: quick_tap_ms repeat protection. After a tap on an HT key,
 *     a re-press of the same position within quick_tap_ms must be forced
 *     to tap (no hold), even if held past tapping_term_ms.
 */
static void test_holdtap_quick_tap_repeat(void) {
    TEST_BEGIN("F: quick_tap_ms forces tap on rapid re-press");
    /* First press: normal tap */
    press_char_ht(KANA_HI, 10, 0,  &hold_lshft_qt150);
    release_char(10, 20);
    expect_romaji("hi");

    /* Second press 60ms after release (< quick_tap_ms=150).
     * Quick-tap protection should suppress hold for this tracker. */
    press_char_ht(KANA_HI, 10, 80, &hold_lshft_qt150);

    /* Drain any pending timers — char_timeout, etc. The tracker should
     * never enter TRK_HOLD because quick_tap suppression dropped has_hold. */
    for (int i = 0; i < 4 && mock_fire_pending_timer(); i++) {
        if (chordis_is_hold_active()) break;
    }
    CHECK(!chordis_is_hold_active(), "hold must NOT activate within quick_tap_ms");

    release_char(10, 350);
    int hi_count = 0;
    for (int i = 0; i < mock_event_count; i++) {
        if (mock_event_log[i].kind == MOCK_EVT_KEYCODE_PRESS &&
            mock_event_log[i].value == ascii_to_encoded('h')) {
            hi_count++;
        }
    }
    CHECK(hi_count == 2, "expected 2 'h' presses (two taps), got %d", hi_count);
    CHECK(!chordis_is_hold_active(), "still no hold");
    TEST_END();
}

/*
 * 29. Slice F: outside the quick_tap window, hold still works normally.
 */
static void test_holdtap_quick_tap_window_expired(void) {
    TEST_BEGIN("F: re-press after quick_tap window → hold still works");
    press_char_ht(KANA_HI, 10, 0,  &hold_lshft_qt150);
    release_char(10, 20);
    expect_romaji("hi");

    /* Re-press 200ms after release (> quick_tap_ms=150) — protection
     * window has expired, so hold should activate normally. */
    press_char_ht(KANA_HI, 10, 220, &hold_lshft_qt150);
    /* First fire: char_timeout extends (hold in flight). */
    CHECK(mock_fire_pending_timer(), "char_timeout should fire");
    /* Second fire: tapping_term resolves to TRK_HOLD. */
    CHECK(mock_fire_pending_timer(), "tapping_term should fire");
    CHECK(chordis_is_hold_active(), "hold active after tapping_term");
    release_char(10, 500);
    CHECK(!chordis_is_hold_active(), "hold cleared on release");
    TEST_END();
}

/*
 * 29b. Prior plain overlap: a later HT key must not lone-promote to HOLD
 *      on tapping_term alone. This is the core rolling fix.
 */
static void test_holdtap_prior_plain_blocks_lone_hold(void) {
    TEST_BEGIN("prior plain overlap blocks lone HOLD promotion");
    int off = 0;
    press_char(KANA_TO, 11, 0);                 /* prior plain */
    press_char_ht(KANA_KA, 10, 20, &hold_lshft_200);

    /* Prior plain is flushed immediately when the HT arrives. */
    off = expect_romaji_at(off, "to");

    CHECK(mock_fire_pending_timer(), "char_timeout fires");
    CHECK(mock_fire_pending_timer(), "tapping_term fires but stays undecided");
    CHECK(!chordis_is_hold_active(), "hold must remain inactive");

    /* HT release should tap, not hold. */
    release_char(10, 260);
    off = expect_romaji_at(off, "ka");
    CHECK(!chordis_is_hold_active(), "still no hold after HT release");
    CHECK(off == mock_event_count, "no unexpected trailing events");
    TEST_END();
}

/*
 * 29b. Prior plain overlap must not hang forever once the older plain is no
 *      longer active by the time tapping_term fires. The blocking snapshot is
 *      taken at HT press, but lone HOLD promotion should resume after the
 *      older plain has been emitted/freed.
 */
static void test_holdtap_prior_plain_clears_before_tapping_term(void) {
    TEST_BEGIN("prior plain overlap clears before tapping_term → HOLD");
    int off = 0;
    press_char(KANA_TO, 11, 0);                 /* older plain */
    press_char_ht(KANA_KA, 10, 20, &hold_lshft_200);

    /* The older plain is emitted and freed immediately when the HT arrives. */
    off = expect_romaji_at(off, "to");

    /* Physical release arrives later, but the tracker is already gone. */
    release_char(11, 40);
    CHECK(off == mock_event_count, "prior plain release should not add output");

    CHECK(mock_fire_pending_timer(), "char_timeout fires");
    CHECK(mock_fire_pending_timer(), "tapping_term fires and should promote");
    CHECK(chordis_is_hold_active(), "hold should activate once prior plain is gone");

    release_char(10, 260);
    CHECK(!chordis_is_hold_active(), "hold cleared on HT release");

    bool saw_press = false;
    bool saw_release = false;
    for (int i = 0; i < mock_event_count; i++) {
        if (mock_event_log[i].kind == MOCK_EVT_BINDING_PRESS &&
            mock_event_log[i].value == HOLD_BINDING_PARAM) {
            saw_press = true;
        }
        if (mock_event_log[i].kind == MOCK_EVT_BINDING_RELEASE &&
            mock_event_log[i].value == HOLD_BINDING_PARAM) {
            saw_release = true;
        }
    }
    CHECK(saw_press, "hold binding press expected");
    CHECK(saw_release, "hold binding release expected");
    TEST_END();
}

/*
 * 29c. Prior plain overlap + later allowed partner: the blocked HT must
 *      still be able to resolve via the existing hold-commit path.
 */
static void test_holdtap_prior_plain_later_partner_allows_hold(void) {
    TEST_BEGIN("prior plain overlap still allows later partner-driven HOLD");
    mock_set_keymap_binding(20, "kp_dummy", BASE_BINDING_PARAM);

    press_char(KANA_TO,    21, 0);               /* prior plain snapshot source */
    press_char_ht(KANA_HI, 10, 20, &hold_lshft_200);
    expect_romaji("to");
    press_char(KANA_YU,    20, 40);              /* later combo partner */

    CHECK(!chordis_is_hold_active(), "still no hold before hold-commit");

    release_char(20, 190);                       /* later partner released near boundary */
    for (int i = 0; i < 4 && !chordis_is_hold_active(); i++) {
        CHECK(mock_fire_pending_timer(), "timer should fire while waiting for HOLD");
    }
    CHECK(chordis_is_hold_active(), "hold should activate via hold-commit");

    release_char(10, 300);
    CHECK(!chordis_is_hold_active(), "hold cleared on HT release");
    TEST_END();
}

/*
 * 29d. Prior plain overlap must not break combos that use a later partner.
 */
static void test_holdtap_prior_plain_does_not_break_combo(void) {
    TEST_BEGIN("prior plain overlap preserves later combo resolution");
    int off = 0;
    press_char(KANA_TO,    12, 0);               /* prior plain snapshot source */
    press_char_ht(KANA_HI, 10, 20, &hold_lshft_200);
    off = expect_romaji_at(off, "to");
    press_char(KANA_YU,    11, 40);              /* later combo partner */

    release_char(11, 60);
    release_char(10, 80);
    off = expect_romaji_at(off, "hyu");
    CHECK(!chordis_is_hold_active(), "combo path should not activate hold");
    CHECK(off == mock_event_count, "no unexpected trailing events");
    TEST_END();
}

/*
 * 29e. Prior HT-undecided is not a "prior plain" blocker. The later HT may
 *      still lone-promote normally, preserving multi-mod semantics.
 */
static void test_holdtap_prior_ht_does_not_block_lone_hold(void) {
    TEST_BEGIN("prior HT-undecided does not block later lone HOLD");
    press_char_ht(KANA_HI, 10, 0,  &hold_lctl_200);   /* prior HT */
    press_char_ht(KANA_KA, 11, 20, &hold_lshft_200);  /* later HT */

    /* Drain timers until both holds have activated. */
    bool saw_second = false;
    for (int i = 0; i < 6 && mock_fire_pending_timer(); i++) {
        if (chordis_is_hold_active() && mock_event_count >= 2) {
            saw_second = true;
            break;
        }
    }
    CHECK(saw_second, "later HT should still lone-promote with prior HT only");

    release_char(11, 300);
    release_char(10, 320);
    CHECK(!chordis_is_hold_active(), "holds cleared after release");
    TEST_END();
}

/*
 * 29f. A prior HOLD-resolved tracker is modifier context, not a prior plain
 *      blocker for a newly pressed HT.
 */
static void test_holdtap_prior_hold_does_not_block_new_hold(void) {
    TEST_BEGIN("prior HOLD-resolved does not block later lone HOLD");
    press_char_ht(KANA_HI, 10, 0, &hold_lctl_200);
    CHECK(mock_fire_pending_timer(), "first char_timeout fires");
    CHECK(mock_fire_pending_timer(), "first tapping_term fires");
    CHECK(chordis_is_hold_active(), "first hold active");

    press_char_ht(KANA_KA, 11, 250, &hold_lshft_200);
    CHECK(mock_fire_pending_timer(), "second char_timeout fires");
    CHECK(mock_fire_pending_timer(), "second tapping_term fires");

    int hold_presses = 0;
    for (int i = 0; i < mock_event_count; i++) {
        if (mock_event_log[i].kind == MOCK_EVT_BINDING_PRESS) {
            hold_presses++;
        }
    }
    CHECK(hold_presses == 2, "second HT should still promote with prior HOLD");

    release_char(11, 500);
    release_char(10, 520);
    CHECK(!chordis_is_hold_active(), "holds cleared after release");
    TEST_END();
}

/*
 * 29g. Real Naginata-shaped reproducer: the "na" key is not an HT key, but
 *      it carries a KANA_HANDAKUTEN face and therefore participates in combo
 *      lookups. Once that older plain key is physically released, the later
 *      HT must be free to lone-promote on tapping_term; the prior key must
 *      not drive hold-commit or leave the HT permanently blocked.
 */
static void test_holdtap_prior_combo_marker_key_release_unblocks_hold(void) {
    TEST_BEGIN("prior combo-marker key release unblocks later HT hold");
    int off = 0;
    uint32_t na_key[CHORDIS_KANA_SLOTS] = {
        KANA_NA, KANA_KUTEN, KANA_NONE, KANA_NONE, KANA_HANDAKUTEN
    };
    uint32_t ru_key[CHORDIS_KANA_SLOTS] = {
        KANA_RU, KANA_YO, KANA_NONE, KANA_NONE, KANA_NONE
    };
    struct zmk_behavior_binding_event ev_na_press = { .position = 12, .timestamp = 0 };
    struct zmk_behavior_binding_event ev_na_rel   = { .position = 12, .timestamp = 60 };
    struct zmk_behavior_binding_event ev_ru_press = { .position = 10, .timestamp = 20 };

    press_kana(na_key, 12, 0);
    chordis_on_char_pressed(ru_key, ev_ru_press, &hold_lshft_200);
    release_char(12, 60);                    /* prior key released first */
    off = expect_romaji_at(off, "na");
    CHECK(!chordis_is_hold_active(), "hold should still be inactive before tapping_term");

    CHECK(mock_fire_pending_timer(), "char_timeout fires");
    CHECK(mock_fire_pending_timer(), "tapping_term fires and should promote");
    CHECK(chordis_is_hold_active(), "hold should activate after prior combo key release");

    release_char(10, 260);
    CHECK(!chordis_is_hold_active(), "hold cleared after HT release");
    CHECK(off + 2 == mock_event_count,
          "expected hold press+release after prior combo key release, got %d events",
          mock_event_count);
    (void)ev_na_press;
    (void)ev_na_rel;
    TEST_END();
}

/* ── Slice G: hold-required-key-positions ────────────────── */

/* HT config with hrkp restriction. Position 20 is allowed, 21 is not. */
static const int32_t hrkp_positions_g[] = { 20 };
static const struct chordis_hold_config hold_lshft_hrkp = {
    .binding = { .behavior_dev = "hold_dummy",
                 .param1 = HOLD_BINDING_PARAM, .param2 = 0 },
    .tapping_term_ms = 200,
    .quick_tap_ms    = 0,
    .hold_required_positions       = hrkp_positions_g,
    .hold_required_positions_count = 1,
};

/*
 * 30. Slice G: hrkp — HT held alone (no partner ever) still resolves to HOLD
 *     on tapping_term. Lone hold use case must keep working.
 */
static void test_holdtap_hrkp_lone_hold(void) {
    TEST_BEGIN("G: hrkp lone hold → HOLD on tapping_term");
    /* KANA_KA: not a combo candidate */
    press_char_ht(KANA_KA, 10, 0, &hold_lshft_hrkp);
    /* char_timeout extends, then tapping_term fires */
    CHECK(mock_fire_pending_timer(), "char_timeout fires");
    CHECK(mock_fire_pending_timer(), "tapping_term fires");
    CHECK(chordis_is_hold_active(), "hold should be active (no partner)");
    release_char(10, 500);
    CHECK(!chordis_is_hold_active(), "hold cleared");
    TEST_END();
}

/*
 * 31. Slice G: hrkp — partner position is in the allowed list → HOLD path runs
 *     normally. HT held + allowed partner B + tapping_term fires → mod active,
 *     B passes through as base layer keycode.
 */
static void test_holdtap_hrkp_allowed_partner(void) {
    TEST_BEGIN("G: hrkp allowed partner → HOLD activates");
    /* Register base-layer binding for the allowed partner so hold-commit path
     * can produce the passthrough. Use a non-combo-candidate kana for B. */
    mock_set_keymap_binding(20, "kp_dummy", BASE_BINDING_PARAM);
    press_char_ht(KANA_KA, 10, 0,  &hold_lshft_hrkp);
    press_char(KANA_TO,    20, 30);   /* allowed (pos 20 in list) */
    /* B is not a combo candidate. CHAR_WAIT logic will flush plain via
     * tracker_first_plain (KA stays HT-UNDECIDED), buffer TO. KA's
     * tapping_term timer is still armed. Drain timers until HOLD activates. */
    bool got_hold = false;
    for (int i = 0; i < 6 && mock_fire_pending_timer(); i++) {
        if (chordis_is_hold_active()) { got_hold = true; break; }
    }
    CHECK(got_hold, "hold should activate (allowed partner kept has_hold)");
    release_char(20, 400);
    release_char(10, 500);
    CHECK(!chordis_is_hold_active(), "hold cleared on HT release");
    TEST_END();
}

/*
 * 32. Slice G: hrkp — partner position is NOT in the allowed list → hrkp
 *     denies hold, has_hold stripped, tapping_term timer cancelled. HT
 *     resolves as plain tap; partner outputs normally. No mod, no
 *     hold-binding events.
 */
static void test_holdtap_hrkp_denied_partner(void) {
    TEST_BEGIN("G: hrkp non-allowed partner → forced tap, no HOLD");
    press_char_ht(KANA_KA, 10, 0,  &hold_lshft_hrkp);
    press_char(KANA_TO,    21, 30);   /* pos 21 NOT in allowed list */
    /* Drain any timers — neither hold-binding press nor hold-commit should occur. */
    for (int i = 0; i < 6 && mock_fire_pending_timer(); i++) {}
    CHECK(!chordis_is_hold_active(), "hold must not activate (denied partner)");
    /* Both keys release; expected output is "ka" then "to" (or whatever the
     * normal flush order produces). The key assertion is "no hold binding". */
    release_char(10, 200);
    release_char(21, 220);
    /* Drain residuals */
    for (int i = 0; i < 4 && mock_fire_pending_timer(); i++) {}
    /* Verify no BIND_PRESS for HOLD_BINDING_PARAM ever occurred. */
    for (int i = 0; i < mock_event_count; i++) {
        CHECK(!(mock_event_log[i].kind == MOCK_EVT_BINDING_PRESS &&
                mock_event_log[i].value == HOLD_BINDING_PARAM),
              "no hold binding press should appear");
    }
    CHECK(!chordis_is_hold_active(), "no hold");
    TEST_END();
}

/*
 * 33. Slice G: hrkp — combo partner that is NOT in the allowed list still
 *     resolves as a combo. hrkp only gates HOLD; the kana combo lookup is
 *     unaffected by has_hold being stripped (the combo runs against the
 *     now-plain tracker exactly as if hold-tap had never been configured).
 */
static void test_holdtap_hrkp_combo_partner_not_allowed(void) {
    TEST_BEGIN("G: hrkp non-allowed combo partner → combo wins, no HOLD");
    /* HI is a combo candidate (HI+YU=HYU). Configure hrkp that does NOT
     * include the YU position. The combo should still resolve. */
    press_char_ht(KANA_HI, 10, 0, &hold_lshft_hrkp);
    press_char(KANA_YU, 21, 30);   /* pos 21 NOT in allowed list (only 20 is) */
    release_char(21, 50);
    release_char(10, 70);
    expect_romaji("hyu");
    CHECK(!chordis_is_hold_active(), "no hold");
    TEST_END();
}

/*
 * 34. Slice G: hrkp — order-based latching. Once an allowed partner has
 *     been seen, a subsequent non-allowed key does NOT strip has_hold.
 *     The decision is made on the FIRST partner only.
 */
static void test_holdtap_hrkp_order_latching(void) {
    TEST_BEGIN("G: hrkp order latching — allowed first, non-allowed later");
    /* Allowed partner first, then non-allowed. The first partner sets
     * hrkp_decided=true, so the second one is ignored. */
    press_char_ht(KANA_KA, 10, 0,  &hold_lshft_hrkp);
    press_char(KANA_TO,    20, 20);  /* allowed → hrkp_decided=true */
    press_char(KANA_NU,    21, 40);  /* non-allowed but already decided */
    /* Drain timers — hold should still activate. */
    bool got_hold = false;
    for (int i = 0; i < 8 && mock_fire_pending_timer(); i++) {
        if (chordis_is_hold_active()) { got_hold = true; break; }
    }
    CHECK(got_hold, "hold should activate (allowed was first)");
    release_char(21, 400);
    release_char(20, 420);
    release_char(10, 440);
    CHECK(!chordis_is_hold_active(), "hold cleared");
    TEST_END();
}

/*
 * 35. Regression: flush while TRK_HOLD is active must release the hold
 *     binding. Without this, auto-off (triggered by e.g. Ctrl+X while a
 *     home-row-mod is held) would leave the HID modifier stuck down until
 *     a keyboard reset.
 */
static void test_holdtap_flush_releases_hold(void) {
    TEST_BEGIN("regression: flush releases active hold binding");
    press_char_ht(KANA_KA, 10, 0, &hold_lshft_200);
    /* Drive the tracker into TRK_HOLD */
    CHECK(mock_fire_pending_timer(), "char_timeout fires");
    CHECK(mock_fire_pending_timer(), "tapping_term fires");
    CHECK(chordis_is_hold_active(), "hold should be active");
    CHECK(mock_event_count == 1, "hold BIND_PRESS expected, got %d", mock_event_count);
    CHECK(mock_event_log[0].kind == MOCK_EVT_BINDING_PRESS, "[0] BIND_PRESS");

    /* Simulate auto-off: flush without the physical key having been released */
    chordis_engine_flush(300);

    /* Flush must emit BIND_RELEASE for the held hold-binding */
    CHECK(mock_event_count == 2, "BIND_RELEASE expected after flush, got %d",
          mock_event_count);
    CHECK(mock_event_log[1].kind  == MOCK_EVT_BINDING_RELEASE, "[1] BIND_RELEASE");
    CHECK(mock_event_log[1].value == HOLD_BINDING_PARAM,       "[1] hold binding");
    CHECK(!chordis_is_hold_active(), "hold cleared after flush");

    /* Late physical release (layer is off in reality; here we just verify
     * the engine is in a clean state and no crash / spurious output). */
    release_char(10, 400);
    CHECK(mock_event_count == 2, "no extra events on late release, got %d",
          mock_event_count);
    TEST_END();
}

/*
 * 36. Regression: an active TRK_HOLD tracker must NOT be visible to the
 *     combo / partition iterators. Before the fix, tracker_first()/_second()
 *     iterated all in_use trackers including TRK_HOLD ones, so the modifier
 *     tracker would be picked up as t1 in CHAR_WAIT combo logic. The
 *     subsequent tracker_free() then dropped the HOLD slot WITHOUT invoking
 *     the hold binding's release — the host modifier got stuck and the
 *     modifier's kana mapping leaked into combo lookups (manifesting as
 *     mysterious sequences like "C-h C-i" when X (ひ) was pressed).
 *
 *     This test pins down the contract: with a HOLD active, a normal kana
 *     press must allocate its own tracker without disturbing the HOLD, and
 *     a subsequent release of the HOLD via the normal release path must
 *     emit BIND_RELEASE.
 */
static void test_holdtap_hold_invisible_to_combo_iter(void) {
    TEST_BEGIN("regression: TRK_HOLD invisible to combo iterators");
    /* Press an HT char and drive it into TRK_HOLD. */
    press_char_ht(KANA_KA, 10, 0, &hold_lshft_200);
    CHECK(mock_fire_pending_timer(), "char_timeout fires (extends due to HT)");
    CHECK(mock_fire_pending_timer(), "tapping_term fires → TRK_HOLD");
    CHECK(chordis_is_hold_active(), "hold active");
    CHECK(mock_event_count == 1, "BIND_PRESS expected, got %d", mock_event_count);
    CHECK(mock_event_log[0].kind == MOCK_EVT_BINDING_PRESS, "[0] BIND_PRESS");

    /* Press a normal kana key (KANA_HI). The combo iterator must not pick
     * up the HOLD tracker — t1 should be NULL on the first iteration so the
     * normal CHAR_WAIT entry path runs. The HOLD tracker stays active. */
    press_char(KANA_HI, 11, 300);
    CHECK(chordis_is_hold_active(), "hold still active after kana press");
    CHECK(mock_event_count == 1, "no extra events (kana buffered), got %d",
          mock_event_count);

    /* Release the HOLD via the normal hold-tap release path — must emit
     * BIND_RELEASE. */
    release_char(11, 350);
    release_char(10, 360);
    CHECK(!chordis_is_hold_active(), "hold cleared after release");
    /* Search for BIND_RELEASE matching HOLD_BINDING_PARAM in the log. */
    bool saw_release = false;
    for (int i = 0; i < mock_event_count; i++) {
        if (mock_event_log[i].kind  == MOCK_EVT_BINDING_RELEASE &&
            mock_event_log[i].value == HOLD_BINDING_PARAM) {
            saw_release = true;
            break;
        }
    }
    CHECK(saw_release, "hold binding BIND_RELEASE expected in log");
    TEST_END();
}

/*
 * 37. Regression: HT pressed → plain partner pressed before tapping_term →
 *     tapping_term fires (HT promoted to HOLD) → the plain partner must NOT
 *     be left buffered to emit later as kana romaji (which would be smeared
 *     by the now-active modifier, e.g. ひ → "hi" → C-h C-i). It must be
 *     flushed via the base-layer binding at promotion time so the host
 *     receives modifier+keystroke (C-x).
 */
static void test_holdtap_plain_partner_flush_on_promotion(void) {
    TEST_BEGIN("regression: plain partner flushed via base layer on HOLD promotion");
    /* D-1.5: register base-layer binding for plain partner's position. */
    mock_set_keymap_binding(11, "kp_dummy", BASE_BINDING_PARAM);

    /* HT press, plain partner press before tapping_term. */
    press_char_ht(KANA_KA, 10, 0, &hold_lshft_200);
    press_char(KANA_HI, 11, 50);
    CHECK(mock_event_count == 0, "no output yet (both buffered)");

    /* Drive char_timeout (extends due to HT) and tapping_term. */
    CHECK(mock_fire_pending_timer(), "char_timeout fires");
    CHECK(mock_fire_pending_timer(), "tapping_term fires → TRK_HOLD");

    /* Expect: HT BIND_PRESS, then plain flushed via base BIND_PRESS+RELEASE. */
    CHECK(mock_event_count == 3, "expected 3 events, got %d", mock_event_count);
    CHECK(mock_event_log[0].kind  == MOCK_EVT_BINDING_PRESS, "[0] BIND_PRESS");
    CHECK(mock_event_log[0].value == HOLD_BINDING_PARAM,     "[0] hold binding");
    CHECK(mock_event_log[1].kind  == MOCK_EVT_BINDING_PRESS,   "[1] base BIND_PRESS");
    CHECK(mock_event_log[1].value == BASE_BINDING_PARAM,       "[1] base binding");
    CHECK(mock_event_log[2].kind  == MOCK_EVT_BINDING_RELEASE, "[2] base BIND_RELEASE");
    CHECK(mock_event_log[2].value == BASE_BINDING_PARAM,       "[2] base binding");
    CHECK(chordis_is_hold_active(), "hold active");

    /* Physical release of the plain partner — tracker already freed; no-op. */
    release_char(11, 100);
    CHECK(mock_event_count == 3, "no extra events on partner release, got %d",
          mock_event_count);

    /* HT release → BIND_RELEASE, hold cleared. */
    release_char(10, 200);
    CHECK(mock_event_count == 4, "expected 4 events, got %d", mock_event_count);
    CHECK(mock_event_log[3].kind  == MOCK_EVT_BINDING_RELEASE, "[3] BIND_RELEASE");
    CHECK(mock_event_log[3].value == HOLD_BINDING_PARAM,       "[3] hold binding");
    CHECK(!chordis_is_hold_active(), "hold cleared after release");
    TEST_END();
}

/*
 * 38. Option A: HT + plain partner, plain released first, HT released
 *     within tapping_term → both are taps → both kanas emitted.
 *     The plain's output is deferred until the HT resolves (tap or hold).
 */
static void test_holdtap_option_a_both_tap(void) {
    TEST_BEGIN("Option A: plain released, HT tapped → both kana");
    press_char_ht(KANA_KA, 10, 0, &hold_lshft_200);
    press_char(KANA_SI, 11, 50);
    CHECK(mock_event_count == 0, "no output yet (both buffered)");

    /* Plain partner release at t=80 — deferred (HT still undecided). */
    release_char(11, 80);
    CHECK(mock_event_count == 0, "plain deferred (no emit yet)");

    /* HT released at t=150, within tapping_term (200) → tap. Emits HT kana,
     * then deferred plain flushes as kana too. */
    release_char(10, 150);
    /* Expect both "ka" and "si" as romaji keycode events. HT kana first
     * (has_hold path), then deferred plain kana. */
    CHECK(!chordis_is_hold_active(), "no hold activated");
    CHECK(mock_event_count > 0, "expected some output");
    /* Verify no BIND_PRESS (no hold binding). */
    for (int i = 0; i < mock_event_count; i++) {
        CHECK(mock_event_log[i].kind != MOCK_EVT_BINDING_PRESS,
              "no hold binding press expected, got at idx=%d", i);
    }
    TEST_END();
}

/*
 * 39. Option A: HT + plain partner, plain released first, HT held past
 *     tapping_term → HOLD + deferred plain emits via base-layer binding.
 */
static void test_holdtap_option_a_hold_base_layer(void) {
    TEST_BEGIN("Option A: plain released, HT held → HOLD + base passthrough");
    mock_set_keymap_binding(11, "kp_dummy", BASE_BINDING_PARAM);

    press_char_ht(KANA_KA, 10, 0, &hold_lshft_200);
    press_char(KANA_SI, 11, 50);
    CHECK(mock_event_count == 0, "no output yet");

    /* Plain release at t=80 — deferred. */
    release_char(11, 80);
    CHECK(mock_event_count == 0, "plain deferred");

    /* Drive char_timeout + tapping_term → HOLD. */
    CHECK(mock_fire_pending_timer(), "char_timeout fires");
    CHECK(mock_fire_pending_timer(), "tapping_term fires → TRK_HOLD");

    /* Expect: BIND_PRESS (hold), then base BIND_PRESS + BIND_RELEASE (plain). */
    CHECK(chordis_is_hold_active(), "hold active");
    CHECK(mock_event_count == 3, "expected 3 events, got %d", mock_event_count);
    CHECK(mock_event_log[0].kind  == MOCK_EVT_BINDING_PRESS, "[0] hold BIND_PRESS");
    CHECK(mock_event_log[0].value == HOLD_BINDING_PARAM,     "[0] hold binding");
    CHECK(mock_event_log[1].kind  == MOCK_EVT_BINDING_PRESS,   "[1] base BIND_PRESS");
    CHECK(mock_event_log[1].value == BASE_BINDING_PARAM,       "[1] base binding");
    CHECK(mock_event_log[2].kind  == MOCK_EVT_BINDING_RELEASE, "[2] base BIND_RELEASE");
    CHECK(mock_event_log[2].value == BASE_BINDING_PARAM,       "[2] base binding");

    /* HT release → hold cleared. */
    release_char(10, 500);
    CHECK(!chordis_is_hold_active(), "hold cleared");
    TEST_END();
}

/* ── Main ─────────────────────────────────────────────────── */

/* ── Kogaki (小書き) combo tests ──────────────────────────── */

/*
 * Kogaki + よ → ょ (basic combo).
 * Q (KANA_KOGAKI) + よ key simultaneously → KANA_SMALL_YO.
 */
static void test_kogaki_combo_basic(void) {
    TEST_BEGIN("kogaki: Q+よ → ょ");
    press_char(KANA_KOGAKI, 0, 0);
    press_char(KANA_YO, 11, 30);
    release_char(11, 60);
    expect_romaji("xyo");
    release_char(0, 80);
    TEST_END();
}

/*
 * Kogaki + あ → ぁ (vowel variant).
 */
static void test_kogaki_combo_vowel(void) {
    TEST_BEGIN("kogaki: Q+あ → ぁ");
    press_char(KANA_KOGAKI, 0, 0);
    press_char(KANA_A, 11, 30);
    release_char(11, 60);
    expect_romaji("xa");
    release_char(0, 80);
    TEST_END();
}

/*
 * Kogaki + つ → っ.
 */
static void test_kogaki_combo_tu(void) {
    TEST_BEGIN("kogaki: Q+つ → っ");
    press_char(KANA_KOGAKI, 0, 0);
    press_char(KANA_TU, 11, 30);
    release_char(11, 60);
    expect_romaji("xtu");
    release_char(0, 80);
    TEST_END();
}

/*
 * Kogaki alone → no output (marker only, no romaji).
 */
static void test_kogaki_alone_silent(void) {
    TEST_BEGIN("kogaki: Q alone → no output");
    press_char(KANA_KOGAKI, 0, 0);
    release_char(0, 50);
    /* KANA_KOGAKI has no romaji entry — output_kana skips it */
    CHECK(mock_event_count == 0, "kogaki alone should produce no output");
    TEST_END();
}

/*
 * Kogaki + non-matching key (e.g. か) → separate output (no combo).
 * Kogaki produces nothing, か outputs normally.
 */
static void test_kogaki_no_match(void) {
    TEST_BEGIN("kogaki: Q+か → no combo, か only");
    press_char(KANA_KOGAKI, 0, 0);
    press_char(KANA_KA, 11, 30);
    release_char(11, 60);
    CHECK(mock_fire_pending_timer(), "timer should fire for KA");
    /* KOGAKI has no romaji → silent. KA outputs "ka". */
    expect_romaji("ka");
    release_char(0, 80);
    TEST_END();
}

/*
 * Reverse order: よ first, then kogaki → still ょ (bidirectional lookup).
 */
static void test_kogaki_reverse_order(void) {
    TEST_BEGIN("kogaki: よ+Q (reverse) → ょ");
    press_char(KANA_YO, 11, 0);
    press_char(KANA_KOGAKI, 0, 30);
    release_char(0, 60);
    expect_romaji("xyo");
    release_char(11, 80);
    TEST_END();
}

/* ── CDIS_KEY (raw keycode in kana slot) tests ───────────── */

/*
 * CDIS_KEY: single char with CDIS_KEY(BSPC) on unshifted face → timeout → BSPC tap.
 */
static void test_cis_key_single_unshifted(void) {
    TEST_BEGIN("CDIS_KEY: single char unshifted → BSPC");
    press_char(CDIS_KEY(BSPC), 10, 0);
    CHECK(mock_event_count == 0, "no output before timeout");
    CHECK(mock_fire_pending_timer(), "timer should fire");
    expect_raw_keycode(BSPC, "BSPC");
    TEST_END();
}

/*
 * CDIS_KEY: char has kana on unshifted, CDIS_KEY(BSPC) on center-shift face.
 * Thumb+char → shifted face → BSPC tap.
 */
static void test_cis_key_shifted_face(void) {
    TEST_BEGIN("CDIS_KEY: thumb+char → shifted CDIS_KEY(BSPC)");
    uint32_t kana[CHORDIS_KANA_SLOTS] = {
        KANA_VU, CDIS_KEY(BSPC), KANA_NONE, KANA_NONE, KANA_NONE
    };
    press_kana(kana, 10, 0);
    press_thumb(CHORDIS_SHIFT_0, 30, 20);
    CHECK(mock_event_count == 0, "no output yet");
    CHECK(mock_fire_pending_timer(), "timeout should fire");
    expect_raw_keycode(BSPC, "BSPC");
    TEST_END();
}

/*
 * CDIS_KEY: unshifted face is kana, no thumb → normal romaji output.
 * Ensures CDIS_KEY on a different face doesn't interfere.
 */
static void test_cis_key_kana_face_unaffected(void) {
    TEST_BEGIN("CDIS_KEY: kana face unaffected by CDIS_KEY on shift face");
    uint32_t kana[CHORDIS_KANA_SLOTS] = {
        KANA_VU, CDIS_KEY(BSPC), KANA_NONE, KANA_NONE, KANA_NONE
    };
    press_kana(kana, 10, 0);
    CHECK(mock_fire_pending_timer(), "timer should fire");
    expect_romaji("vu");  /* unshifted face = kana, not raw keycode */
    TEST_END();
}

/*
 * CDIS_KEY: CDIS_KEY(LEFT) — arrow key, not just BSPC.
 */
static void test_cis_key_left_arrow(void) {
    TEST_BEGIN("CDIS_KEY: LEFT arrow via CDIS_KEY");
    press_char(CDIS_KEY(LEFT), 10, 0);
    CHECK(mock_fire_pending_timer(), "timer should fire");
    expect_raw_keycode(LEFT, "LEFT");
    TEST_END();
}

/*
 * CDIS_KEY: CDIS_KEY(LS(BSPC)) — modifier-wrapped keycode.
 * Verifies that modifier bits in the encoded keycode are preserved.
 */
static void test_cis_key_with_modifier(void) {
    TEST_BEGIN("CDIS_KEY: LS(BSPC) modifier-wrapped keycode");
    press_char(CDIS_KEY(LS(BSPC)), 10, 0);
    CHECK(mock_fire_pending_timer(), "timer should fire");
    expect_raw_keycode(LS(BSPC), "LS(BSPC)");
    TEST_END();
}

/*
 * CDIS_KEY: KANA_NONE still produces no output (regression guard).
 */
static void test_cis_key_none_still_silent(void) {
    TEST_BEGIN("CDIS_KEY: KANA_NONE still silent");
    uint32_t kana[CHORDIS_KANA_SLOTS] = {
        KANA_NONE, KANA_NONE, KANA_NONE, KANA_NONE, KANA_NONE
    };
    press_kana(kana, 10, 0);
    CHECK(mock_fire_pending_timer(), "timer should fire");
    CHECK(mock_event_count == 0, "no output for KANA_NONE");
    TEST_END();
}

/*
 * CDIS_KEY: char release before timeout (sequential path) with CDIS_KEY.
 */
static void test_cis_key_release_before_timeout(void) {
    TEST_BEGIN("CDIS_KEY: char released before timeout → still outputs on timer");
    press_char(CDIS_KEY(BSPC), 10, 0);
    release_char(10, 50);
    CHECK(mock_event_count == 0, "no output on release");
    CHECK(mock_fire_pending_timer(), "timer should fire");
    expect_raw_keycode(BSPC, "BSPC");
    TEST_END();
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        mock_verbose = 1;
    }

    chordis_engine_init();
    register_test_combos();

    test_single_char_unshifted();
    test_char_release_before_timeout();
    test_char_thumb_timeout();
    test_thumb_tap_alone();
    test_thumb_held_then_char();
    test_spec_542_partition_d1_le_d2();
    test_spec_542_partition_d1_gt_d2();
    test_flush_char_wait();
    test_flush_thumb_held_unused();
    test_thumb_release_after_use();
    test_non_active_thumb_release_preserves_active_thumb();
    test_overlap_promote_newest_on_active_release();
    test_overlap_tap_on_last_release();
    test_overlap_promotion_inherits_used_state();
    test_buffered_char_thumb_wait_stable_across_promotion();
    test_buffered_char_thumb_wait_release_active_with_replacement();
    test_char_thumb_wait_d1_le_d2_clears_buffered();
    test_watchdog_reentry_picks_newest_held();
    test_spec_552_sequential_short_overlap();
    test_char_combo_basic();
    test_combo_candidate_alone();
    test_combo_no_match();
    test_holdtap_tap();
    test_holdtap_hold();
    test_holdtap_combo_candidate_hold();
    test_holdtap_combo_partner_combo_wins();
    test_holdtap_combo_partner_hold_commit_to_hold();
    test_holdtap_combo_partner_ht_first();
    test_holdtap_multi_mod_hold_commit();
    test_holdtap_multi_mod_both_tap();
    test_holdtap_hold_commit_no_base_binding();
    test_holdtap_ht_release_hold_commit_recovery();
    test_holdtap_globals_inherit_tapping_term();
    test_holdtap_globals_fallback_tapping_term_200();
    test_holdtap_globals_per_key_overrides();
    test_holdtap_globals_hold_after_partner_release();
    test_holdtap_hold_after_partner_release_does_not_preempt_tapping_term();
    test_holdtap_hold_after_partner_release_extends_near_tapping_term();
    test_holdtap_per_key_hold_after_partner_release();
    test_holdtap_per_key_hold_after_partner_release_max();
    test_holdtap_globals_inherit_quick_tap();
    test_holdtap_prior_idle_gap_satisfied();
    test_holdtap_prior_idle_gap_violated();
    test_holdtap_prior_idle_same_key_release_counts();
    test_holdtap_prior_idle_per_key_overrides();
    test_holdtap_prior_idle_inherits_global();
    test_holdtap_chain_combo_with_ht();
    test_holdtap_thumb_within_timeout();
    test_holdtap_thumb_after_timeout();
    test_holdtap_quick_tap_repeat();
    test_holdtap_quick_tap_window_expired();
    test_holdtap_prior_plain_blocks_lone_hold();
    test_holdtap_prior_plain_clears_before_tapping_term();
    test_holdtap_prior_plain_later_partner_allows_hold();
    test_holdtap_prior_plain_does_not_break_combo();
    test_holdtap_prior_ht_does_not_block_lone_hold();
    test_holdtap_prior_hold_does_not_block_new_hold();
    test_holdtap_prior_combo_marker_key_release_unblocks_hold();
    test_holdtap_hrkp_lone_hold();
    test_holdtap_hrkp_allowed_partner();
    test_holdtap_hrkp_denied_partner();
    test_holdtap_hrkp_combo_partner_not_allowed();
    test_holdtap_hrkp_order_latching();
    test_holdtap_flush_releases_hold();
    test_holdtap_hold_invisible_to_combo_iter();
    test_holdtap_plain_partner_flush_on_promotion();
    test_holdtap_option_a_both_tap();
    test_holdtap_option_a_hold_base_layer();

    /* Kogaki (小書き) combos */
    test_kogaki_combo_basic();
    test_kogaki_combo_vowel();
    test_kogaki_combo_tu();
    test_kogaki_alone_silent();
    test_kogaki_no_match();
    test_kogaki_reverse_order();

    /* CDIS_KEY (raw keycode in kana slot) */
    test_cis_key_single_unshifted();
    test_cis_key_shifted_face();
    test_cis_key_kana_face_unaffected();
    test_cis_key_left_arrow();
    test_cis_key_with_modifier();
    test_cis_key_none_still_silent();
    test_cis_key_release_before_timeout();

    fprintf(stderr, "\n%d run, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
