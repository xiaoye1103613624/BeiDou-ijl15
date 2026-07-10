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

#define MEMBER_ARRAY_AT(T, OFFSET, NAME, N) \
    __declspec(property(get = get_##NAME)) T(&NAME)[N]; \
    __forceinline T(&get_##NAME())[N] { \
        return *reinterpret_cast<T(*)[N]>(reinterpret_cast<uintptr_t>(this) + OFFSET); \
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

template <typename U>
inline void PatchJmp(uintptr_t pAddress, U pDestination) {
    Memory::WriteByte(pAddress, 0xE9);
    Memory::WriteInt(pAddress + 1, static_cast<int>(
        reinterpret_cast<uintptr_t>(pDestination) - pAddress - 5));
}

template <typename T, typename U>
inline void PatchJmp(T pAddress, U pDestination) {
    PatchJmp(reinterpret_cast<uintptr_t>(pAddress), pDestination);
}

template <typename U>
inline void PatchCall(uintptr_t pAddress, U pDestination, size_t uSize = 5) {
    Memory::WriteByte(pAddress, 0xE8);
    Memory::WriteInt(pAddress + 1, static_cast<int>(
        reinterpret_cast<uintptr_t>(pDestination) - pAddress - 5));
    for (size_t i = 5; i < uSize; ++i) {
        Memory::WriteByte(pAddress + i, 0x90);
    }
}

template <typename T, typename U>
inline void PatchCall(T pAddress, U pDestination, size_t uSize = 5) {
    PatchCall(reinterpret_cast<uintptr_t>(pAddress), pDestination, uSize);
}

#define MEMBER_HOOK(T, ADDRESS, NAME, ...) \
    inline static auto NAME = reinterpret_cast<T(__thiscall*)(void*, __VA_ARGS__)>(ADDRESS); \
    T NAME##_hook(__VA_ARGS__);
