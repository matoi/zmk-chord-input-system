/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * NICOLA shared state machine — implementation.
 *
 * States:
 *   IDLE             — waiting for input
 *   CHAR_WAIT        — char key buffered, waiting for thumb (timeout → unshifted)
 *   CHAR_THUMB_WAIT  — char+thumb buffered, waiting for 2nd char or release
 *                       (spec 5.4.2 partitioning / 5.5.2 sequential detection)
 *   THUMB_HELD       — thumb held as shift modifier
 *
 * Thumb key acts like a modifier: press → THUMB_HELD, release → IDLE.
 * Thumb tap (Space) is output only when thumb is released without any
 * character key having been pressed during the hold.
 */

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
/* zmk/keymap.h provides:
 *   zmk_keymap_layer_default()
 *   zmk_keymap_get_layer_binding_at_idx()  — used in hold_commit_handler (D-1.5) */
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk-chordis/chordis_engine.h>
#include <zmk-chordis/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ── Timing configuration (read from DT once) ────────────── */

static struct chordis_timing_config timing = {
    .timeout_ms = 100,
    .sequential_threshold = 2,
    .sequential_min_overlap_ms = 20,
    .default_tapping_term_ms = 200,
    .default_quick_tap_ms    = 0,
    .default_require_prior_idle_ms = 0,
    .hold_after_partner_release_ms = 30,
};

/* Most recent release timestamp across char + thumb trackers. Used by
 * require-prior-idle-ms gating: an HT pressed within this window after
 * the previous release is forced to tap. -1 means "no prior release yet
 * in this session" (and therefore always satisfies the gate). */
static int64_t last_key_release_ts = -1;

/* ── Auto-off configuration ──────────────────────────────── */

static int chordis_active_layer = -1;

/* Auto-off deferred dismissal:
 * When the auto-off listener detects the configured mod+key combo, it must
 * NOT release the held modifier (the user is mid-shortcut, e.g. C-x C-f).
 * Instead it records the layer here; the actual layer-deactivate + LANG2 is
 * deferred until the last hold-tap modifier releases naturally, so the IME
 * dismissal arrives without modifier collision. -1 means no pending. */
static int chordis_pending_auto_off_layer = -1;

void chordis_set_active_layer(int layer) {
    chordis_active_layer = layer;
    LOG_DBG("nicola active layer set to %d", layer);
}

void chordis_clear_active_layer(void) {
    chordis_active_layer = -1;
    chordis_pending_auto_off_layer = -1;
    LOG_DBG("nicola active layer cleared");
}

#if DT_HAS_COMPAT_STATUS_OKAY(zmk_chord_input_config)
#define NC_CFG_NODE DT_INST(0, zmk_chord_input_config)

#if DT_NODE_HAS_PROP(NC_CFG_NODE, auto_off_keys)
#define CHORDIS_AUTO_OFF_ENABLED 1

#define AUTO_OFF_ENTRY(node, prop, idx) DT_PROP_BY_IDX(node, prop, idx),
static const uint32_t auto_off_keys[] = {
    DT_FOREACH_PROP_ELEM(NC_CFG_NODE, auto_off_keys, AUTO_OFF_ENTRY)
};
#define AUTO_OFF_PAIR_COUNT (ARRAY_SIZE(auto_off_keys) / 2)

#endif /* DT_NODE_HAS_PROP auto_off_keys */
#endif /* DT_HAS_COMPAT_STATUS_OKAY */

/* ── Per-position char key tracker ─────────────────────────
 * Replaces the fixed buffered_kana/buffered_kana_2 pair from the old
 * singleton state. Each active character key (in CHAR_WAIT etc.) occupies
 * one slot. Ordering is by pressed_ts, not slot index.
 *
 * Phase 2 will extend this struct with per-key hold-tap fields
 * (tapping_term_ms, hold_binding, resolution state, own timer).
 */

#ifndef CHORDIS_MAX_TRACKERS
#define CHORDIS_MAX_TRACKERS 6
#endif

enum tracker_resolution {
    TRK_UNDECIDED,    /* pressed, tap/hold not yet decided */
    TRK_TAP_PENDING,  /* tap confirmed, output may still be pending */
    TRK_HOLD,         /* hold confirmed, hold_binding active */
};

struct key_tracker {
    bool     in_use;
    uint32_t position;
    int64_t  pressed_ts;
    uint32_t kana[CHORDIS_KANA_SLOTS];

    enum tracker_resolution resolution;

    /* hold-tap fields (valid when has_hold == true) */
    bool                          has_hold;
    /* True if this press had a hold-tap config attached, even if quick_tap
     * suppressed has_hold. Used to record the release timestamp for future
     * quick_tap_ms checks (Slice F repeat protection). */
    bool                          ht_configured;
    uint32_t                      tapping_term_ms;
    uint32_t                      quick_tap_ms;
    uint32_t                      hold_after_partner_release_ms;
    struct zmk_behavior_binding   hold_binding;
    struct k_work_delayable       tapping_term_timer;
    bool                          tapping_term_timer_inited;

    /* Hold-after-partner-release balanced resolution (Slice D-1).
     * Set when a plain combo partner is released while an HT-UNDECIDED
     * tracker is still held. The hold_commit_timer waits
     * hold_after_partner_release_ms for the HT to also be released
     * (→ combo) or fires (→ HT becomes HOLD). */
    bool                          awaiting_hold_commit;
    int64_t                       released_ts;
    uint32_t                      hold_commit_delay_ms;
    struct k_work_delayable       hold_commit_timer;
    bool                          hold_commit_timer_inited;

    /* Snapshot taken when this HT key was pressed: true if a plain
     * (non-HT, non-HOLD) tracker was already active. Such a tracker must
     * not lone-promote to HOLD on tapping_term alone; only post-press
     * partners may still drive combo / hold-commit resolution. */
    bool                          blocked_by_prior_plain;

    /* hold-required-key-positions gating (Slice G).
     * When the tracker has an hrkp list configured, the FIRST non-self key
     * pressed during the undecided window decides hold eligibility:
     *   - position in list  → tracker keeps has_hold (hrkp_decided = true)
     *   - position not in list → has_hold stripped, timer cancelled
     * hrkp_decided latches after the first decision; subsequent partners do
     * not flip the result. Lone hold (no partner ever) still resolves via
     * tapping_term_timer as usual. */
    const int32_t                *hrkp_positions;
    uint32_t                      hrkp_positions_count;
    bool                          hrkp_decided;
};

/* Release-gap window for HT vs combo partner balancing (Slice D-1).
 * When the plain combo partner B is released while HT-UNDECIDED A is held,
 * we wait this many ms for A's release before deciding combo vs hold. */
/* Wait window after a plain combo partner is released before the engine
 * commits an HT-undecided peer to HOLD. Each HT may tune the grace period;
 * when multiple HTs share a plain release, the engine uses the maximum
 * resolved value so a short-window peer cannot truncate a longer one. */

/* ── Singleton state machine ──────────────────────────────── */

static struct {
    enum chordis_state_id state;

    /* Char key trackers — unordered, logical order derived from pressed_ts */
    struct key_tracker chars[CHORDIS_MAX_TRACKERS];

    /* Active thumb (valid in CHAR_THUMB_WAIT and THUMB_HELD) */
    enum chordis_thumb_side active_thumb;
    int64_t  thumb_timestamp;
    bool     thumb_used;          /* true if any char key fired during THUMB_HELD */
    struct zmk_behavior_binding thumb_tap_binding;
    uint32_t thumb_position;

    /* Timer (CHAR_WAIT: thumb timeout, CHAR_THUMB_WAIT: 2nd char timeout) */
    struct k_work_delayable timeout_work;

    /* Watchdog timer for THUMB_HELD — recovers from dropped BLE release */
    struct k_work_delayable watchdog_work;

    /* Combo pending flag: two chars in trackers, combo lookup succeeded (or
     * chain-combo potential detected). Set in CHAR_WAIT when 2nd char
     * arrives, cleared on resolution or flush. */
    bool combo_pending;

    /* Physical key tracking (independent of state machine).
     * Used to detect hold-based combos and recover from BLE drops.
     * Multiple thumb/shift keys may overlap physically; thumb_slots tracks
     * all of them, while active_thumb exposes exactly one logical shift
     * source for new input. */
    struct {
        bool     held;
        int64_t  pressed_ts;
        struct zmk_behavior_binding tap_binding;
        uint32_t position;
    } thumb_slots[CHORDIS_MAX_SHIFTS];
    struct {
        bool     held;
        uint32_t position;
        int64_t  pressed_ts;
    } char_slots[CHORDIS_MAX_TRACKERS];
    int  char_held_count;       /* number of char keys physically held */

    /* Buffered thumb context — frozen shift source for in-flight
     * CHAR_THUMB_WAIT / THUMB_CHAR_WAIT interactions. Immune to later
     * active_thumb promotion so combo interpretation cannot change
     * retroactively. */
    enum chordis_thumb_side buffered_thumb_side;
    bool                   has_buffered_thumb;

    bool timer_initialized;
} sm;

/* ── Physical thumb-slot helpers ──────────────────────────────
 * Aggregate tracking for overlapping thumb/shift keys. active_thumb is the
 * logical shift source for NEW input; the slot array tracks every thumb
 * that is physically down so release/promotion/watchdog can see them all.
 */

static void thumb_mark_pressed(enum chordis_thumb_side side,
                               const struct zmk_behavior_binding *tap,
                               uint32_t position,
                               int64_t ts) {
    if ((int)side < 0 || (int)side >= CHORDIS_MAX_SHIFTS) return;
    sm.thumb_slots[side].held         = true;
    sm.thumb_slots[side].pressed_ts   = ts;
    sm.thumb_slots[side].tap_binding  = *tap;
    sm.thumb_slots[side].position     = position;
}

static void thumb_mark_released(enum chordis_thumb_side side) {
    if ((int)side < 0 || (int)side >= CHORDIS_MAX_SHIFTS) return;
    sm.thumb_slots[side].held = false;
}

static bool thumb_any_held(void) {
    for (int i = 0; i < CHORDIS_MAX_SHIFTS; i++) {
        if (sm.thumb_slots[i].held) return true;
    }
    return false;
}

static void char_mark_pressed(uint32_t position, int64_t ts) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        if (sm.char_slots[i].held && sm.char_slots[i].position == position) {
            sm.char_slots[i].pressed_ts = ts;
            return;
        }
    }
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        if (!sm.char_slots[i].held) {
            sm.char_slots[i].held      = true;
            sm.char_slots[i].position  = position;
            sm.char_slots[i].pressed_ts = ts;
            return;
        }
    }
}

static void char_mark_released(uint32_t position) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        if (sm.char_slots[i].held && sm.char_slots[i].position == position) {
            sm.char_slots[i].held = false;
            return;
        }
    }
}

static void char_clear_all_physical(void) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        sm.char_slots[i].held = false;
    }
}

/* Returns true if at least one thumb is held and writes the newest-held
 * side (max pressed_ts) to *out_side. */
static bool thumb_newest_held(enum chordis_thumb_side *out_side) {
    bool found = false;
    int64_t newest_ts = 0;
    enum chordis_thumb_side newest = (enum chordis_thumb_side)0;
    for (int i = 0; i < CHORDIS_MAX_SHIFTS; i++) {
        if (!sm.thumb_slots[i].held) continue;
        if (!found || sm.thumb_slots[i].pressed_ts > newest_ts) {
            newest    = (enum chordis_thumb_side)i;
            newest_ts = sm.thumb_slots[i].pressed_ts;
            found     = true;
        }
    }
    if (found && out_side) *out_side = newest;
    return found;
}

/* Copy physical slot state into the logical sm.active_thumb / tap_binding /
 * position fields. Used for release-time promotion and watchdog re-entry so
 * the tap binding and position match the thumb currently driving shift. */
static void thumb_promote_active(enum chordis_thumb_side side) {
    if ((int)side < 0 || (int)side >= CHORDIS_MAX_SHIFTS) return;
    sm.active_thumb       = side;
    sm.thumb_timestamp    = sm.thumb_slots[side].pressed_ts;
    sm.thumb_tap_binding  = sm.thumb_slots[side].tap_binding;
    sm.thumb_position     = sm.thumb_slots[side].position;
}

#define THUMB_WATCHDOG_MS 1000

