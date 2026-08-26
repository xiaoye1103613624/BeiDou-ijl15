#pragma once

#define MUSH_FEATURE(name) ((void)0)
#define DEBUG_MESSAGE(...) ((void)0)

inline bool MushFeatureQuarantined(const char*) {
    return false;
}

inline void MushRegisterCode(uintptr_t, const char*) {
}

inline void PatchMemory(void* dest, const void* src, size_t len) {
    DWORD old = 0;
    VirtualProtect(dest, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(dest, src, len);
    VirtualProtect(dest, len, old, &old);
}

#define REGISTER_CODECAVE(addr, name) ((void)0)
