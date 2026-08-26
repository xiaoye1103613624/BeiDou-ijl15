#include "stdafx.h"
#include "damagelong.h"
#include "compat/hook.h"

#include <climits>
#include <deque>
#include <unordered_map>

namespace {

constexpr long long kIntMax = 2147483647LL;
constexpr double kExactIntLimit = 9007199254740992.0; // 2^53

long long g_lastReal = 0;
std::deque<long long> g_overflow;
std::deque<long long> g_packetLines;
std::unordered_map<int*, long long> g_slotMap;

constexpr int kMaxLineReals = 32;
long long g_lineReals[kMaxLineReals];
int g_nLineReals = 0;

void PushOverflow(long long v) {
    while (g_overflow.size() >= 64) {
        g_overflow.pop_front();
    }
    g_overflow.push_back(v);
}

void RecordSlot(int* slot, long long v) {
    if (!slot) {
        return;
    }
    if (g_slotMap.size() >= 256) {
        g_slotMap.clear();
        g_packetLines.clear();
        g_nLineReals = 0;
    }
    g_slotMap[slot] = v;
    while (g_packetLines.size() >= 64) {
        g_packetLines.pop_front();
    }
    g_packetLines.push_back(v);
    if (g_nLineReals < kMaxLineReals) {
        g_lineReals[g_nLineReals++] = v;
    }
}

bool TryPeekSlot(int* slot, long long* out) {
    if (!slot || !out) {
        return false;
    }
    auto it = g_slotMap.find(slot);
    if (it == g_slotMap.end()) {
        return false;
    }
    *out = it->second;
    return true;
}

long long ResolveSrc(int srcInt, int* src, int firstHalfIndex) {
    long long srcL = 0;
    if (TryPeekSlot(src, &srcL)) {
        return srcL;
    }
    if (firstHalfIndex >= 0 && firstHalfIndex < g_nLineReals) {
        return g_lineReals[firstHalfIndex];
    }
    return srcInt;
}

} // namespace

extern "C" int __stdcall DamageLong_ConsumeFtol(double d) {
    if (!(d > 0.0)) {
        g_lastReal = 0;
        return 0;
    }
    if (d > kExactIntLimit) {
        d = kExactIntLimit;
    }
    long long v = static_cast<long long>(d);
    g_lastReal = v;
    if (v >= kIntMax) {
        PushOverflow(v);
        return static_cast<int>(kIntMax);
    }
    return static_cast<int>(v);
}

extern "C" void __stdcall DamageLong_StoreSlot(int sat, int* slot) {
    RecordSlot(slot, g_lastReal);
    if (slot) {
        *slot = sat;
    }
}

extern "C" void __stdcall DamageLong_ResetLineReals() {
    g_nLineReals = 0;
}

extern "C" int __stdcall DamageLong_WritePartner50(int srcInt, int* src, int* dest, int firstHalfIndex) {
    long long srcL = ResolveSrc(srcInt, src, firstHalfIndex);
    if (srcL < 1) {
        return 0;
    }
    long long part = srcL / 2;
    RecordSlot(dest, part);
    if (part >= kIntMax) {
        PushOverflow(part);
        return static_cast<int>(kIntMax);
    }
    return static_cast<int>(part);
}

extern "C" double __stdcall DamageLong_PartnerPercent(int srcInt, int* src, int pct, int firstHalfIndex) {
    long long srcL = ResolveSrc(srcInt, src, firstHalfIndex);
    if (pct < 0) {
        pct = 0;
    }
    long long part = srcL * static_cast<long long>(pct) / 100;
    if (part < 0) {
        part = 0;
    }
    if (static_cast<double>(part) > kExactIntLimit) {
        part = static_cast<long long>(kExactIntLimit);
    }
    return static_cast<double>(part);
}

bool DamageLong_TakeOverflow(int displayedInt, long long* out) {
    unsigned int mag = static_cast<unsigned int>(displayedInt) & 0x7FFFFFFFu;
    if (!out || mag != static_cast<unsigned int>(kIntMax)) {
        return false;
    }
    if (g_overflow.empty()) {
        return false;
    }
    *out = g_overflow.front();
    g_overflow.pop_front();
    return true;
}

long long DamageLong_PeekSlot(int* slot) {
    if (!slot) {
        return 0;
    }
    auto it = g_slotMap.find(slot);
    if (it == g_slotMap.end()) {
        return 0;
    }
    return it->second;
}

__declspec(naked) void FtolDamageLine() {
    __asm {
        sub esp, 8
        fstp qword ptr [esp]
        call DamageLong_ConsumeFtol
        ret
    }
}

static auto kPDamageStoreRet = 0x007905A3;
static auto kMDamageStoreRet = 0x00791FA8;