/* Quick-tap history: small ring buffer of recent (position, released_ts) pairs.
 * Used to suppress hold on rapid re-press of the same position within
 * quick_tap_ms (repeat-typing protection). Sized small — only HT keys need
 * an entry, and only the most recent release per position matters. */
#define CHORDIS_QUICK_TAP_HISTORY 8
struct quick_tap_entry {
    bool     valid;
    uint32_t position;
    int64_t  released_ts;
};
static struct quick_tap_entry quick_tap_history[CHORDIS_QUICK_TAP_HISTORY];

/** Record a release timestamp for quick-tap suppression. */
static void quick_tap_record(uint32_t position, int64_t ts) {
    /* Replace any existing entry for this position. */
    for (int i = 0; i < CHORDIS_QUICK_TAP_HISTORY; i++) {
        if (quick_tap_history[i].valid &&
            quick_tap_history[i].position == position) {
            quick_tap_history[i].released_ts = ts;
            return;
        }
    }
    /* Otherwise allocate the oldest slot (simple LRU by released_ts). */
    int oldest = 0;
    for (int i = 1; i < CHORDIS_QUICK_TAP_HISTORY; i++) {
        if (!quick_tap_history[i].valid) {
            oldest = i;
            break;
        }
        if (quick_tap_history[i].released_ts < quick_tap_history[oldest].released_ts) {
            oldest = i;
        }
    }
    quick_tap_history[oldest].valid       = true;
    quick_tap_history[oldest].position    = position;
    quick_tap_history[oldest].released_ts = ts;
}

/** Look up the last release timestamp for a position. Returns -1 if none. */
static int64_t quick_tap_lookup(uint32_t position) {
    for (int i = 0; i < CHORDIS_QUICK_TAP_HISTORY; i++) {
        if (quick_tap_history[i].valid &&
            quick_tap_history[i].position == position) {
            return quick_tap_history[i].released_ts;
        }
    }
    return -1;
}

/* ── Tracker helpers ──────────────────────────────────────── */

static void tapping_term_handler(struct k_work *work);
static void hold_commit_handler(struct k_work *work);
static struct key_tracker *tracker_find_by_position(uint32_t position);

/** Allocate a free tracker slot. Returns NULL if all slots are in use. */
static struct key_tracker *tracker_alloc(void) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        if (!sm.chars[i].in_use) {
            struct key_tracker *t = &sm.chars[i];
            t->in_use          = true;
            t->resolution      = TRK_UNDECIDED;
            t->has_hold        = false;
            t->ht_configured   = false;
            t->awaiting_hold_commit = false;
            t->hold_commit_delay_ms = 0;
            t->blocked_by_prior_plain = false;
            t->hrkp_positions       = NULL;
            t->hrkp_positions_count = 0;
            t->hrkp_decided         = false;
            if (!t->tapping_term_timer_inited) {
                k_work_init_delayable(&t->tapping_term_timer, tapping_term_handler);
                t->tapping_term_timer_inited = true;
            }
            if (!t->hold_commit_timer_inited) {
                k_work_init_delayable(&t->hold_commit_timer, hold_commit_handler);
                t->hold_commit_timer_inited = true;
            }
            return t;
        }
    }
    LOG_WRN("tracker table full");
    return NULL;
}

/** Free a tracker slot. */
static void tracker_free(struct key_tracker *t) {
    if (!t) return;
    if (t->has_hold) {
        k_work_cancel_delayable(&t->tapping_term_timer);
    }
    if (t->awaiting_hold_commit) {
        k_work_cancel_delayable(&t->hold_commit_timer);
    }
    t->in_use          = false;
    t->has_hold        = false;
    t->awaiting_hold_commit = false;
    t->hold_commit_delay_ms = 0;
    t->blocked_by_prior_plain = false;
    t->resolution      = TRK_UNDECIDED;
}

/** Release all TRK_HOLD-resolved hold bindings (e.g. &kp LCTRL → release LCTRL)
 * and free those trackers. Used by every flush path that wipes trackers en
 * masse, so the HID modifier state cannot get stuck while a kana flush emits
 * romaji that would otherwise be combined with the leaked modifier. */
static void release_held_trackers(int64_t timestamp) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        struct key_tracker *t = &sm.chars[i];
        if (!t->in_use || t->resolution != TRK_HOLD) continue;
        LOG_DBG("release_held_trackers: pos=%d", t->position);
        struct zmk_behavior_binding_event ev = {
            .position  = t->position,
            .timestamp = timestamp,
        };
        zmk_behavior_invoke_binding(&t->hold_binding, ev, false);
        tracker_free(t);
    }
}

/** Find an active HT tracker still in TRK_UNDECIDED, optionally excluding one. */
static struct key_tracker *find_ht_undecided(const struct key_tracker *exclude) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        struct key_tracker *t = &sm.chars[i];
        if (t->in_use && t->has_hold && t->resolution == TRK_UNDECIDED &&
            t != exclude) {
            return t;
        }
    }
    return NULL;
}

/** Returns true if the given plain tracker may act as a hold partner for the
 * undecided HT tracker. A blocked HT ignores partners that were already down
 * before the HT was pressed, but still accepts keys pressed afterwards. */
static bool ht_accepts_partner(const struct key_tracker *ht,
                               const struct key_tracker *partner) {
    if (!ht || !partner || !ht->in_use || !partner->in_use) return false;
    if (!ht->has_hold || ht->resolution != TRK_UNDECIDED) return false;
    if (ht->blocked_by_prior_plain && partner->pressed_ts < ht->pressed_ts) {
        return false;
    }
    return true;
}

/** Find an active HT tracker that may use the given partner tracker to drive
 * hold-commit / hold resolution, optionally excluding one tracker. */
static struct key_tracker *find_ht_undecided_for_partner(
    const struct key_tracker *partner,
    const struct key_tracker *exclude) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        struct key_tracker *t = &sm.chars[i];
        if (t == exclude) continue;
        if (ht_accepts_partner(t, partner)) {
            return t;
        }
    }
    return NULL;
}

/** Return the longest effective hold-commit delay among HT trackers that
 * would be resolved by a hold-commit firing for this plain tracker.
 *
 * The commit deadline for each HT is:
 *   max(HT press + tapping-term, plain release + hold-after-partner-release)
 *
 * This means hold-after-partner-release can delay HOLD slightly beyond
 * tapping-term near the boundary, but it must never make HOLD commit earlier
 * than tapping-term. Multiple HTs use the latest deadline so a short-window
 * peer cannot truncate a longer one. */
static uint32_t max_hold_commit_delay_for_pending_hts(
    const struct key_tracker *partner,
    const struct key_tracker *exclude) {
    int64_t latest_deadline = partner->released_ts;
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        struct key_tracker *t = &sm.chars[i];
        if (t == exclude) continue;
        if (!t->in_use || !t->has_hold || t->resolution != TRK_UNDECIDED) {
            continue;
        }
        int64_t tapping_deadline =
            t->pressed_ts + (int64_t)t->tapping_term_ms;
        int64_t partner_grace_deadline =
            partner->released_ts + (int64_t)t->hold_after_partner_release_ms;
        int64_t deadline = tapping_deadline > partner_grace_deadline
                               ? tapping_deadline
                               : partner_grace_deadline;
        if (deadline > latest_deadline) {
            latest_deadline = deadline;
        }
    }
    return (uint32_t)(latest_deadline - partner->released_ts);
}

/** Find the oldest non-HT (plain) tracker, or NULL if none. */
static struct key_tracker *tracker_first_plain(void) {
    struct key_tracker *first = NULL;
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        struct key_tracker *t = &sm.chars[i];
        if (t->in_use && !t->has_hold) {
            if (first == NULL || t->pressed_ts < first->pressed_ts) {
                first = t;
            }
        }
    }
    return first;
}

/** Returns true if a plain (non-HT, non-HOLD) tracker already exists.
 * Used as a snapshot when creating a new HT tracker so post-roll presses
 * do not lone-promote to HOLD based solely on tapping_term. */
static bool tracker_has_prior_plain(void) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        struct key_tracker *t = &sm.chars[i];
        if (t->in_use && !t->has_hold && t->resolution != TRK_HOLD) {
            return true;
        }
    }
    return false;
}

/** Returns true if this HT still has an older plain key physically held.
 * Older plain keys may still be represented by a live plain tracker, or they
 * may already have been emitted/freed while the physical switch remains down. */
static bool tracker_has_active_prior_plain_before(const struct key_tracker *ht) {
    if (!ht) return false;
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        if (!sm.char_slots[i].held ||
            sm.char_slots[i].position == ht->position ||
            sm.char_slots[i].pressed_ts >= ht->pressed_ts) {
            continue;
        }
        const struct key_tracker *t = tracker_find_by_position(sm.char_slots[i].position);
        if (t == NULL || (!t->has_hold && t->resolution != TRK_HOLD)) {
            return true;
        }
    }
    return false;
}

/** Clear all tracker slots. */
static void tracker_clear_all(void) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        sm.chars[i].in_use = false;
    }
}

/** Count active trackers. */
__attribute__((unused))
static int tracker_count(void) {
    int n = 0;
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        if (sm.chars[i].in_use) n++;
    }
    return n;
}

/** Count non-HOLD trackers — i.e. trackers still relevant to kana / combo
 *  resolution. Used to decide when the state machine should drop back to
 *  IDLE after a HOLD promotion (HOLD trackers are modifier state and are
 *  invisible to the combo iterators). */
static int tracker_count_active_chars(void) {
    int n = 0;
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        if (sm.chars[i].in_use && sm.chars[i].resolution != TRK_HOLD) n++;
    }
    return n;
}

/* Emit each in-use plain (non-HT) tracker via its base-layer binding
 * (press + release) and free the slot. Used when an HT promotes to HOLD
 * with plain trackers buffered alongside: the user's intent is "modifier
 * + key", so the buffered chars must be delivered as modified keystrokes
 * via the base layer rather than emitted later as kana romaji (which
 * would smear the now-active modifier across the romaji output, e.g.
 * a buffered ひ → "hi" → C-h C-i instead of the intended C-x).
 *
 * HT-UNDECIDED sidecars (has_hold == true) are preserved — they may
 * still resolve as their own taps or holds. */
static void flush_plain_trackers_via_base_layer(int64_t timestamp) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        struct key_tracker *t = &sm.chars[i];
        if (!t->in_use || t->has_hold) continue;
        const struct zmk_behavior_binding *base = zmk_keymap_get_layer_binding_at_idx(
            zmk_keymap_layer_default(), t->position);
        if (base != NULL && base->behavior_dev != NULL) {
            struct zmk_behavior_binding base_copy = *base;
            struct zmk_behavior_binding_event ev = {
                .position  = t->position,
                .timestamp = timestamp,
            };
            LOG_DBG("flush plain via base layer: pos=%d", t->position);
            zmk_behavior_invoke_binding(&base_copy, ev, true);
            zmk_behavior_invoke_binding(&base_copy, ev, false);
        } else {
            LOG_DBG("flush plain via base layer: pos=%d no base binding — drop",
                    t->position);
        }
        tracker_free(t);
    }
}

/**
 * Return the tracker with the Nth oldest pressed_ts among active trackers.
 * order=0 returns the oldest, order=1 the second oldest, etc. NULL if fewer
 * than (order+1) active trackers exist.
 */
static struct key_tracker *tracker_by_order(int order) {
    struct key_tracker *active[CHORDIS_MAX_TRACKERS];
    int n = 0;
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        /* Skip TRK_HOLD: these are modifier state, not char input. The
         * combo / partition logic must not pick them up as t1/t2 — doing so
         * would (a) feed the modifier's kana mapping into combo lookups
         * (producing unexpected output) and (b) tracker_free() the HOLD slot
         * without releasing its hold_binding, leaving the host modifier
         * stuck and the position untracked for its eventual release. */
        if (sm.chars[i].in_use && sm.chars[i].resolution != TRK_HOLD) {
            active[n++] = &sm.chars[i];
        }
    }
    if (order >= n) return NULL;
    /* Partial selection sort — small N, in-place */
    for (int s = 0; s <= order; s++) {
        int min = s;
        for (int j = s + 1; j < n; j++) {
            if (active[j]->pressed_ts < active[min]->pressed_ts) min = j;
        }
        if (min != s) {
            struct key_tracker *tmp = active[s];
            active[s] = active[min];
            active[min] = tmp;
        }
    }
    return active[order];
}

