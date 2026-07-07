#pragma once

#include "../Memory.h"

#define MEMBER_AT(T, OFFSET, NAME) \
    __declspec(property(get = get_##NAME, put = set_##NAME)) T NAME; \
    __forceinline const T& get_##NAME() const { \
        return *reinterpret_cast<const T*>(reinterpret_cast<uintptr_t>(this) + OFFSET); \
    } \
    __forceinline T& get_##NAME() { \
        return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + OFFSET); \
    } \
    __forceinline void set_##NAME(const T& value) { \
        *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + OFFSET) = const_cast<T&>(value); \
    } \
    __forceinline void set_##NAME(T& value) { \
        *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + OFFSET) = value; \
    }

#ifdef _DEBUG
#define ATTACH_HOOK(TARGET, DETOUR) \
    Memory::SetHook(true, reinterpret_cast<void**>(&TARGET), CastHook(&DETOUR))
#else
#define ATTACH_HOOK(TARGET, DETOUR) \
    Memory::SetHook(true, reinterpret_cast<void**>(&TARGET), CastHook(&DETOUR))
#endif

template <typename T>
constexpr void* CastHook(T fn) {
    union {
        T fn;
        void* p;
    } u;
    u.fn = fn;
    return u.p;
}
