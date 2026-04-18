/*
 * Host test stub for <zephyr/kernel.h>
 * Provides minimal k_work delayable timer interface used by chordis_engine.c.
 * Timer firing is driven manually by the test runner via mock_runtime.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct k_work;
typedef void (*k_work_handler_t)(struct k_work *work);

struct k_work_delayable {
    k_work_handler_t handler;
    bool             scheduled;
    int              delay_ms;
};

#define K_MSEC(ms) (ms)

/* Implemented in mock_runtime.c — they register/cancel into the mock timer table. */
void k_work_init_delayable(struct k_work_delayable *w, k_work_handler_t h);
int  k_work_schedule(struct k_work_delayable *w, int delay);
int  k_work_cancel_delayable(struct k_work_delayable *w);
int64_t k_uptime_get(void);

/* ── DT macro stubs ──
 * Make DT_HAS_COMPAT_STATUS_OKAY(*) evaluate to 0 so the DT-conditional
 * blocks (timing config, auto_off) compile out. Engine falls back to
 * its built-in defaults.
 */
#define DT_HAS_COMPAT_STATUS_OKAY(compat) 0
#define DT_INST(inst, compat)             0
#define DT_PROP(node, prop)               0
#define DT_NODE_HAS_PROP(node, prop)      0
#define DT_FOREACH_PROP_ELEM(node, prop, fn)