static inline struct key_tracker *tracker_first(void)  { return tracker_by_order(0); }
static inline struct key_tracker *tracker_second(void) { return tracker_by_order(1); }

/** Find an active tracker by position. */
static struct key_tracker *tracker_find_by_position(uint32_t position) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        if (sm.chars[i].in_use && sm.chars[i].position == position) {
            return &sm.chars[i];
        }
    }
    return NULL;
}

/* ── Character combo table ────────────────────────────────── */

struct char_combo {
    uint32_t kana_a;
    uint32_t kana_b;
    uint32_t result;
};

static struct char_combo combo_table[CONFIG_ZMK_CHORDIS_MAX_CHAR_COMBOS];
static int combo_count = 0;

void chordis_register_char_combo(uint32_t kana_a, uint32_t kana_b, uint32_t result) {
    if (combo_count >= CONFIG_ZMK_CHORDIS_MAX_CHAR_COMBOS) {
        LOG_WRN("char combo table full (%d)", CONFIG_ZMK_CHORDIS_MAX_CHAR_COMBOS);
        return;
    }
    combo_table[combo_count].kana_a = kana_a;
    combo_table[combo_count].kana_b = kana_b;
    combo_table[combo_count].result = result;
    combo_count++;
    LOG_DBG("registered char combo: %d + %d → %d (total=%d)", kana_a, kana_b, result, combo_count);
}

/**
 * Look up a combo for two kana IDs (bidirectional).
 * Returns the result kana ID, or KANA_NONE if no match.
 */
static uint32_t lookup_char_combo(uint32_t kana_a, uint32_t kana_b) {
    for (int i = 0; i < combo_count; i++) {
        if ((combo_table[i].kana_a == kana_a && combo_table[i].kana_b == kana_b) ||
            (combo_table[i].kana_a == kana_b && combo_table[i].kana_b == kana_a)) {
            return combo_table[i].result;
        }
    }
    return KANA_NONE;
}

/**
 * Check if a kana ID appears in any combo entry (as either side).
 * Used to decide whether to buffer a char in THUMB_HELD for combo detection.
 */
static bool is_combo_candidate(uint32_t kana_id) {
    if (kana_id == KANA_NONE) return false;
    for (int i = 0; i < combo_count; i++) {
        if (combo_table[i].kana_a == kana_id || combo_table[i].kana_b == kana_id) {
            return true;
        }
    }
    return false;
}

/**
 * Check if any kana in a multi-slot array is a combo candidate.
 */
static bool is_any_combo_candidate(const uint32_t kana[CHORDIS_KANA_SLOTS]) {
    for (int i = 0; i < CHORDIS_KANA_SLOTS; i++) {
        if (is_combo_candidate(kana[i])) return true;
    }
    return false;
}

/**
 * Check if two keys could form a chain combo with some future 3rd key.
 * For each combo entry where key 'a' participates, check if the result
 * chains with any face of 'b' (and vice versa).
 * Example: る[RU,YO]+濁点[KA,MA,_,DAKUTEN,_] → combo SI+YO=SYO exists
 *          (YO is a face of る), and SYO+DAKUTEN=ZYO exists → true.
 */
static bool has_potential_chain(const uint32_t a[CHORDIS_KANA_SLOTS],
                                const uint32_t b[CHORDIS_KANA_SLOTS]) {
    for (int c = 0; c < combo_count; c++) {
        uint32_t ca = combo_table[c].kana_a;
        uint32_t cb = combo_table[c].kana_b;
        uint32_t cr = combo_table[c].result;

        /* Does key 'a' participate in this combo? */
        bool a_in = false;
        for (int i = 0; i < CHORDIS_KANA_SLOTS && !a_in; i++) {
            if (a[i] != KANA_NONE && (a[i] == ca || a[i] == cb)) a_in = true;
        }
        if (a_in) {
            for (int j = 0; j < CHORDIS_KANA_SLOTS; j++) {
                if (b[j] != KANA_NONE && lookup_char_combo(cr, b[j]) != KANA_NONE)
                    return true;
            }
        }

        /* Does key 'b' participate in this combo? */
        bool b_in = false;
        for (int i = 0; i < CHORDIS_KANA_SLOTS && !b_in; i++) {
            if (b[i] != KANA_NONE && (b[i] == ca || b[i] == cb)) b_in = true;
        }
        if (b_in) {
            for (int j = 0; j < CHORDIS_KANA_SLOTS; j++) {
                if (a[j] != KANA_NONE && lookup_char_combo(cr, a[j]) != KANA_NONE)
                    return true;
            }
        }
    }
    return false;
}

/**
 * Look up a combo trying all kana slot combinations (for CHAR_WAIT,
 * where no shift is active). Needed because yōon combos register
 * e.g. KANA_SI + KANA_YO, but YO may be on a shift face of the key
 * whose unshifted face is RU.
 */
static uint32_t lookup_any_combo(const uint32_t kana_a[CHORDIS_KANA_SLOTS],
                                 const uint32_t kana_b[CHORDIS_KANA_SLOTS]) {
    for (int i = 0; i < CHORDIS_KANA_SLOTS; i++) {
        if (kana_a[i] == KANA_NONE) continue;
        for (int j = 0; j < CHORDIS_KANA_SLOTS; j++) {
            if (kana_b[j] == KANA_NONE) continue;
            uint32_t result = lookup_char_combo(kana_a[i], kana_b[j]);
            if (result != KANA_NONE) return result;
        }
    }
    return KANA_NONE;
}

/**
 * Look up a shifted combo. The active shift face is prioritised, but
 * ALL kana slots of both characters are searched because the two keys
 * may contribute kana from different shift faces (e.g. じょ = ZI from
 * dakuten face of し + YO from center-shift face of る).
 *
 * Priority:
 *   1. shifted_a + any face of b  (e.g. じ + よ → じょ)
 *   2. any face of a + shifted_b
 *   3. any remaining combination   (fallback via lookup_any_combo)
 */
static uint32_t lookup_shifted_combo(const uint32_t kana_a[CHORDIS_KANA_SLOTS],
                                     const uint32_t kana_b[CHORDIS_KANA_SLOTS],
                                     enum chordis_thumb_side side) {
    int idx = side + 1;
    uint32_t sa = kana_a[idx];
    uint32_t sb = kana_b[idx];
    uint32_t result;

    /* Priority 1: shifted_a + any face of b */
    if (sa != KANA_NONE) {
        for (int j = 0; j < CHORDIS_KANA_SLOTS; j++) {
            if (kana_b[j] != KANA_NONE) {
                result = lookup_char_combo(sa, kana_b[j]);
                if (result != KANA_NONE) return result;
            }
        }
    }

    /* Priority 2: any face of a + shifted_b */
    if (sb != KANA_NONE) {
        for (int i = 0; i < CHORDIS_KANA_SLOTS; i++) {
            if (kana_a[i] != KANA_NONE) {
                result = lookup_char_combo(kana_a[i], sb);
                if (result != KANA_NONE) return result;
            }
        }
    }

    /* Priority 3: any combination */
    return lookup_any_combo(kana_a, kana_b);
}

/* ── Forward declarations ─────────────────────────────────── */

static void output_kana(uint32_t kana_code, int64_t timestamp);
static void output_shifted_kana(const uint32_t kana[CHORDIS_KANA_SLOTS],
                                enum chordis_thumb_side side,
                                int64_t timestamp);
static void output_thumb_tap(int64_t timestamp);
static void start_char_timeout(void);
static void cancel_timeout(void);
static void timeout_handler(struct k_work *work);
static void start_watchdog(void);
static void cancel_watchdog(void);
static void watchdog_handler(struct k_work *work);

/* ── Initialisation ───────────────────────────────────────── */

void chordis_engine_init(void) {
    if (sm.timer_initialized) {
        return;
    }

    /* Read timing from DT if the config node exists */
#if DT_HAS_COMPAT_STATUS_OKAY(zmk_chord_input_config)
    timing.timeout_ms =
        DT_PROP(DT_INST(0, zmk_chord_input_config), timeout_ms);
    timing.sequential_threshold =
        DT_PROP(DT_INST(0, zmk_chord_input_config), sequential_threshold);
    timing.sequential_min_overlap_ms =
        DT_PROP(DT_INST(0, zmk_chord_input_config), sequential_min_overlap_ms);
    timing.default_tapping_term_ms =
        DT_PROP(DT_INST(0, zmk_chord_input_config), default_tapping_term_ms);
    timing.default_quick_tap_ms =
        DT_PROP(DT_INST(0, zmk_chord_input_config), default_quick_tap_ms);
    timing.default_require_prior_idle_ms =
        DT_PROP(DT_INST(0, zmk_chord_input_config), default_require_prior_idle_ms);
    timing.hold_after_partner_release_ms =
        DT_PROP(DT_INST(0, zmk_chord_input_config), hold_after_partner_release_ms);
#endif

    k_work_init_delayable(&sm.timeout_work, timeout_handler);
    k_work_init_delayable(&sm.watchdog_work, watchdog_handler);
    sm.state = CHORDIS_IDLE;
    sm.timer_initialized = true;

    LOG_DBG("nicola engine init: timeout=%d threshold=%d min_overlap=%d "
            "default_tt=%d default_qt=%d hold_after_partner_release=%d",
            timing.timeout_ms, timing.sequential_threshold,
            timing.sequential_min_overlap_ms,
            timing.default_tapping_term_ms,
            timing.default_quick_tap_ms,
            timing.hold_after_partner_release_ms);
}

void chordis_engine_set_globals(uint32_t default_tapping_term_ms,
                                uint32_t default_quick_tap_ms,
                                uint32_t hold_after_partner_release_ms) {
    if (default_tapping_term_ms != CHORDIS_KEEP) {
        timing.default_tapping_term_ms = default_tapping_term_ms;
    }
    if (default_quick_tap_ms != CHORDIS_KEEP) {
        timing.default_quick_tap_ms = default_quick_tap_ms;
    }
    if (hold_after_partner_release_ms != CHORDIS_KEEP) {
        timing.hold_after_partner_release_ms = hold_after_partner_release_ms;
    }
}

void chordis_engine_set_require_prior_idle_global(uint32_t default_require_prior_idle_ms) {
    if (default_require_prior_idle_ms != CHORDIS_KEEP) {
        timing.default_require_prior_idle_ms = default_require_prior_idle_ms;
    }
}

/* ── Character key events ─────────────────────────────────── */

/** Apply hold-tap config to a freshly allocated tracker and arm the timer.
 * Slice F: if a previous release of the same position lies within
 * quick_tap_ms, suppress hold for this press (repeat-typing protection). */
static void tracker_apply_hold(struct key_tracker *t,
                               const struct chordis_hold_config *hold) {
    if (!t || !hold) return;
    t->ht_configured = true;
    /* Inherit globals when the per-key value is the sentinel 0. */
    uint32_t tapping_term_ms = hold->tapping_term_ms != 0
                                   ? hold->tapping_term_ms
                                   : timing.default_tapping_term_ms;
    uint32_t quick_tap_ms    = hold->quick_tap_ms != 0
                                   ? hold->quick_tap_ms
                                   : timing.default_quick_tap_ms;
    uint32_t require_prior_idle_ms = hold->require_prior_idle_ms != 0
                                   ? hold->require_prior_idle_ms
                                   : timing.default_require_prior_idle_ms;
    uint32_t hold_after_partner_release_ms =
        hold->hold_after_partner_release_ms != 0
            ? hold->hold_after_partner_release_ms
            : timing.hold_after_partner_release_ms;
    /* require-prior-idle gate: if any key was released within the
     * configured window before this press, force tap (skip arming the
     * hold timer). Mirrors hrkp deny semantics — the tracker stays
     * allocated but loses its hold capability. */
    if (require_prior_idle_ms > 0 && last_key_release_ts >= 0 &&
        (t->pressed_ts - last_key_release_ts) < (int64_t)require_prior_idle_ms) {
        LOG_DBG("require_prior_idle suppress hold pos=%d gap=%lld < %d",
                t->position,
                (long long)(t->pressed_ts - last_key_release_ts),
                require_prior_idle_ms);
        return;
    }
    if (quick_tap_ms > 0) {
        int64_t last_release = quick_tap_lookup(t->position);
        if (last_release >= 0 &&
            (t->pressed_ts - last_release) < (int64_t)quick_tap_ms) {
            LOG_DBG("quick_tap suppress hold pos=%d gap=%lld < %d",
                    t->position,
                    (long long)(t->pressed_ts - last_release),
                    quick_tap_ms);
            /* Leave has_hold = false (set by tracker_alloc); do not arm timer. */
            return;
        }
    }
    t->has_hold        = true;
    t->tapping_term_ms = tapping_term_ms;
    t->quick_tap_ms    = quick_tap_ms;
    t->hold_after_partner_release_ms = hold_after_partner_release_ms;
    t->hold_binding    = hold->binding;
    /* Slice G: copy hrkp pointer/count. The pointer references storage owned
     * by the behavior config (lifetime: program), so a raw pointer is safe. */
    t->hrkp_positions       = hold->hold_required_positions;
    t->hrkp_positions_count = hold->hold_required_positions_count;
    t->hrkp_decided         = false;
    k_work_schedule(&t->tapping_term_timer, K_MSEC(tapping_term_ms));
}

