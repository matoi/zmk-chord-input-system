/*
 * Host test stub for <zmk/keymap.h>.
 *
 * Slice D-1.5: settle_handler queries the base-layer binding at the plain
 * tracker's position to emit Cmd+X-style passthrough. The mock implementation
 * lives in mock_runtime.c — tests populate it via mock_set_keymap_binding().
 */
#pragma once

#include <stdint.h>
#include <zmk/behavior.h>

typedef uint8_t zmk_keymap_layer_id_t;

zmk_keymap_layer_id_t zmk_keymap_layer_default(void);

const struct zmk_behavior_binding *
zmk_keymap_get_layer_binding_at_idx(zmk_keymap_layer_id_t layer, uint8_t binding_idx);

int zmk_keymap_layer_deactivate(zmk_keymap_layer_id_t layer);
