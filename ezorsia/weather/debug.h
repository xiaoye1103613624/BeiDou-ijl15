#pragma once
#include <cstdarg>
#include <cstdio>
#include <windows.h>

#ifdef _DEBUG
#define DEBUG_MESSAGE(FORMAT, ...) \
    do { LogMessage(FORMAT, __VA_ARGS__); } while (0)
#else
#define DEBUG_MESSAGE(FORMAT, ...) ((void)0)
#endif

inline void ErrorMessage(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    FILE* f = nullptr;
    if (fopen_s(&f, "beidou-weather.log", "a") == 0 && f) {
        fputs(buf, f);
        fputc('\n', f);
        fclose(f);
    }
}

inline void LogMessage(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA("[weather] ");
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    FILE* f = nullptr;
    if (fopen_s(&f, "beidou-weather.log", "a") == 0 && f) {
        fputs(buf, f);
        fputc('\n', f);
        fclose(f);
    }
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
                LogMessage("    (%d distinct ids logged at this site; "         \
                           "further ones suppressed)", kLogKeyCap);             \
            }                                                                   \
        }                                                                       \
    } while (0)