/** Slice G: hrkp (hold-required-key-positions) gating. Called when a new
 * char-key arrives, BEFORE any state machine dispatch. Walks all HT-UNDECIDED
 * trackers that still have an undecided hrkp partner; the arriving position
 * is treated as their first partner. If the position is in the tracker's
 * required list, the tracker keeps has_hold; otherwise has_hold is stripped
 * (force tap), the tapping_term timer is cancelled, and the tracker degrades
 * to a plain combo candidate. Trackers without an hrkp list (count==0) are
 * unaffected. Trackers that have already decided are skipped (only the first
 * partner counts). The arriving key's own position is skipped (self). */
static void hrkp_evaluate_partner(uint32_t partner_position) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        struct key_tracker *t = &sm.chars[i];
        if (!t->in_use || !t->has_hold || t->resolution != TRK_UNDECIDED) {
            continue;
        }
        if (t->hrkp_positions_count == 0 || t->hrkp_decided) {
            continue;
        }
        if (t->position == partner_position) {
            continue;
        }
        bool allowed = false;
        for (uint32_t k = 0; k < t->hrkp_positions_count; k++) {
            if ((uint32_t)t->hrkp_positions[k] == partner_position) {
                allowed = true;
                break;
            }
        }
        t->hrkp_decided = true;
        if (!allowed) {
            LOG_DBG("hrkp deny: partner pos=%d strips has_hold from tracker pos=%d",
                    partner_position, t->position);
            t->has_hold = false;
            k_work_cancel_delayable(&t->tapping_term_timer);
        } else {
            LOG_DBG("hrkp allow: partner pos=%d keeps tracker pos=%d as HT",
                    partner_position, t->position);
        }
    }
}

/** Allocate + populate a tracker for the current incoming key. */
static struct key_tracker *tracker_alloc_for_key(
    const uint32_t kana[CHORDIS_KANA_SLOTS],
    struct zmk_behavior_binding_event event,
    const struct chordis_hold_config *hold,
    bool blocked_by_prior_plain) {
    struct key_tracker *t = tracker_alloc();
    if (!t) return NULL;
    memcpy(t->kana, kana, sizeof(t->kana));
    t->pressed_ts = event.timestamp;
    t->position   = event.position;
    t->blocked_by_prior_plain = blocked_by_prior_plain;
    tracker_apply_hold(t, hold);
    return t;
}

void chordis_on_char_pressed(const uint32_t kana[CHORDIS_KANA_SLOTS],
                            struct zmk_behavior_binding_event event,
                            const struct chordis_hold_config *hold) {
    chordis_engine_init();
    char_mark_pressed(event.position, event.timestamp);
    /* Snapshot before hrkp: this flag only models whether an older plain was
     * already active when the new HT arrived. hrkp may strip has_hold from
     * existing trackers, but it does not allocate the new key's tracker, so
     * the new key's own prior-plain relationship is fixed at press time. */
    bool blocked_by_prior_plain = (hold != NULL) ? tracker_has_prior_plain() : false;

    LOG_DBG("char_pressed pos=%d state=%d kana=[0x%04x,0x%04x,0x%04x,0x%04x,0x%04x] hold=%d",
            event.position, sm.state, kana[0], kana[1], kana[2], kana[3], kana[4], hold ? 1 : 0);

    /* Slice G: hrkp gating runs before state dispatch so any has_hold strip
     * lands before tracker_first_plain() / has_hold checks below. The new
     * key's own tracker is not yet allocated, so self-skip in the helper is
     * a defensive no-op. */
    hrkp_evaluate_partner(event.position);

    sm.char_held_count++;

    switch (sm.state) {

    case CHORDIS_IDLE:
        if (thumb_any_held()) {
            /* Thumb is still held after watchdog reset — re-enter THUMB_HELD.
             * Recompute active_thumb from currently-held thumbs using the
             * same newest-held rule as release-time promotion. This refreshes
             * tap_binding / position too, so a later release of the
             * now-promoted thumb fires its own tap binding (not a stale one
             * from a thumb released earlier). */
            enum chordis_thumb_side newest = (enum chordis_thumb_side)0;
            if (thumb_newest_held(&newest)) {
                thumb_promote_active(newest);
            }
            LOG_DBG("re-entering THUMB_HELD (thumb still held, side=%d)",
                    sm.active_thumb);
            output_shifted_kana(kana, sm.active_thumb, event.timestamp);
            sm.thumb_used = true;
            sm.state = CHORDIS_THUMB_HELD;
            start_watchdog();
        } else {
            /* Buffer this character, start timer, wait for thumb */
            tracker_alloc_for_key(kana, event, hold, blocked_by_prior_plain);
            start_char_timeout();
            sm.state = CHORDIS_CHAR_WAIT;
        }
        break;

    case CHORDIS_CHAR_WAIT: {
        cancel_timeout();

        struct key_tracker *t1 = tracker_first();
        struct key_tracker *t2 = tracker_second();

        if (sm.combo_pending) {
            /* 3rd char while combo pending — try chained combo first.
             * e.g. し+濁点 → しょ intermediate, then しょ+濁点 → じょ
             * We try all pairings: (1,2)+3, (1,3)+2, (2,3)+1 */
            uint32_t chain_result = KANA_NONE;

            /* Try combo(1,2) → intermediate → combo(intermediate, any face of 3) */
            uint32_t inter12 = lookup_any_combo(t1->kana, t2->kana);
            if (inter12 != KANA_NONE) {
                for (int k = 0; k < CHORDIS_KANA_SLOTS && chain_result == KANA_NONE; k++) {
                    if (kana[k] != KANA_NONE)
                        chain_result = lookup_char_combo(inter12, kana[k]);
                }
            }

            /* Try combo(1,3) → intermediate → combo(intermediate, any face of 2) */
            if (chain_result == KANA_NONE) {
                uint32_t inter13 = lookup_any_combo(t1->kana, kana);
                if (inter13 != KANA_NONE) {
                    for (int k = 0; k < CHORDIS_KANA_SLOTS && chain_result == KANA_NONE; k++) {
                        if (t2->kana[k] != KANA_NONE)
                            chain_result = lookup_char_combo(inter13, t2->kana[k]);
                    }
                }
            }

            /* Try combo(2,3) → intermediate → combo(intermediate, any face of 1) */
            if (chain_result == KANA_NONE) {
                uint32_t inter23 = lookup_any_combo(t2->kana, kana);
                if (inter23 != KANA_NONE) {
                    for (int k = 0; k < CHORDIS_KANA_SLOTS && chain_result == KANA_NONE; k++) {
                        if (t1->kana[k] != KANA_NONE)
                            chain_result = lookup_char_combo(inter23, t1->kana[k]);
                    }
                }
            }

            if (chain_result != KANA_NONE) {
                /* Chained 3-key combo resolved (e.g. じょ) */
                LOG_DBG("chained combo: → %d", chain_result);
                output_kana(chain_result, t1->pressed_ts);
                tracker_free(t1);
                tracker_free(t2);
                sm.combo_pending = false;
                sm.state = CHORDIS_IDLE;
            } else {
                /* No chain — flush buffered chars, buffer new char */
                uint32_t result = lookup_any_combo(t1->kana, t2->kana);
                if (result != KANA_NONE) {
                    output_kana(result, t1->pressed_ts);
                } else {
                    output_kana(t1->kana[0], t1->pressed_ts);
                    output_kana(t2->kana[0], t2->pressed_ts);
                }
                tracker_free(t1);
                tracker_free(t2);
                sm.combo_pending = false;
                tracker_alloc_for_key(kana, event, hold, blocked_by_prior_plain);
                start_char_timeout();
            }
            break;
        }

        uint32_t combo_result = KANA_NONE;
        if (combo_count > 0) {
            combo_result = lookup_any_combo(t1->kana, kana);
        }

        if (combo_result != KANA_NONE) {
            /* Direct combo found — buffer 2nd char and wait for a
             * potential 3rd key that could chain (e.g. し+る→しょ,
             * then しょ+濁点→じょ). */
            LOG_DBG("combo pending (direct): %d", combo_result);
            tracker_alloc_for_key(kana, event, hold, blocked_by_prior_plain);
            sm.combo_pending = true;
            start_char_timeout();
            /* Stay in CHAR_WAIT */
        } else if (combo_count > 0 &&
                   has_potential_chain(t1->kana, kana)) {
            /* No direct combo, but a 3rd key could form a chain
             * (e.g. る+濁点: combo(し,る)=しょ → combo(しょ,濁点)=じょ).
             * Buffer both and wait. */
            LOG_DBG("combo pending (potential chain)");
            tracker_alloc_for_key(kana, event, hold, blocked_by_prior_plain);
            sm.combo_pending = true;
            start_char_timeout();
            /* Stay in CHAR_WAIT */
        } else {
            /* No combo possible. Flush the buffered plain tracker (if any)
             * as unshifted, but PRESERVE any HT-UNDECIDED trackers — they
             * are independent mod-in-flight sidecars (Slice D-2 multi-mod).
             * Then buffer the new key. */
            struct key_tracker *plain = tracker_first_plain();
            if (plain) {
                output_kana(plain->kana[0], plain->pressed_ts);
                tracker_free(plain);
            }
            tracker_alloc_for_key(kana, event, hold, blocked_by_prior_plain);
            start_char_timeout();
            /* Stay in CHAR_WAIT */
        }
        break;
    }

    case CHORDIS_CHAR_THUMB_WAIT: {
        /* 2nd char arrived while char+thumb are buffered.
         * First check if char1+char2 form a shifted combo (3-key simultaneous).
         * If not, fall back to d1/d2 partition (spec 5.4.2).
         * Shift source is the frozen buffered_thumb_side — immune to any
         * active_thumb promotion that happened since this state was entered. */
        cancel_timeout();

        struct key_tracker *t1 = tracker_first();
        enum chordis_thumb_side bside = sm.buffered_thumb_side;

        uint32_t combo_result = KANA_NONE;
        if (combo_count > 0) {
            combo_result = lookup_shifted_combo(t1->kana, kana, bside);
        }

        if (combo_result != KANA_NONE) {
            LOG_DBG("CHAR_THUMB_WAIT combo: → %d", combo_result);
            output_kana(combo_result, t1->pressed_ts);
            tracker_free(t1);
            sm.thumb_used = true;
            sm.has_buffered_thumb = false;
            sm.state = CHORDIS_THUMB_HELD;
            start_watchdog();
        } else {
            int64_t d1 = sm.thumb_timestamp - t1->pressed_ts;
            int64_t d2 = event.timestamp - sm.thumb_timestamp;

            LOG_DBG("5.4.2 partition: d1=%lld d2=%lld", d1, d2);

            if (d1 <= d2) {
                /* char1 + thumb = shifted, char2 → new CHAR_WAIT.
                 * Buffered interaction resolves here — clear the captured
                 * side so the next interaction uses active_thumb afresh. */
                output_shifted_kana(t1->kana, bside, t1->pressed_ts);
                tracker_free(t1);
                tracker_alloc_for_key(kana, event, hold, blocked_by_prior_plain);
                start_char_timeout();
                sm.has_buffered_thumb = false;
                sm.state = CHORDIS_CHAR_WAIT;
            } else {
                /* char1 = unshifted, char2 + thumb = shifted */
                output_kana(t1->kana[0], t1->pressed_ts);
                tracker_free(t1);
                output_shifted_kana(kana, bside, event.timestamp);
                sm.thumb_used = true;
                sm.has_buffered_thumb = false;
                sm.state = CHORDIS_THUMB_HELD;
                start_watchdog();
            }
        }
        break;
    }

    case CHORDIS_THUMB_HELD: {
        /* Thumb is held — check if any face of this key is a combo candidate */
        if (combo_count > 0 && is_any_combo_candidate(kana)) {
            /* Buffer this char and wait for a combo partner */
            LOG_DBG("THUMB_HELD: buffering combo candidate");
            tracker_alloc_for_key(kana, event, hold, blocked_by_prior_plain);
            sm.thumb_used = true;
            cancel_watchdog();
            start_char_timeout();
            sm.buffered_thumb_side = sm.active_thumb;
            sm.has_buffered_thumb  = true;
            sm.state = CHORDIS_THUMB_CHAR_WAIT;
        } else {
            /* Not a combo candidate — output shifted kana immediately */
            output_shifted_kana(kana, sm.active_thumb, event.timestamp);
            sm.thumb_used = true;
            start_watchdog(); /* reset: 1s from last char press */
        }
        break;
    }

    case CHORDIS_THUMB_CHAR_WAIT: {
        /* 2nd char while thumb+char buffered — check for combo.
         * Frozen shift source from the buffered interaction. */
        cancel_timeout();
        struct key_tracker *t1 = tracker_first();
        enum chordis_thumb_side bside = sm.buffered_thumb_side;
        uint32_t combo_result = lookup_shifted_combo(t1->kana, kana, bside);

        if (combo_result != KANA_NONE) {
            LOG_DBG("shifted combo: → %d", combo_result);
            output_kana(combo_result, t1->pressed_ts);
        } else {
            /* No combo — output buffered shifted, then new shifted.
             * The new char is emitted with the buffered side to preserve
             * the existing semantic (it belonged to the same thumb-active
             * episode). Any subsequent char in THUMB_HELD will use the
             * current active_thumb. */
            output_shifted_kana(t1->kana, bside, t1->pressed_ts);
            output_shifted_kana(kana, bside, event.timestamp);
        }
        tracker_free(t1);
        sm.has_buffered_thumb = false;
        sm.state = CHORDIS_THUMB_HELD;
        start_watchdog();
        break;
    }
    }
}