__declspec(naked) void PDamage_store_hook() {
    __asm {
        mov ecx, dword ptr [ebp - 0x48]
        push ecx
        push eax
        call DamageLong_StoreSlot
        jmp dword ptr [kPDamageStoreRet]
    }
}

__declspec(naked) void MDamage_store_hook() {
    __asm {
        mov ecx, dword ptr [ebp + 0x40]
        push ecx
        push eax
        call DamageLong_StoreSlot
        jmp dword ptr [kMDamageStoreRet]
    }
}

static auto kEncode4 = reinterpret_cast<void(__thiscall*)(void*, unsigned int)>(0x004065A6);

extern "C" void EncodeDamageAndMaybeLong(void* pkt, unsigned int n, int* slot) {
    if (slot && slot[15]) {
        n |= 0x80000000u;
    }
    kEncode4(pkt, n);
    long long real = 0;
    if (!TryPeekSlot(slot, &real)) {
        if (!g_packetLines.empty()) {
            real = g_packetLines.front();
            g_packetLines.pop_front();
        } else {
            real = static_cast<int>(n & 0x7FFFFFFFu);
        }
    }
    if (real < 0) {
        real = -real;
    }
    unsigned int lo = static_cast<unsigned int>(real);
    unsigned int hi = static_cast<unsigned int>(real >> 32);
    kEncode4(pkt, lo);
    kEncode4(pkt, hi);
}

__declspec(naked) void EncodeDamage_Melee() {
    __asm {
        push eax
        push dword ptr [esp + 8]
        push ecx
        call EncodeDamageAndMaybeLong
        add esp, 12
        ret 4
    }
}

__declspec(naked) void EncodeDamage_Edi() {
    __asm {
        push edi
        push dword ptr [esp + 8]
        push ecx
        call EncodeDamageAndMaybeLong
        add esp, 12
        ret 4
    }
}

static auto kDecode4 = reinterpret_cast<unsigned int(__thiscall*)(void*)>(0x00406629);

extern "C" unsigned int __stdcall DecodeDamageAndMaybeLong(void* pkt) {
    unsigned int n = kDecode4(pkt);
    unsigned int lo = kDecode4(pkt);
    unsigned int hi = kDecode4(pkt);
    unsigned long long ureal =
        static_cast<unsigned long long>(lo) | (static_cast<unsigned long long>(hi) << 32);
    long long real = static_cast<long long>(ureal);
    if (real < 0) {
        real = -real;
    }
    unsigned int mag = n & 0x7FFFFFFFu;
    if (mag == static_cast<unsigned int>(kIntMax) && real >= kIntMax) {
        PushOverflow(real);
    }
    return n;
}

__declspec(naked) void DecodeDamage_Remote() {
    __asm {
        push ecx
        call DecodeDamageAndMaybeLong
        ret
    }
}

static auto kPartnerPctSkillRet = 0x0079024B;
static auto kPartnerPctAltRet = 0x00790283;

__declspec(naked) void PartnerPercent_cave() {
    __asm {
        push ecx
        mov eax, dword ptr [ebp + 0x40]
        add eax, ecx
        push ecx
        shr dword ptr [esp], 2
        push edx
        push eax
        push dword ptr [eax]
        call DamageLong_PartnerPercent
        pop ecx
        jmp dword ptr [kPartnerPctSkillRet]
    }
}

__declspec(naked) void PartnerPercent_alt_cave() {
    __asm {
        push ecx
        mov eax, dword ptr [ebp + 0x40]
        add eax, ecx
        push ecx
        shr dword ptr [esp], 2
        push edx
        push eax
        push dword ptr [eax]
        call DamageLong_PartnerPercent
        pop ecx
        jmp dword ptr [kPartnerPctAltRet]
    }
}

void AttachDamageLongMod() {
    Patch1(0x0079058F, 0xEB);
    Patch1(0x00791F94, 0xEB);

    PatchCall(0x00790599, &FtolDamageLine);
    PatchCall(0x00791F9E, &FtolDamageLine);

    PatchJmp(0x0079059E, &PDamage_store_hook);
    PatchJmp(0x00791FA3, &MDamage_store_hook);

    PatchCall(0x00952B28, &EncodeDamage_Melee);
    PatchCall(0x00955428, &EncodeDamage_Edi);
    PatchCall(0x0095705E, &EncodeDamage_Edi);

    PatchCall(0x00980574, &DecodeDamage_Remote);
    PatchCall(0x00980619, &DecodeDamage_Remote);

    PatchJmp(0x0079023C, &PartnerPercent_cave);
    PatchJmp(0x00790274, &PartnerPercent_alt_cave);
}
