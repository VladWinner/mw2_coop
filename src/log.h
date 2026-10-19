#pragma once

// Log file: <game dir>\mw2coop\mw2coop.log. Safe to call from any thread.
void log_open(void);
void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_raw(const char *text);