void chordis_on_char_released(struct zmk_behavior_binding_event event) {
    LOG_DBG("char_released pos=%d state=%d", event.position, sm.state);

    /* Track most recent release for require-prior-idle-ms gating on the
     * NEXT press. */
    last_key_release_ts = event.timestamp;

    if (sm.char_held_count > 0) sm.char_held_count--;
    char_mark_released(event.position);

    /* Slice F: record release timestamp for HT-configured positions so the
     * next press can apply quick_tap_ms suppression. */
    {
        struct key_tracker *t_ht = tracker_find_by_position(event.position);
        if (t_ht && t_ht->ht_configured) {
            quick_tap_record(event.position, event.timestamp);
        }
    }

    /* Hold-tap: if this position has a HOLD-resolved tracker, release the
     * hold action and free the tracker. Bypass normal state-machine flow. */
    {
        struct key_tracker *th = tracker_find_by_position(event.position);
        if (th && th->resolution == TRK_HOLD) {
            LOG_DBG("releasing hold at pos=%d", event.position);
            zmk_behavior_invoke_binding(&th->hold_binding, event, false);
            tracker_free(th);
            if (tracker_count() == 0 && !thumb_any_held()) {
                cancel_timeout();
                cancel_watchdog();
                sm.combo_pending = false;
                sm.state = CHORDIS_IDLE;
            }
            /* Deferred auto-off dismissal: if a previous shortcut press armed
             * pending_auto_off and no hold-tap modifiers remain, the user has
             * fully released the chord — now is the safe moment to deactivate
             * the nicola layer and emit LANG2 with no modifier collision. */
            if (chordis_pending_auto_off_layer >= 0 && !chordis_is_hold_active()) {
                int layer = chordis_pending_auto_off_layer;
                chordis_pending_auto_off_layer = -1;
                chordis_active_layer = -1;
                LOG_DBG("deferred auto-off: deactivate layer=%d + LANG2", layer);
                zmk_keymap_layer_deactivate(layer);
                raise_zmk_keycode_state_changed_from_encoded(LANG2, true, event.timestamp);
                raise_zmk_keycode_state_changed_from_encoded(LANG2, false, event.timestamp);
            }
            return;
        }
    }

    /* CHAR_WAIT + combo_pending: a combo key was released.
     * Default: resolve immediately — release means "done participating."
     *
     * Slice D-1 hold-after-partner-release balanced exception: if
     * releasing a plain (non-HT) tracker while another tracker is
     * HT-UNDECIDED, we don't yet know whether this is a combo or a held
     * mod. Schedule a hold_commit_timer (hold_after_partner_release_ms);
     * if HT releases within the window,
     * combo wins; if not, HT becomes HOLD and the plain is dropped
     * (passthrough placeholder until base-layer binding plumbing). */
    if (sm.state == CHORDIS_CHAR_WAIT && sm.combo_pending &&
        tracker_find_by_position(event.position) != NULL) {

        struct key_tracker *released_t = tracker_find_by_position(event.position);
        struct key_tracker *ht_undecided = find_ht_undecided_for_partner(released_t, released_t);

        if (ht_undecided != NULL && !released_t->has_hold) {
            released_t->awaiting_hold_commit = true;
            released_t->released_ts     = event.timestamp;
            uint32_t commit_delay_ms =
                max_hold_commit_delay_for_pending_hts(released_t, released_t);
            LOG_DBG("plain release while HT undecided pos=%d → schedule hold-commit %dms",
                    released_t->position, commit_delay_ms);
            released_t->hold_commit_delay_ms = commit_delay_ms;
            k_work_schedule(&released_t->hold_commit_timer, K_MSEC(commit_delay_ms));
            return;
        }

        if (!released_t->has_hold && find_ht_undecided(released_t) != NULL) {
            LOG_DBG("combo pending: plain pos=%d was already down before HT arrived; "
                    "emit unshifted kana on release",
                    released_t->position);
            output_kana(released_t->kana[0], released_t->pressed_ts);
            tracker_free(released_t);
            sm.combo_pending = false;
            if (tracker_count_active_chars() == 0 && !thumb_any_held()) {
                cancel_timeout();
                sm.state = CHORDIS_IDLE;
            } else {
                sm.state = CHORDIS_CHAR_WAIT;
            }
            return;
        }

        cancel_timeout();
        struct key_tracker *t1 = tracker_first();
        struct key_tracker *t2 = tracker_second();
        uint32_t result = lookup_any_combo(t1->kana, t2->kana);
        if (result != KANA_NONE) {
            output_kana(result, t1->pressed_ts);
        } else {
            output_kana(t1->kana[0], t1->pressed_ts);
            output_kana(t2->kana[0], t2->pressed_ts);
        }
        tracker_free(t1);
        tracker_free(t2);
        sm.combo_pending = false;
        sm.state = CHORDIS_IDLE;
        return;
    }

    /* Option A deferred resolution: a non-HT tracker released while an
     * HT-UNDECIDED tracker exists and no combo is pending. The user's intent
     * is ambiguous until the HT resolves:
     *   - HT released before tapping_term → both taps → both kana
     *   - HT held past tapping_term → HOLD → plain emits via base layer
     * Mark the plain as awaiting_hold_commit (no separate timer — tapping_term
     * IS the deadline). Resolution happens in:
     *   (a) tapping_term_handler → flush_plain_trackers_via_base_layer
     *   (b) HT tap-release below → flush deferred plains as kana */
    if (sm.state == CHORDIS_CHAR_WAIT && !sm.combo_pending) {
        struct key_tracker *rel = tracker_find_by_position(event.position);
        if (rel && !rel->has_hold && find_ht_undecided(rel) != NULL) {
            LOG_DBG("defer plain pos=%d until HT resolves", rel->position);
            rel->awaiting_hold_commit = true;
            rel->released_ts     = event.timestamp;
            return;
        }
    }

    /* CHAR_WAIT (single char, no combo_pending): release-driven output
     * for combo candidates and hold-tap keys. Non-combo characters use
     * the normal timeout so that a thumb arriving after release can still
     * shift. For hold-tap keys we must output on release because the
     * char_timeout is extended indefinitely while hold is pending. */
    if (sm.state == CHORDIS_CHAR_WAIT) {
        struct key_tracker *t = tracker_find_by_position(event.position);
        if (t && (t->has_hold ||
                  (combo_count > 0 && is_any_combo_candidate(t->kana)))) {
            bool was_ht = t->ht_configured;
            output_kana(t->kana[0], t->pressed_ts);
            tracker_free(t);

            /* Option A recovery: the HT was released before tapping_term
             * (it emitted its tap kana above). Any deferred plain trackers
             * should also emit as kana (both are taps). */
            if (was_ht) {
                for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
                    struct key_tracker *p = &sm.chars[i];
                    if (p->in_use && p->awaiting_hold_commit) {
                        p->awaiting_hold_commit = false;
                        LOG_DBG("deferred plain pos=%d → kana (HT tapped)", p->position);
                        output_kana(p->kana[0], p->pressed_ts);
                        tracker_free(p);
                    }
                }
            }

            /* D-2: only return to IDLE if no other trackers remain (e.g.
             * other HT-UNDECIDED sidecars from a multi-mod sequence). */
            if (tracker_count() == 0) {
                cancel_timeout();
                sm.state = CHORDIS_IDLE;
            }
            return;
        }
    }

    /* THUMB_CHAR_WAIT: char released while thumb+char buffered → commit shifted */
    if (sm.state == CHORDIS_THUMB_CHAR_WAIT) {
        struct key_tracker *t = tracker_find_by_position(event.position);
        if (t) {
            cancel_timeout();
            LOG_DBG("char released in THUMB_CHAR_WAIT: commit shifted");
            output_shifted_kana(t->kana, sm.buffered_thumb_side, t->pressed_ts);
            tracker_free(t);
            sm.has_buffered_thumb = false;
            sm.state = CHORDIS_THUMB_HELD;
            start_watchdog();
            return;
        }
    }

    if (sm.state != CHORDIS_CHAR_THUMB_WAIT) {
        return;
    }

    /* Only the buffered char's release is meaningful */
    struct key_tracker *tch = tracker_find_by_position(event.position);
    if (!tch) {
        return;
    }

    /* Sequential typing detection (spec 5.5.2).
     * t1 = char ON → thumb ON
     * t2 = thumb ON → char OFF (overlap)
     * If t2 is very short or t1 >> t2, the user typed sequentially
     * (char first, then thumb for space), not simultaneously. */
    int64_t t1 = sm.thumb_timestamp - tch->pressed_ts;
    int64_t t2 = event.timestamp - sm.thumb_timestamp;

    LOG_DBG("5.5.2 sequential check: t1=%lld t2=%lld thresh=%d min=%d",
            t1, t2, timing.sequential_threshold, timing.sequential_min_overlap_ms);

    bool sequential = false;
    if (t2 < timing.sequential_min_overlap_ms) {
        /* Overlap too short — always sequential */
        sequential = true;
    } else if (t1 > (int64_t)t2 * timing.sequential_threshold) {
        /* t1 >> t2 — sequential */
        sequential = true;
    }

    if (sequential) {
        cancel_timeout();
        LOG_DBG("5.5.2 sequential: char unshifted, thumb to THUMB_HELD");
        output_kana(tch->kana[0], tch->pressed_ts);
        tracker_free(tch);
        sm.thumb_used = false;
        sm.state = CHORDIS_THUMB_HELD;
        start_watchdog();
    }
}

/* ── Thumb key events ─────────────────────────────────────── */

