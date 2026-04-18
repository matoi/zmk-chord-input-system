/*
 * Host test stub for <zmk/behavior.h>
 * Provides minimal binding/event types used by chordis_engine.c.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

struct zmk_behavior_binding {
    const char *behavior_dev;
    uint32_t    param1;
    uint32_t    param2;
};

struct zmk_behavior_binding_event {
    uint32_t position;
    int64_t  timestamp;
};

/* Forward declaration for stub implementation in mock_runtime.c */
int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event,
                                bool pressed);
