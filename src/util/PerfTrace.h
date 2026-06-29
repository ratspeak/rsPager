#pragma once

#include <Arduino.h>
#include <string>

#ifndef RSPAGER_PERF_TRACE
#define RSPAGER_PERF_TRACE 1
#endif

#ifndef RSPAGER_PERF_WRITE_TRACE_MS
#define RSPAGER_PERF_WRITE_TRACE_MS 20UL
#endif

#ifndef RSPAGER_PERF_MSG_TRACE_MS
#define RSPAGER_PERF_MSG_TRACE_MS 25UL
#endif

#ifndef RSPAGER_PERF_UI_TRACE_MS
#define RSPAGER_PERF_UI_TRACE_MS 16UL
#endif

#ifndef RSPAGER_PERF_PERSIST_TRACE_MS
#define RSPAGER_PERF_PERSIST_TRACE_MS 25UL
#endif

#if RSPAGER_PERF_TRACE
#define RSPAGER_PERF_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define RSPAGER_PERF_PRINTF(...) do {} while (0)
#endif

namespace PerfTrace {

inline unsigned long nowMs() {
    return millis();
}

inline unsigned long elapsedMs(unsigned long startMs) {
    return millis() - startMs;
}

inline void shortHex(const std::string& hex, char* out, size_t outLen) {
    if (!out || outLen == 0) return;
    snprintf(out, outLen, "%.8s", hex.c_str());
}

inline bool shouldLog(unsigned long durationMs, unsigned long thresholdMs) {
#if RSPAGER_PERF_TRACE
    return durationMs >= thresholdMs;
#else
    (void)durationMs;
    (void)thresholdMs;
    return false;
#endif
}

inline void logWrite(const char* backend, const char* op, const char* path,
                     size_t bytes, bool ok, unsigned long durationMs,
                     unsigned long thresholdMs = RSPAGER_PERF_WRITE_TRACE_MS) {
#if RSPAGER_PERF_TRACE
    if (!ok || durationMs >= thresholdMs) {
        Serial.printf("[PERF] WRITE backend=%s op=%s path=%s bytes=%u ok=%d dur=%lums\n",
                      backend ? backend : "?",
                      op ? op : "?",
                      path ? path : "?",
                      (unsigned)bytes,
                      ok ? 1 : 0,
                      durationMs);
    }
#else
    (void)backend;
    (void)op;
    (void)path;
    (void)bytes;
    (void)ok;
    (void)durationMs;
    (void)thresholdMs;
#endif
}

}  // namespace PerfTrace