void chordis_on_thumb_pressed(enum chordis_thumb_side side,
                             const struct zmk_behavior_binding *tap,
                             struct zmk_behavior_binding_event event) {
    chordis_engine_init();

    LOG_DBG("thumb_pressed side=%d state=%d ts=%lld", side, sm.state, event.timestamp);

    /* Always record the physical press so overlap tracking and promotion
     * see every thumb, even when the state-machine branch below ignores
     * second/later thumbs for logical purposes. */
    thumb_mark_pressed(side, tap, event.position, event.timestamp);

    switch (sm.state) {

    case CHORDIS_IDLE:
        /* Thumb down — enter shift mode */
        sm.active_thumb = side;
        sm.thumb_timestamp = event.timestamp;
        sm.thumb_used = false;
        sm.thumb_tap_binding = *tap;
        sm.thumb_position = event.position;
        sm.state = CHORDIS_THUMB_HELD;
        start_watchdog();
        break;

    case CHORDIS_CHAR_WAIT: {
        /* Slice E: hold extension may have kept CHAR_WAIT alive past
         * timeout_ms via an HT-UNDECIDED tracker. If the oldest tracker has
         * elapsed beyond NICOLA's simultaneous-press window, the user clearly
         * is NOT trying to shift it — flush the stale char(s) as unshifted
         * and start a fresh THUMB_HELD instead of entering CHAR_THUMB_WAIT. */
        struct key_tracker *t_old = tracker_first();
        if (t_old != NULL &&
            (event.timestamp - t_old->pressed_ts) >= (int64_t)timing.timeout_ms) {
            LOG_DBG("thumb arrived after timeout_ms (elapsed=%lld) — flush HT, fresh THUMB_HELD",
                    (long long)(event.timestamp - t_old->pressed_ts));
            cancel_timeout();
            /* Release any TRK_HOLD-resolved hold bindings first so their HID
             * modifier (e.g. LCTRL from a home-row-mod) does not get smeared
             * onto the kana romaji we are about to emit for the remaining
             * trackers. Without this, a thumb arriving long after an
             * already-resolved hold-tap would emit something like "C-h C-i"
             * for a buffered KANA_HI tracker. */
            release_held_trackers(event.timestamp);
            /* Flush all in-use trackers as unshifted kana, oldest first.
             * tracker_free cancels per-tracker tapping_term / hold_commit timers. */
            for (;;) {
                struct key_tracker *t = tracker_first();
                if (t == NULL) break;
                output_kana(t->kana[0], t->pressed_ts);
                tracker_free(t);
            }
            sm.combo_pending = false;
            sm.active_thumb       = side;
            sm.thumb_tap_binding  = *tap;
            sm.thumb_position     = event.position;
            sm.thumb_timestamp    = event.timestamp;
            sm.thumb_used         = false;
            sm.state              = CHORDIS_THUMB_HELD;
            start_watchdog();
            break;
        }

        cancel_timeout();
        sm.active_thumb = side;
        sm.thumb_tap_binding = *tap;
        sm.thumb_position = event.position;
        sm.thumb_timestamp = event.timestamp;
        if (sm.combo_pending) {
            /* Two chars buffered + thumb → resolve shifted combo */
            struct key_tracker *t1 = tracker_first();
            struct key_tracker *t2 = tracker_second();
            uint32_t result = lookup_shifted_combo(t1->kana, t2->kana, side);
            if (result == KANA_NONE) {
                result = lookup_any_combo(t1->kana, t2->kana);
            }
            if (result != KANA_NONE) {
                LOG_DBG("combo+thumb: → %d", result);
                output_kana(result, t1->pressed_ts);
            } else {
                /* No combo — flush both as shifted */
                output_shifted_kana(t1->kana, side, t1->pressed_ts);
                output_shifted_kana(t2->kana, side, t2->pressed_ts);
            }
            tracker_free(t1);
            tracker_free(t2);
            sm.combo_pending = false;
            sm.thumb_used = true;
            sm.state = CHORDIS_THUMB_HELD;
            start_watchdog();
        } else {
            /* Single char buffered + thumb → enter intermediate state.
             * Defer output until 2nd char, char release, or timeout
             * (5.4.2/5.5.2). */
            start_char_timeout();
            sm.buffered_thumb_side = side;
            sm.has_buffered_thumb  = true;
            sm.state = CHORDIS_CHAR_THUMB_WAIT;
        }
        break;
    }

    case CHORDIS_CHAR_THUMB_WAIT:
        /* Already in char+thumb wait — ignore second thumb */
        break;

    case CHORDIS_THUMB_HELD:
        /* Already holding a thumb — ignore second thumb press */
        break;

    case CHORDIS_THUMB_CHAR_WAIT:
        /* Thumb+char buffered, another thumb arrives — ignore */
        break;
    }
}

void chordis_on_thumb_released(enum chordis_thumb_side side,
                              struct zmk_behavior_binding_event event) {
    LOG_DBG("thumb_released side=%d state=%d thumb_used=%d",
            side, sm.state, sm.thumb_used);

    /* Track most recent release for require-prior-idle-ms gating. */
    last_key_release_ts = event.timestamp;

    /* Always drop this thumb from physical tracking, regardless of whether
     * it is the currently-active one. Without this, a non-active thumb
     * release would leak stale "held" state into promotion and any-held
     * checks. */
    thumb_mark_released(side);

    /* If the released thumb is not the logical active one, the buffered
     * interaction (if any) keeps its captured side and the logical state
     * machine is unaffected. */
    if (side != sm.active_thumb) {
        return;
    }

    /* The active thumb was released. If another thumb is still physically
     * held, promote the newest one so future input has a valid shift
     * source. Promotion deliberately does NOT touch buffered_thumb_side —
     * any in-flight CHAR_THUMB_WAIT / THUMB_CHAR_WAIT continues to resolve
     * against the side it started with. thumb_used stays as-is (global
     * per-episode), so a freshly-promoted thumb inherits used-state and
     * will not emit a tap on its eventual release. */
    enum chordis_thumb_side promoted;
    bool has_replacement = thumb_newest_held(&promoted);

    switch (sm.state) {
    case CHORDIS_CHAR_THUMB_WAIT: {
        /* Thumb released while char+thumb buffered — commit as shifted
         * using the frozen buffered side. */
        cancel_timeout();
        struct key_tracker *t1 = tracker_first();
        if (t1) {
            output_shifted_kana(t1->kana, sm.buffered_thumb_side, t1->pressed_ts);
            tracker_free(t1);
        }
        sm.has_buffered_thumb = false;
        if (has_replacement) {
            /* Another thumb still held — stay in THUMB_HELD with the
             * promoted thumb as the active shift source for further input. */
            thumb_promote_active(promoted);
            sm.thumb_used = true;
            sm.state = CHORDIS_THUMB_HELD;
            start_watchdog();
        } else {
            sm.state = CHORDIS_IDLE;
        }
        break;
    }

    case CHORDIS_THUMB_CHAR_WAIT: {
        /* Thumb released while waiting for combo partner — flush buffered */
        cancel_timeout();
        struct key_tracker *t1 = tracker_first();
        if (t1) {
            output_shifted_kana(t1->kana, sm.buffered_thumb_side, t1->pressed_ts);
            tracker_free(t1);
        }
        sm.has_buffered_thumb = false;
        if (has_replacement) {
            thumb_promote_active(promoted);
            sm.thumb_used = true;
            sm.state = CHORDIS_THUMB_HELD;
            start_watchdog();
        } else {
            sm.state = CHORDIS_IDLE;
        }
        break;
    }

    case CHORDIS_THUMB_HELD:
        if (has_replacement) {
            /* Another thumb is still physically held — promote it. The
             * watchdog keeps running; no tap fires because this release is
             * not the last thumb. */
            thumb_promote_active(promoted);
        } else {
            cancel_watchdog();
            if (!sm.thumb_used) {
                output_thumb_tap(event.timestamp);
            }
            sm.state = CHORDIS_IDLE;
        }
        break;

    default:
        /* IDLE / CHAR_WAIT — no buffered thumb interaction to disturb.
         * Physical tracking was already updated above; if the active thumb
         * was this one but sm.state is not thumb-related (e.g. we were
         * forced to IDLE by a watchdog or flush while the thumb stayed
         * physically down), just promote for consistency. */
        if (has_replacement) {
            thumb_promote_active(promoted);
        }
        break;
    }
}

/* ── Flush (for mode switch) ──────────────────────────────── */

void chordis_engine_flush(int64_t timestamp) {
    LOG_DBG("flush state=%d", sm.state);

    cancel_timeout();
    cancel_watchdog();

    /* Release any TRK_HOLD-resolved hold bindings BEFORE the normal flush
     * branch runs, and free those trackers so the CHAR_WAIT kana-flush path
     * below does not spuriously output their kana as unshifted.
     *
     * Without this, a flush (e.g. from auto-off while the user is holding a
     * home-row-mod key combined with a shortcut) would leave the HID modifier
     * stuck down: tracker_clear_all() would zero the tracker and the physical
     * key release, arriving after the layer is off, would hit the base-layer
     * binding instead of routing through chordis_on_char_released. */
    release_held_trackers(timestamp);

    switch (sm.state) {
    case CHORDIS_CHAR_WAIT: {
        struct key_tracker *t1 = tracker_first();
        if (sm.combo_pending) {
            struct key_tracker *t2 = tracker_second();
            uint32_t result = lookup_any_combo(t1->kana, t2->kana);
            if (result != KANA_NONE) {
                output_kana(result, t1->pressed_ts);
            } else {
                output_kana(t1->kana[0], t1->pressed_ts);
                output_kana(t2->kana[0], t2->pressed_ts);
            }
            sm.combo_pending = false;
        } else if (t1) {
            output_kana(t1->kana[0], timestamp);
        }
        break;
    }
    case CHORDIS_CHAR_THUMB_WAIT: {
        /* Flush during char+thumb wait — output as shifted using the frozen
         * buffered side. */
        struct key_tracker *t1 = tracker_first();
        if (t1) {
            output_shifted_kana(t1->kana, sm.buffered_thumb_side, t1->pressed_ts);
        }
        break;
    }
    case CHORDIS_THUMB_CHAR_WAIT: {
        /* Flush during thumb+char combo wait — output buffered shifted */
        struct key_tracker *t1 = tracker_first();
        if (t1) {
            output_shifted_kana(t1->kana, sm.buffered_thumb_side, t1->pressed_ts);
        }
        break;
    }
    case CHORDIS_THUMB_HELD:
        /* Flush is an explicit abort (mode-switch / auto-off). If the episode
         * was unused, emit the tap regardless of whether the thumb is still
         * physically held — the flush itself ends the thumb-active episode.
         * The tap binding in sm.thumb_tap_binding corresponds to the current
         * active_thumb, which is always valid while in THUMB_HELD. */
        if (!sm.thumb_used) {
            output_thumb_tap(timestamp);
        }
        break;
    case CHORDIS_IDLE:
        break;
    }

    tracker_clear_all();
    sm.combo_pending = false;
    sm.has_buffered_thumb = false;
    sm.char_held_count = 0;
    char_clear_all_physical();
    sm.state = CHORDIS_IDLE;
}

/* ── Timer (CHAR_WAIT and CHAR_THUMB_WAIT) ───────────────── */

static void start_char_timeout(void) {
    LOG_DBG("start_char_timeout timeout_ms=%d", timing.timeout_ms);
    k_work_schedule(&sm.timeout_work, K_MSEC(timing.timeout_ms));
}

static void cancel_timeout(void) {
    k_work_cancel_delayable(&sm.timeout_work);
}

