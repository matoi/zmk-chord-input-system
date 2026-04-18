/*
 * Host test stub for <zephyr/logging/log.h>
 * Logs are routed to stderr when MOCK_VERBOSE=1, otherwise dropped.
 */
#pragma once

#include <stdio.h>

extern int mock_verbose;

#define LOG_MODULE_DECLARE(...)

#define LOG_DBG(fmt, ...) do { if (mock_verbose) fprintf(stderr, "[DBG] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_INF(fmt, ...) do { if (mock_verbose) fprintf(stderr, "[INF] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_WRN(fmt, ...) do { if (mock_verbose) fprintf(stderr, "[WRN] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_ERR(fmt, ...) do { if (mock_verbose) fprintf(stderr, "[ERR] " fmt "\n", ##__VA_ARGS__); } while (0)
