// bounce.cpp — beginner 1050/1054 (横向闪跃 / 垂直闪跃) cast routing.
//
// IDA / PE-verified on BeiDou-Client_S9\BeiDou.exe (ImageBase 0x400000):
//   DoActiveSkill cave  0x0096792A  bytes 0F 8F 71 09 00 00 (match MXD kOrigCaveBytes)
//   DoActive jmp-back   0x0096793B
//   DoBoundJump          0x0096897A
//   FlashJump cave      0x0096BF0B  cmp eax,0xD72A0C …
//   FlashJump nop-kill  0x0096C073  0F 85 8E 00 00 00 (jnz → PatchNop 6)
//   velocity imm        0x0096C00A / 0x0096C021 / 0x0096C031
//
// No existing hook addresses changed — only new caves / nop / write-immediates
// on the FlashJump / DoActiveSkill sites above (same as MXD mr_skill_satellites).

#include "stdafx.h"
#include "BounceSkillApi.h"
#include "Memory.h"

#include <cstdio>
#include <iostream>

namespace {

constexpr DWORD kDoActiveCave = 0x0096792A;
constexpr DWORD kDoActiveJmpBack = 0x0096793B;
constexpr DWORD kDoBoundJump = 0x0096897A;
constexpr DWORD kFlashJumpCave = 0x0096BF0B;
constexpr DWORD kFlashJumpNop = 0x0096C073;
constexpr DWORD kFlashJumpVar = 0x0096BF52;
constexpr DWORD kFlashJumpRet = 0x0096BF12;

int g_doBoundJump = static_cast<int>(kDoBoundJump);
int g_doActiveJmpBack = static_cast<int>(kDoActiveJmpBack);
DWORD g_FlashJumpVar = kFlashJumpVar;
DWORD g_FlashJumpRet = kFlashJumpRet;

bool g_attached = false;

void BounceLog(const char* msg) {
    FILE* f = nullptr;
    if (fopen_s(&f, "beidou-bounce.log", "a") == 0 && f) {
        fputs(msg, f);
        fputc('\n', f);
        fclose(f);
    }
}

void applyVelocityChange() {
    Memory::WriteInt(0x0096C00A + 1, 0xFFFFFFFF);
    Memory::WriteInt(0x0096C021 + 3, 0x00000000);
    Memory::WriteInt(0x0096C031 + 1, 0xFFFFFB50);
}

void restoreVelocityChange() {
    Memory::WriteInt(0x0096C00A + 1, 0xFFFFFD55);
    Memory::WriteInt(0x0096C021 + 3, 0x0000025E);
    Memory::WriteInt(0x0096C031 + 1, 0xFFFFFD50);
}

// Route only beginner bounce IDs into DoBoundJump; everything else → vanilla continue.
void __declspec(naked) BounceDoActiveSkills() {
    __asm {
        mov eax, 1050
        cmp esi, eax
        je jumpmove_lbl
        mov eax, 10001050
        cmp esi, eax
        je jumpmove_lbl
        mov eax, 20001050
        cmp esi, eax
        je jumpmove_lbl
        mov eax, 1054
        cmp esi, eax
        je jumpmove_lbl
        mov eax, 10001054
        cmp esi, eax
        je jumpmove_lbl
        mov eax, 20001054
        cmp esi, eax
        je jumpmove_lbl
        jmp dword ptr[g_doActiveJmpBack]

        jumpmove_lbl:
        jmp dword ptr[g_doBoundJump]
    }
}

void __declspec(naked) FlashJumpAll() {
    __asm {
        cmp eax, 0xD72A0C
        je applyDefault
        cmp eax, 1050
        je applyDefault
        cmp eax, 10001050
        je applyDefault
        cmp eax, 20001050
        je applyDefault
        cmp eax, 1054
        je applyOverride
        cmp eax, 10001054
        je applyOverride
        cmp eax, 20001054
        je applyOverride
        jmp dword ptr[g_FlashJumpRet]

        applyOverride:
        push ebp
        mov ebp, esp
        call applyVelocityChange
        mov esp, ebp
        pop ebp
        jmp fjvar

        applyDefault:
        push ebp
        mov ebp, esp
        call restoreVelocityChange
        mov esp, ebp
        pop ebp
        jmp fjvar

        fjvar:
        jmp dword ptr[g_FlashJumpVar]
    }
}

} // namespace

void AttachBounceSkillMod() {
    if (g_attached) {
        return;
    }
    g_attached = true;

    BounceLog("[bounce] AttachBounceSkillMod begin (1050/1054 only)");

    // DoActiveSkill: permanent cave (nNOPCount=1) — same site as MXD hyperskill.
    Memory::CodeCave(reinterpret_cast<void*>(&BounceDoActiveSkills), kDoActiveCave, 1);
    BounceLog("[bounce] DoActiveSkill cave @ 0x0096792A -> DoBoundJump for 1050/1054");

    // FlashJump satellite (MXD mr_skill_satellites FlashJump-only).
    Memory::CodeCave(reinterpret_cast<void*>(&FlashJumpAll), kFlashJumpCave, 0);
    Memory::PatchNop(kFlashJumpNop, 6);
    Memory::WriteInt(0x0096C00A + 1, 0xFFFFFD55);
    Memory::WriteInt(0x0096C021 + 3, 0x0000025E);
    Memory::WriteInt(0x0096C031 + 1, 0xFFFFFD50);

    BounceLog("[bounce] FlashJump cave+nop+momentum OK");
    BounceLog("[bounce] AttachBounceSkillMod OK");
    std::cout << "[bounce] AttachBounceSkillMod OK (1050/1054)" << std::endl;
}