static void timeout_handler(struct k_work *work) {
    LOG_DBG("timeout state=%d ts=%lld", sm.state, k_uptime_get());

    switch (sm.state) {
    case CHORDIS_CHAR_WAIT: {
        struct key_tracker *t1 = tracker_first();
        /* Hold-tap: while any tracker has a hold pending or active, keep
         * CHAR_WAIT alive so the per-tracker tapping_term_timer can drive
         * resolution and release events route through the early-exit path. */
        bool hold_in_flight = false;
        for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
            if (sm.chars[i].in_use && sm.chars[i].has_hold &&
                sm.chars[i].resolution != TRK_TAP_PENDING) {
                hold_in_flight = true;
                break;
            }
        }
        if (hold_in_flight) {
            LOG_DBG("hold-tap in flight — extending CHAR_WAIT");
            k_work_schedule(&sm.timeout_work, K_MSEC(THUMB_WATCHDOG_MS));
            break;
        }
        if (sm.char_held_count > 0 &&
            combo_count > 0 &&
            t1 && is_any_combo_candidate(t1->kana)) {
            /* Combo candidate still held — keep waiting (hold mode).
             * Applies to both single-char and combo_pending states:
             * single-char waits for combo partner, combo_pending waits
             * for potential 3rd key (chained combo). */
            LOG_DBG("combo candidate held — restarting timeout");
            k_work_schedule(&sm.timeout_work, K_MSEC(THUMB_WATCHDOG_MS));
            break;
        }
        if (sm.combo_pending) {
            struct key_tracker *t2 = tracker_second();
            /* Keys released, no more input — resolve combo or flush both */
            uint32_t result = lookup_any_combo(t1->kana, t2->kana);
            if (result != KANA_NONE) {
                output_kana(result, t1->pressed_ts);
            } else {
                output_kana(t1->kana[0], t1->pressed_ts);
                output_kana(t2->kana[0], t2->pressed_ts);
            }
            tracker_free(t2);
            sm.combo_pending = false;
        } else if (t1) {
            output_kana(t1->kana[0], t1->pressed_ts);
        }
        if (t1) tracker_free(t1);
        sm.state = CHORDIS_IDLE;
        break;
    }

    case CHORDIS_CHAR_THUMB_WAIT: {
        /* No 2nd char arrived in time → commit char+thumb as shifted,
         * transition to THUMB_HELD for further chars (NICOLA spec 5.4).
         * Use the frozen buffered side to resolve the buffered char. */
        struct key_tracker *t1 = tracker_first();
        if (t1) {
            output_shifted_kana(t1->kana, sm.buffered_thumb_side, t1->pressed_ts);
            tracker_free(t1);
        }
        sm.thumb_used = true;
        sm.has_buffered_thumb = false;
        sm.state = CHORDIS_THUMB_HELD;
        start_watchdog();
        break;
    }

    case CHORDIS_THUMB_CHAR_WAIT: {
        struct key_tracker *t1 = tracker_first();
        if (sm.char_held_count > 0 &&
            combo_count > 0 &&
            t1 && is_any_combo_candidate(t1->kana)) {
            /* Combo candidate still held — keep waiting for partner */
            LOG_DBG("THUMB_CHAR_WAIT: combo candidate held — restarting");
            k_work_schedule(&sm.timeout_work, K_MSEC(THUMB_WATCHDOG_MS));
            break;
        }
        /* No combo partner arrived → output buffered shifted kana,
         * return to THUMB_HELD */
        LOG_DBG("THUMB_CHAR_WAIT timeout → output shifted, back to THUMB_HELD");
        if (t1) {
            output_shifted_kana(t1->kana, sm.buffered_thumb_side, t1->pressed_ts);
            tracker_free(t1);
        }
        sm.has_buffered_thumb = false;
        sm.state = CHORDIS_THUMB_HELD;
        start_watchdog();
        break;
    }

    default:
        break;
    }
}

/* ── Watchdog (THUMB_HELD recovery for BLE event drops) ──── */

static void start_watchdog(void) {
    LOG_DBG("start_watchdog %dms", THUMB_WATCHDOG_MS);
    k_work_schedule(&sm.watchdog_work, K_MSEC(THUMB_WATCHDOG_MS));
}

static void cancel_watchdog(void) {
    k_work_cancel_delayable(&sm.watchdog_work);
}

/* ── Hold-tap support ─────────────────────────────────────── */

bool chordis_is_hold_active(void) {
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        if (sm.chars[i].in_use && sm.chars[i].resolution == TRK_HOLD) {
            return true;
        }
    }
    return false;
}

static void tapping_term_handler(struct k_work *work) {
    /* Linear scan — small N (CHORDIS_MAX_TRACKERS) */
    struct key_tracker *t = NULL;
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        if (sm.chars[i].in_use && sm.chars[i].has_hold &&
            (struct k_work *)&sm.chars[i].tapping_term_timer == work) {
            t = &sm.chars[i];
            break;
        }
    }
    if (!t || t->resolution != TRK_UNDECIDED) {
        return;
    }
    int64_t tt_ts = t->pressed_ts + t->tapping_term_ms;
    if (t->blocked_by_prior_plain &&
        tracker_has_active_prior_plain_before(t)) {
        LOG_DBG("tapping_term fired: pos=%d blocked by prior plain → stay UNDECIDED",
                t->position);
        return;
    }
    int64_t latest_partner_grace_deadline = -1;
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        struct key_tracker *plain = &sm.chars[i];
        if (!plain->in_use || !plain->awaiting_hold_commit) {
            continue;
        }
        int64_t grace_deadline =
            plain->released_ts + (int64_t)t->hold_after_partner_release_ms;
        if (grace_deadline > latest_partner_grace_deadline) {
            latest_partner_grace_deadline = grace_deadline;
        }
    }
    if (latest_partner_grace_deadline > tt_ts) {
        LOG_DBG("tapping_term fired: pos=%d deferred by partner grace until %lld",
                t->position, (long long)latest_partner_grace_deadline);
        return;
    }
    LOG_DBG("tapping_term fired: pos=%d → TRK_HOLD", t->position);
    t->resolution = TRK_HOLD;
    struct zmk_behavior_binding_event ev = {
        .position  = t->position,
        .timestamp = tt_ts,
    };
    zmk_behavior_invoke_binding(&t->hold_binding, ev, true);

    /* Any plain trackers buffered alongside the HT must now be delivered as
     * modifier+keystroke via the base layer, not later as kana romaji. The
     * user's pressed-while-HT-pending key is unambiguously a partner for the
     * modifier (e.g. C-x while holding cdis_ng_i for RCTRL). */
    flush_plain_trackers_via_base_layer(ev.timestamp);

    /* If no char trackers remain after promotion, drop back to IDLE — the
     * state machine no longer has any pending kana to resolve, and the
     * HOLD tracker itself is invisible to the combo iterators. Without
     * this reset, sm.state would stay CHAR_WAIT and the next press would
     * dereference a NULL t1 from tracker_first(). */
    if (tracker_count_active_chars() == 0 && !thumb_any_held()) {
        cancel_timeout();
        sm.combo_pending = false;
        sm.state = CHORDIS_IDLE;
    }
}

/* Slice D-1/D-2: hold_commit_timer fired for an awaiting_hold_commit plain tracker.
 *
 * The plain combo partner B was released hold_after_partner_release_ms ago and at least
 * one HT-UNDECIDED tracker has not been released since. Resolve ALL HT
 * trackers still in TRK_UNDECIDED to HOLD (multi-mod, e.g. Cmd+Shift+B),
 * then emit press+release for the plain key's base-layer binding so that
 * B is delivered as a normal keystroke under the held modifiers.
 */
static void hold_commit_handler(struct k_work *work) {
    struct key_tracker *plain = NULL;
    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        if (sm.chars[i].in_use && sm.chars[i].awaiting_hold_commit &&
            (struct k_work *)&sm.chars[i].hold_commit_timer == work) {
            plain = &sm.chars[i];
            break;
        }
    }
    if (!plain) {
        return;
    }
    plain->awaiting_hold_commit = false;

    int64_t hold_ts = plain->released_ts + plain->hold_commit_delay_ms;
    int resolved = 0;

    for (int i = 0; i < CHORDIS_MAX_TRACKERS; i++) {
        struct key_tracker *t = &sm.chars[i];
        if (!t->in_use || !t->has_hold || t->resolution != TRK_UNDECIDED) {
            continue;
        }
        LOG_DBG("hold-commit pos=%d: HT pos=%d → HOLD", plain->position, t->position);
        k_work_cancel_delayable(&t->tapping_term_timer);
        t->resolution = TRK_HOLD;
        struct zmk_behavior_binding_event ev = {
            .position  = t->position,
            .timestamp = hold_ts,
        };
        zmk_behavior_invoke_binding(&t->hold_binding, ev, true);
        resolved++;
    }

    if (resolved == 0) {
        LOG_DBG("hold-commit pos=%d: no HT undecided — drop plain", plain->position);
    } else {
        /* D-1.5: emit the plain key's base-layer binding press+release so the
         * unmodified character is delivered while the resolved HT modifiers
         * are still active. */
        const struct zmk_behavior_binding *base = zmk_keymap_get_layer_binding_at_idx(
            zmk_keymap_layer_default(), plain->position);
        if (base != NULL && base->behavior_dev != NULL) {
            struct zmk_behavior_binding base_copy = *base;
            struct zmk_behavior_binding_event ev = {
                .position  = plain->position,
                .timestamp = hold_ts,
            };
            zmk_behavior_invoke_binding(&base_copy, ev, true);
            zmk_behavior_invoke_binding(&base_copy, ev, false);
        } else {
            LOG_DBG("hold-commit pos=%d: no base-layer binding for plain", plain->position);
        }
    }

    /* Drop the plain tracker */
    tracker_free(plain);
    sm.combo_pending = false;
    /* HT trackers are still tracked until released, but are now in TRK_HOLD
     * and invisible to combo iterators. If no char trackers remain we must
     * drop back to IDLE so the next press allocates fresh state instead of
     * dereferencing a NULL t1 from a stale CHAR_WAIT. */
    if (tracker_count_active_chars() == 0 && !thumb_any_held()) {
        cancel_timeout();
        sm.state = CHORDIS_IDLE;
    }
}

static void watchdog_handler(struct k_work *work) {
    if (sm.state != CHORDIS_THUMB_HELD) {
        return;
    }

    LOG_WRN("watchdog: THUMB_HELD timeout — resetting to IDLE (BLE drop?)");

    /* Silent reset — no output. If any thumb is still physically held,
     * the next char_pressed will re-enter THUMB_HELD via thumb_any_held()
     * and promote the newest held thumb to active. */
    sm.state = CHORDIS_IDLE;
}

/* ── Romaji output table ──────────────────────────────────── */

/*
 * Each kana ID maps to a romaji string (up to 3 characters).
 * The OS IME in romaji input mode converts these to kana.
 *
 * Convention: nihon-shiki (si, ti, tu, hu) which all major Japanese
 * IMEs accept. x-prefix for small kana (xa, xtu).
 */
