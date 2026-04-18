/*
 * Mock runtime — implementations of stub Zephyr/ZMK symbols used by
 * chordis_engine.c when built for host tests.
 *
 * Captures HID emit and binding invocation calls into a global event log,
 * and tracks scheduled k_work_delayable timers in a fixed-size table that
 * tests can fire manually.
 */

#include "mock_runtime.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

/* ── Globals ──────────────────────────────────────────────── */

struct mock_event       mock_event_log[MOCK_EVENT_LOG_SIZE];
int                     mock_event_count = 0;
int                     mock_verbose     = 0;
uint8_t                 mock_explicit_mods = 0;

struct mock_timer_slot  mock_timers[MOCK_MAX_TIMERS];

/* Mock keymap: per-position base-layer binding (D-1.5). Tests populate
 * this via mock_set_keymap_binding(). entries with NULL behavior_dev
 * indicate "no binding registered". */
static struct zmk_behavior_binding mock_keymap[MOCK_KEYMAP_SIZE];

/* ── Reset ────────────────────────────────────────────────── */

void mock_reset(void) {
    mock_event_count   = 0;
    mock_explicit_mods = 0;
    memset(mock_event_log, 0, sizeof(mock_event_log));
    memset(mock_keymap,    0, sizeof(mock_keymap));
    for (int i = 0; i < MOCK_MAX_TIMERS; i++) {
        mock_timers[i].active = false;
        /* Keep .work pointer so re-scheduling the same delayable maps to the same slot */
    }
}

void mock_set_keymap_binding(uint8_t position, const char *behavior_dev,
                             uint32_t param1) {
    if (position >= MOCK_KEYMAP_SIZE) return;
    mock_keymap[position].behavior_dev = behavior_dev;
    mock_keymap[position].param1       = param1;
    mock_keymap[position].param2       = 0;
}

void mock_clear_keymap_bindings(void) {
    memset(mock_keymap, 0, sizeof(mock_keymap));
}

zmk_keymap_layer_id_t zmk_keymap_layer_default(void) { return 0; }

int zmk_keymap_layer_deactivate(zmk_keymap_layer_id_t layer) {
    (void)layer;
    return 0;
}

const struct zmk_behavior_binding *
zmk_keymap_get_layer_binding_at_idx(zmk_keymap_layer_id_t layer, uint8_t binding_idx) {
    (void)layer;
    if (binding_idx >= MOCK_KEYMAP_SIZE) return NULL;
    if (mock_keymap[binding_idx].behavior_dev == NULL) return NULL;
    return &mock_keymap[binding_idx];
}

/* ── Event capture helpers ───────────────────────────────── */

static void capture_event(enum mock_event_kind k, uint32_t value, int64_t ts) {
    if (mock_event_count >= MOCK_EVENT_LOG_SIZE) {
        fprintf(stderr, "mock_runtime: event log overflow\n");
        return;
    }
    mock_event_log[mock_event_count++] = (struct mock_event){
        .kind = k, .value = value, .timestamp = ts,
    };
}

const char *mock_event_kind_name(enum mock_event_kind k) {
    switch (k) {
    case MOCK_EVT_KEYCODE_PRESS:   return "KC_PRESS";
    case MOCK_EVT_KEYCODE_RELEASE: return "KC_RELEASE";
    case MOCK_EVT_BINDING_PRESS:   return "BIND_PRESS";
    case MOCK_EVT_BINDING_RELEASE: return "BIND_RELEASE";
    }
    return "?";
}

void mock_dump_log(void) {
    fprintf(stderr, "── mock event log (%d events) ──\n", mock_event_count);
    for (int i = 0; i < mock_event_count; i++) {
        fprintf(stderr, "  [%d] %-12s value=0x%08x ts=%lld\n",
                i,
                mock_event_kind_name(mock_event_log[i].kind),
                mock_event_log[i].value,
                (long long)mock_event_log[i].timestamp);
    }
}

/* ── Stub implementations ───────────────────────────────── */

uint8_t zmk_hid_get_explicit_mods(void) {
    return mock_explicit_mods;
}

int raise_zmk_keycode_state_changed_from_encoded(uint32_t encoded, bool pressed, int64_t timestamp) {
    capture_event(pressed ? MOCK_EVT_KEYCODE_PRESS : MOCK_EVT_KEYCODE_RELEASE,
                  encoded, timestamp);
    return 0;
}

int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event,
                                bool pressed) {
    (void)event;
    capture_event(pressed ? MOCK_EVT_BINDING_PRESS : MOCK_EVT_BINDING_RELEASE,
                  binding ? binding->param1 : 0,
                  event.timestamp);
    return 0;
}

/* ── Timer slot management ──────────────────────────────── */

static int find_or_alloc_slot(struct k_work_delayable *w) {
    /* Try to find existing slot for this work pointer */
    for (int i = 0; i < MOCK_MAX_TIMERS; i++) {
        if (mock_timers[i].work == w) return i;
    }
    /* Allocate a new slot */
    for (int i = 0; i < MOCK_MAX_TIMERS; i++) {
        if (mock_timers[i].work == NULL) {
            mock_timers[i].work = w;
            return i;
        }
    }
    fprintf(stderr, "mock_runtime: timer table full\n");
    return -1;
}

void k_work_init_delayable(struct k_work_delayable *w, k_work_handler_t h) {
    w->handler   = h;
    w->scheduled = false;
    w->delay_ms  = 0;
    /* Pre-register slot so timer table tracks it */
    find_or_alloc_slot(w);
}

int k_work_schedule(struct k_work_delayable *w, int delay) {
    int idx = find_or_alloc_slot(w);
    if (idx < 0) return -1;
    mock_timers[idx].active   = true;
    mock_timers[idx].delay_ms = delay;
    w->scheduled = true;
    w->delay_ms  = delay;
    return 0;
}

int k_work_cancel_delayable(struct k_work_delayable *w) {
    for (int i = 0; i < MOCK_MAX_TIMERS; i++) {
        if (mock_timers[i].work == w) {
            mock_timers[i].active = false;
        }
    }
    w->scheduled = false;
    return 0;
}

int64_t k_uptime_get(void) { return 0; }

/* ── Timer firing ────────────────────────────────────────── */

bool mock_fire_pending_timer(void) {
    /* Fire the active timer with the smallest delay (closest to firing).
     * If multiple are tied, fire the first one found. */
    int best = -1;
    for (int i = 0; i < MOCK_MAX_TIMERS; i++) {
        if (!mock_timers[i].active) continue;
        if (best < 0 || mock_timers[i].delay_ms < mock_timers[best].delay_ms) {
            best = i;
        }
    }
    if (best < 0) return false;
    return mock_fire_timer_slot(best);
}

bool mock_fire_timer_slot(int idx) {
    if (idx < 0 || idx >= MOCK_MAX_TIMERS) return false;
    if (!mock_timers[idx].active) return false;
    struct k_work_delayable *w = mock_timers[idx].work;
    mock_timers[idx].active = false;
    w->scheduled            = false;
    if (w->handler) {
        w->handler((struct k_work *)w);
    }
    return true;
}
