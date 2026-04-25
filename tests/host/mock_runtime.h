/*
 * Mock runtime — exposes capture buffers and timer control to tests.
 *
 * The engine emits HID keycodes and invokes behavior bindings through the
 * stub headers; the implementations live in mock_runtime.c and route output
 * here. Tests inspect the captured events and drive timer firing manually.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <zephyr/kernel.h>  /* for struct k_work_delayable */

/* ── Captured events ──────────────────────────────────────── */

enum mock_event_kind {
    MOCK_EVT_KEYCODE_PRESS,
    MOCK_EVT_KEYCODE_RELEASE,
    MOCK_EVT_BINDING_PRESS,
    MOCK_EVT_BINDING_RELEASE,
};

struct mock_event {
    enum mock_event_kind kind;
    uint32_t             value;     /* encoded keycode, or binding param1 */
    int64_t              timestamp;
};

#define MOCK_EVENT_LOG_SIZE 64

extern struct mock_event mock_event_log[MOCK_EVENT_LOG_SIZE];
extern int               mock_event_count;
extern int               mock_verbose;
extern uint8_t           mock_explicit_mods;

void mock_reset(void);

/* ── Mock keymap (D-1.5: base-layer binding lookup) ───────── */

#define MOCK_KEYMAP_SIZE 64

/* Register a base-layer binding for a given position. The hold_commit_handler
 * looks this up via zmk_keymap_get_layer_binding_at_idx() to emit the
 * passthrough press+release for the plain combo partner B. */
void mock_set_keymap_binding(uint8_t position, const char *behavior_dev,
                             uint32_t param1);
void mock_clear_keymap_bindings(void);

/* ── Timer control ────────────────────────────────────────── *
 * The engine schedules at most a few delayable works. We track up to
 * MAX_TIMERS active timers; tests fire them manually.
 */
#define MOCK_MAX_TIMERS 16

struct mock_timer_slot {
    struct k_work_delayable *work;
    bool                     active;
    int                      delay_ms;
};

extern struct mock_timer_slot mock_timers[MOCK_MAX_TIMERS];

/* Fire the most-recently-scheduled active timer (= the one engine is waiting on).
 * Returns true if a timer was fired. */
bool mock_fire_pending_timer(void);

/* Fire a specific timer slot (advanced use). */
bool mock_fire_timer_slot(int idx);

/* ── Pretty-printing helpers for assertions ──────────────── */

const char *mock_event_kind_name(enum mock_event_kind k);
void        mock_dump_log(void);