static const char * const romaji_table[KANA_COUNT] = {
    [KANA_NONE] = "",
    /* Vowels */
    [KANA_A]  = "a",   [KANA_I]  = "i",   [KANA_U]  = "u",
    [KANA_E]  = "e",   [KANA_O]  = "o",
    /* K-row */
    [KANA_KA] = "ka",  [KANA_KI] = "ki",  [KANA_KU] = "ku",
    [KANA_KE] = "ke",  [KANA_KO] = "ko",
    /* S-row */
    [KANA_SA] = "sa",  [KANA_SI] = "si",  [KANA_SU] = "su",
    [KANA_SE] = "se",  [KANA_SO] = "so",
    /* T-row */
    [KANA_TA] = "ta",  [KANA_TI] = "ti",  [KANA_TU] = "tu",
    [KANA_TE] = "te",  [KANA_TO] = "to",
    /* N-row */
    [KANA_NA] = "na",  [KANA_NI] = "ni",  [KANA_NU] = "nu",
    [KANA_NE] = "ne",  [KANA_NO] = "no",
    /* H-row */
    [KANA_HA] = "ha",  [KANA_HI] = "hi",  [KANA_HU] = "hu",
    [KANA_HE] = "he",  [KANA_HO] = "ho",
    /* M-row */
    [KANA_MA] = "ma",  [KANA_MI] = "mi",  [KANA_MU] = "mu",
    [KANA_ME] = "me",  [KANA_MO] = "mo",
    /* Y-row */
    [KANA_YA] = "ya",  [KANA_YU] = "yu",  [KANA_YO] = "yo",
    /* R-row */
    [KANA_RA] = "ra",  [KANA_RI] = "ri",  [KANA_RU] = "ru",
    [KANA_RE] = "re",  [KANA_RO] = "ro",
    /* W + N */
    [KANA_WA] = "wa",  [KANA_WO] = "wo",  [KANA_NN] = "nn",
    /* Dakuten (voiced) */
    [KANA_GA] = "ga",  [KANA_GI] = "gi",  [KANA_GU] = "gu",
    [KANA_GE] = "ge",  [KANA_GO] = "go",
    [KANA_ZA] = "za",  [KANA_ZI] = "zi",  [KANA_ZU] = "zu",
    [KANA_ZE] = "ze",  [KANA_ZO] = "zo",
    [KANA_DA] = "da",  [KANA_DI] = "di",  [KANA_DU] = "du",
    [KANA_DE] = "de",  [KANA_DO] = "do",
    [KANA_BA] = "ba",  [KANA_BI] = "bi",  [KANA_BU] = "bu",
    [KANA_BE] = "be",  [KANA_BO] = "bo",
    [KANA_VU] = "vu",
    /* Handakuten (semi-voiced) */
    [KANA_PA] = "pa",  [KANA_PI] = "pi",  [KANA_PU] = "pu",
    [KANA_PE] = "pe",  [KANA_PO] = "po",
    /* Small kana */
    [KANA_SMALL_A]  = "xa",  [KANA_SMALL_I]  = "xi",
    [KANA_SMALL_U]  = "xu",  [KANA_SMALL_E]  = "xe",
    [KANA_SMALL_O]  = "xo",
    [KANA_SMALL_YA] = "xya", [KANA_SMALL_YU] = "xyu",
    [KANA_SMALL_YO] = "xyo", [KANA_SMALL_TU] = "xtu",
    /* Punctuation — sent as ASCII, IME converts to fullwidth.
     * 。/．→ ".", 、/，→ "," — actual character depends on IME punctuation style. */
    [KANA_KUTEN]    = ".",   [KANA_TOUTEN]   = ",",
    [KANA_COMMA]    = ",",   [KANA_PERIOD]   = ".",
    [KANA_NAKAGURO] = "/",   [KANA_CHOUON]   = "-",
    /* Digits */
    [KEY_0] = "0", [KEY_1] = "1", [KEY_2] = "2", [KEY_3] = "3", [KEY_4] = "4",
    [KEY_5] = "5", [KEY_6] = "6", [KEY_7] = "7", [KEY_8] = "8", [KEY_9] = "9",
    /* ASCII symbols */
    [KEY_SPACE]    = " ",  [KEY_EXCL]     = "!", [KEY_DQUOTE]   = "\"",
    [KEY_HASH]     = "#",  [KEY_DOLLAR]   = "$", [KEY_PERCENT]  = "%",
    [KEY_AMP]      = "&",  [KEY_SQUOTE]   = "'", [KEY_LPAREN]   = "(",
    [KEY_RPAREN]   = ")",  [KEY_ASTER]    = "*", [KEY_PLUS]     = "+",
    [KEY_COMMA]    = ",",  [KEY_MINUS]    = "-", [KEY_DOT]      = ".",
    [KEY_SLASH]    = "/",  [KEY_COLON]    = ":", [KEY_SEMI]     = ";",
    [KEY_LT]       = "<",  [KEY_EQ]       = "=", [KEY_GT]       = ">",
    [KEY_QUESTION] = "?",  [KEY_AT]       = "@", [KEY_LBKT]     = "[",
    [KEY_BSLH]     = "\\", [KEY_RBKT]     = "]", [KEY_CARET]    = "^",
    [KEY_UNDER]    = "_",  [KEY_GRAVE]    = "`", [KEY_LBRACE]   = "{",
    [KEY_PIPE]     = "|",  [KEY_RBRACE]   = "}", [KEY_TILDE]    = "~",
    /* Yōon (拗音) */
    [KANA_KYA] = "kya", [KANA_KYU] = "kyu", [KANA_KYO] = "kyo",
    [KANA_SYA] = "sya", [KANA_SYU] = "syu", [KANA_SYO] = "syo",
    [KANA_TYA] = "tya", [KANA_TYU] = "tyu", [KANA_TYO] = "tyo",
    [KANA_NYA] = "nya", [KANA_NYU] = "nyu", [KANA_NYO] = "nyo",
    [KANA_HYA] = "hya", [KANA_HYU] = "hyu", [KANA_HYO] = "hyo",
    [KANA_MYA] = "mya", [KANA_MYU] = "myu", [KANA_MYO] = "myo",
    [KANA_RYA] = "rya", [KANA_RYU] = "ryu", [KANA_RYO] = "ryo",
    [KANA_GYA] = "gya", [KANA_GYU] = "gyu", [KANA_GYO] = "gyo",
    [KANA_ZYA] = "zya", [KANA_ZYU] = "zyu", [KANA_ZYO] = "zyo",
    [KANA_DYA] = "dya", [KANA_DYU] = "dyu", [KANA_DYO] = "dyo",
    [KANA_BYA] = "bya", [KANA_BYU] = "byu", [KANA_BYO] = "byo",
    [KANA_PYA] = "pya", [KANA_PYU] = "pyu", [KANA_PYO] = "pyo",
    /* Extended kana (外来音) */
    [KANA_FA]  = "fa",  [KANA_FI]  = "fi",  [KANA_FE]  = "fe",
    [KANA_FO]  = "fo",
    [KANA_TI2] = "thi", [KANA_DI2] = "dhi",
    [KANA_TU2] = "twu", [KANA_DU2] = "dwu",
    [KANA_WI]  = "whi", [KANA_WE]  = "whe", [KANA_WO2] = "who",
    [KANA_SYE] = "sye", [KANA_ZYE] = "zye", [KANA_TYE] = "tye",
    [KANA_SMALL_WA] = "xwa",
};

/*
 * Map ASCII character to HID encoded keycode (US QWERTY layout).
 * Shifted characters use LS() to add implicit Shift modifier.
 */

#define HID_KEY(usage) ZMK_HID_USAGE(HID_USAGE_KEY, usage)
#define HID_KEY_S(usage) LS(HID_KEY(usage))  /* shifted */

static uint32_t char_to_hid(char c) {
    if (c >= 'a' && c <= 'z') {
        return HID_KEY(c - 'a' + HID_USAGE_KEY_KEYBOARD_A);
    }
    if (c >= '1' && c <= '9') {
        return HID_KEY(c - '1' + HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION);
    }
    switch (c) {
    /* Unshifted keys */
    case '0': return HID_KEY(HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS);
    case ' ': return HID_KEY(HID_USAGE_KEY_KEYBOARD_SPACEBAR);
    case '-': return HID_KEY(HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE);
    case '=': return HID_KEY(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS);
    case '[': return HID_KEY(HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE);
    case ']': return HID_KEY(HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE);
    case '\\': return HID_KEY(HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE);
    case ';': return HID_KEY(HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON);
    case '\'': return HID_KEY(HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE);
    case '`': return HID_KEY(HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE);
    case ',': return HID_KEY(HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN);
    case '.': return HID_KEY(HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN);
    case '/': return HID_KEY(HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK);
    /* Shifted keys */
    case '!': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION);
    case '@': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_2_AND_AT);
    case '#': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_3_AND_HASH);
    case '$': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR);
    case '%': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT);
    case '^': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_6_AND_CARET);
    case '&': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND);
    case '*': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK);
    case '(': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS);
    case ')': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS);
    case '_': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE);
    case '+': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS);
    case '{': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE);
    case '}': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE);
    case '|': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE);
    case ':': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON);
    case '"': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE);
    case '~': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE);
    case '<': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN);
    case '>': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN);
    case '?': return HID_KEY_S(HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK);
    default:  return 0;
    }
}

/* ── Kana output ──────────────────────────────────────────── */

static void output_kana(uint32_t kana_id, int64_t timestamp) {
    if (kana_id == KANA_NONE) {
        return;
    }

    /* CDIS_KEY path intentionally runs before KANA_COUNT validation. */
    if (kana_id >= CDIS_RAW_KEYCODE_THRESHOLD) {
        LOG_DBG("output_kana raw keycode 0x%08x", kana_id);
        raise_zmk_keycode_state_changed_from_encoded(kana_id, true, timestamp);
        raise_zmk_keycode_state_changed_from_encoded(kana_id, false, timestamp);
        return;
    }

    if (kana_id >= KANA_COUNT) {
        return;
    }

    const char *romaji = romaji_table[kana_id];
    if (!romaji) {
        return;
    }

    for (const char *p = romaji; *p; p++) {
        uint32_t encoded = char_to_hid(*p);
        if (encoded) {
            raise_zmk_keycode_state_changed_from_encoded(encoded, true, timestamp);
            raise_zmk_keycode_state_changed_from_encoded(encoded, false, timestamp);
        }
    }

    LOG_DBG("output_kana id=%d romaji=\"%s\"", kana_id, romaji);
}

/* Public API — usable from &cdis_kana behavior */
void chordis_output_kana(uint32_t kana_code, int64_t timestamp) {
    output_kana(kana_code, timestamp);
}

static void output_shifted_kana(const uint32_t kana[CHORDIS_KANA_SLOTS],
                                enum chordis_thumb_side side,
                                int64_t timestamp) {
    int idx = side + 1; /* shift 0 → kana[1], shift 1 → kana[2], etc. */
    LOG_DBG("output_shifted side=%d idx=%d kana=0x%04x", side, idx, kana[idx]);
    output_kana(kana[idx], timestamp);
}

static void output_thumb_tap(int64_t timestamp) {
    LOG_DBG("output_thumb_tap binding=%s param1=0x%08x",
            sm.thumb_tap_binding.behavior_dev,
            sm.thumb_tap_binding.param1);

    struct zmk_behavior_binding_event ev = {
        .position = sm.thumb_position,
        .timestamp = timestamp,
    };
    zmk_behavior_invoke_binding(&sm.thumb_tap_binding, ev, true);
    zmk_behavior_invoke_binding(&sm.thumb_tap_binding, ev, false);
}

/* ── Auto-off event listener ─────────────────────────────── */

#ifdef CHORDIS_AUTO_OFF_ENABLED

static int chordis_auto_off_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* Deferred dismissal trigger for non-nicola modifier sources: if pending
     * is armed and a release event has cleared all explicit modifiers, fire
     * the dismissal here. The TRK_HOLD release path in chordis_on_char_released
     * handles the more common case where the modifier comes from a nicola
     * hold-tap. */
    if (!ev->state && chordis_pending_auto_off_layer >= 0 &&
        zmk_hid_get_explicit_mods() == 0 && !chordis_is_hold_active()) {
        int layer = chordis_pending_auto_off_layer;
        chordis_pending_auto_off_layer = -1;
        chordis_active_layer = -1;
        LOG_DBG("deferred auto-off (listener): deactivate layer=%d + LANG2", layer);
        zmk_keymap_layer_deactivate(layer);
        raise_zmk_keycode_state_changed_from_encoded(LANG2, true, ev->timestamp);
        raise_zmk_keycode_state_changed_from_encoded(LANG2, false, ev->timestamp);
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (chordis_active_layer < 0 || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint8_t mods = zmk_hid_get_explicit_mods();
    if (mods == 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < AUTO_OFF_PAIR_COUNT; i++) {
        uint32_t cfg_mod_mask = auto_off_keys[i * 2];
        uint32_t cfg_encoded = auto_off_keys[i * 2 + 1];
        uint16_t cfg_page = (cfg_encoded >> 16) & 0xFFFF;
        uint32_t cfg_keycode = cfg_encoded & 0xFFFF;

        if ((mods & cfg_mod_mask) &&
            ev->usage_page == cfg_page &&
            ev->keycode == cfg_keycode) {
            LOG_DBG("auto-off armed: mods=0x%02x key=0x%04x layer=%d (deferred)",
                    mods, ev->keycode, chordis_active_layer);

            /* Defer dismissal: do NOT release the held modifier or deactivate
             * the layer here. The user is mid-shortcut (e.g. C-x C-f) and
             * expects Ctrl to remain held across multiple keystrokes.
             *
             * Instead, arm pending_auto_off; the deferred handler in the
             * TRK_HOLD release path (chordis_on_char_released) will deactivate
             * the layer and emit LANG2 once the last hold-tap modifier
             * releases naturally. That guarantees LANG2 reaches the host with
             * no modifier collision.
             *
             * Idempotent on repeated shortcut presses while still held. */
            if (chordis_pending_auto_off_layer < 0) {
                chordis_pending_auto_off_layer = chordis_active_layer;
            }

            return ZMK_EV_EVENT_BUBBLE;
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(chordis_auto_off, chordis_auto_off_listener);
ZMK_SUBSCRIPTION(chordis_auto_off, zmk_keycode_state_changed);

#endif /* CHORDIS_AUTO_OFF_ENABLED */
