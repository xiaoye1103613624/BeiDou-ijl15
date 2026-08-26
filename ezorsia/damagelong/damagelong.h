#pragma once

// CalcDamage stays double; intercept the __ftol stores that write each damage line
// and hand the real 64-bit value to Effect_HP / attack packets.
void AttachDamageLongMod();

// Effect_HP Format hook: if displayedInt is INT_MAX sentinel, pop overflow into *out.
bool DamageLong_TakeOverflow(int displayedInt, long long* out);

extern "C" int __stdcall DamageLong_WritePartner50(int srcInt, int* src, int* dest, int firstHalfIndex);
extern "C" void __stdcall DamageLong_ResetLineReals();

long long DamageLong_PeekSlot(int* slot);
