#pragma once
// Local debug.h — weather/debug.h is also on the include path; prefer this
// directory when compiling coloringprism sources (same-dir include wins for
// quoted includes from this folder). Provide LOG_ONCE used by upstream.

#include <cstdarg>
#include <cstdio>

inline void LogMessage(const char* sFormat, ...) {
    char buf[1024];
    va_list args;
    va_start(args, sFormat);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, sFormat, args);
    va_end(args);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

inline void ErrorMessage(const char* sFormat, ...) {
    char buf[1024];
    va_list args;
    va_start(args, sFormat);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, sFormat, args);
    va_end(args);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

#define LOG_ONCE(FORMAT, ...)                       \
    do {                                            \
        static bool bLogged__ = false;              \
        if (!bLogged__) {                           \
            bLogged__ = true;                       \
            LogMessage(FORMAT, __VA_ARGS__);        \
        }                                           \
    } while (0)

constexpr int kLogKeyCap = 64;
#define LOG_ONCE_PER_ID(KEY, FORMAT, ...)                                       \
    do {                                                                        \
        static int  aKeys__[kLogKeyCap] = {};                                   \
        static int  nKeys__ = 0;                                                \
        static bool bCapped__ = false;                                          \
        const int   nKey__ = (KEY);                                             \
        bool bSeen__ = false;                                                   \
        for (int i__ = 0; i__ < nKeys__; ++i__) {                               \
            if (aKeys__[i__] == nKey__) { bSeen__ = true; break; }              \
        }                                                                       \
        if (!bSeen__) {                                                         \
            if (nKeys__ < kLogKeyCap) {                                         \
                aKeys__[nKeys__++] = nKey__;                                    \
                LogMessage(FORMAT, __VA_ARGS__);                                \
            } else if (!bCapped__) {                                            \
                bCapped__ = true;                                               \
                LogMessage("    (%d distinct ids logged here; rest suppressed)",\
                           kLogKeyCap);                                         \
            }                                                                   \
        }                                                                       \
    } while (0)
