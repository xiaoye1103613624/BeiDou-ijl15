#pragma once
#include "hook.h"
#include "../Memory.h"

inline void PatchMemory(void* dest, const void* src, size_t len) {
    DWORD old = 0;
    VirtualProtect(dest, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(dest, src, len);
    VirtualProtect(dest, len, old, &old);
}

inline void PatchDouble(uintptr_t addr, double value) {
    PatchMemory(reinterpret_cast<void*>(addr), &value, sizeof(double));
}

inline void PatchInt(uintptr_t addr, int value) {
    PatchMemory(reinterpret_cast<void*>(addr), &value, sizeof(int));
}

inline void PatchBytes(uintptr_t addr, const unsigned char* bytes, size_t len) {
    PatchMemory(reinterpret_cast<void*>(addr), bytes, len);
}
