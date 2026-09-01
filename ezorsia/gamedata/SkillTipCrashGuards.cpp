// SkillTipCrashGuards — null-safe tip string copy + CUISkill empty-button invoke
// + null skill-icon Copy skip (novice skill scroll CANVAS AV).
// Ported from MXD hyperskill tip/Btn guards (no skill-window expansion).
// Stock bytes verified on BeiDou-Client_S9 BeiDou.exe:
//   A60C72/9E/CC2 tip copies; 8AD927 Btn invoke; 8ACD44 skill-icon CopyEx
//   (IDA decompile 2026-09-01: sub_8AC38A uses v124[p+42] icon without null check)
// Do NOT touch A60C00 entry or A60C33 (shared memcpy — boot AV if guarded).

#include "stdafx.h"
#include "SkillTipCrashGuardsApi.h"
#include "../Memory.h"

#include <iostream>

namespace {

static char g_EmptySkillTipStr[1] = {'\0'};
constexpr DWORD kTipResumeC72 = 0x00A60C79;
constexpr DWORD kTipResume9E = 0x00A60CA5;
constexpr DWORD kTipResumeCC2 = 0x00A60CC7;
constexpr DWORD kSkillIconCopyCont = 0x008ACD49; // after replaced mov edx/mov ecx
constexpr DWORD kSkillIconCopySkip = 0x008ACD7D; // release var_114, skip CopyEx
constexpr DWORD kSkillIconCopy2Cont = 0x008ACE05;
constexpr DWORD kSkillIconCopy2Skip = 0x008ACE36; // release var_4C, skip 2nd CopyEx

__declspec(naked) void SkillTipStrCopy_GuardC72() {
    __asm {
        test esi, esi
        jnz L_ok
        lea esi, g_EmptySkillTipStr
    L_ok:
        mov al, byte ptr [esi]
        mov byte ptr [edi], al
        mov al, byte ptr [esi + 1]
        jmp kTipResumeC72
    }
}

__declspec(naked) void SkillTipStrCopy_Guard9E() {
    __asm {
        test esi, esi
        jnz L_ok
        lea esi, g_EmptySkillTipStr
    L_ok:
        mov al, byte ptr [esi]
        mov byte ptr [edi], al
        mov al, byte ptr [esi + 1]
        jmp kTipResume9E
    }
}

__declspec(naked) void SkillTipStrCopy_GuardCC2() {
    __asm {
        test esi, esi
        jnz L_ok
        lea esi, g_EmptySkillTipStr
    L_ok:
        mov al, byte ptr [esi]
        mov byte ptr [edi], al
        inc esi
        jmp kTipResumeCC2
    }
}

__declspec(naked) void BtnArrayInvoke_Guard() {
    __asm {
        cmp esi, 0x01000000
        jb L_skip
        cmp dword ptr [esi + 4], 0
        je L_skip
        mov eax, dword ptr [esi + 4]
        cmp eax, 0x01000000
        jb L_skip
        lea ecx, [eax + 4]
        mov eax, dword ptr [ecx]
        test eax, eax
        jz L_skip
        call dword ptr [eax + 0x24]
        mov esi, dword ptr [esi + 4]
        push dword ptr [esp + 0x10]
        mov eax, dword ptr [esi + 4]
        test eax, eax
        jz L_drop_push
        lea ecx, [esi + 4]
        mov eax, dword ptr [ecx]
        test eax, eax
        jz L_drop_push
        call dword ptr [eax + 0x1C]
        pop esi
        ret 0x0C
    L_drop_push:
        add esp, 4
        pop esi
        ret 0x0C
    L_skip:
        add esp, 4
        pop esi
        ret 0x0C
    }
}

// CUISkill draw (sub_8AC38A): skill-entry icon at +42 may be null for some beginner
// rows; stock still calls IWzCanvas::CopyEx → CANVAS.DLL null AV while scrolling.
// Stock @8ACD44: 8B 55 F0 8B 08 (mov edx,[ebp-10]; mov ecx,[eax]) — exactly 5 bytes.
// ebp-1Ch = source icon canvas (Value); eax = dest window canvas (already non-null).
// Crash log return addr 8ACD6B confirms first CopyEx is the hot path.
__declspec(naked) void SkillIconCopy_NullGuard() {
    __asm {
        cmp dword ptr [ebp - 0x1C], 0
        je L_skip
        mov edx, dword ptr [ebp - 0x10]
        mov ecx, dword ptr [eax]
        jmp kSkillIconCopyCont
    L_skip:
        jmp kSkillIconCopySkip
    }
}

// Second CopyEx @8ACE00 pushes [ebp-28] (UI glyph from this+0x608); same pattern.
__declspec(naked) void SkillIconCopy2_NullGuard() {
    __asm {
        cmp dword ptr [ebp - 0x28], 0
        je L_skip
        mov edx, dword ptr [ebp - 0x10]
        mov ecx, dword ptr [eax]
        jmp kSkillIconCopy2Cont
    L_skip:
        jmp kSkillIconCopy2Skip
    }
}

static bool ExpectBytes(DWORD va, const unsigned char* expected, size_t n) {
    __try {
        const auto* live = reinterpret_cast<const unsigned char*>(va);
        for (size_t i = 0; i < n; ++i) {
            if (live[i] != expected[i]) {
                return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

void AttachSkillTipCrashGuards() {
    static const unsigned char kTipHead[] = {0x8A, 0x06, 0x88, 0x07};
    static const unsigned char kBtnHead[] = {0x8B, 0x46, 0x04, 0x8D, 0x48};
    static const unsigned char kIconCopyHead[] = {0x8B, 0x55, 0xF0, 0x8B, 0x08};

    if (ExpectBytes(0x00A60C72, kTipHead, sizeof(kTipHead))) {
        Memory::CodeCave(reinterpret_cast<void*>(&SkillTipStrCopy_GuardC72), 0x00A60C72, 0);
        std::cout << "[SkillTipGuard] A60C72 installed" << std::endl;
    } else {
        std::cout << "[SkillTipGuard] A60C72 SKIP (bytes mismatch)" << std::endl;
    }
    if (ExpectBytes(0x00A60C9E, kTipHead, sizeof(kTipHead))) {
        Memory::CodeCave(reinterpret_cast<void*>(&SkillTipStrCopy_Guard9E), 0x00A60C9E, 0);
        std::cout << "[SkillTipGuard] A60C9E installed" << std::endl;
    } else {
        std::cout << "[SkillTipGuard] A60C9E SKIP (bytes mismatch)" << std::endl;
    }
    if (ExpectBytes(0x00A60CC2, kTipHead, sizeof(kTipHead))) {
        Memory::CodeCave(reinterpret_cast<void*>(&SkillTipStrCopy_GuardCC2), 0x00A60CC2, 0);
        std::cout << "[SkillTipGuard] A60CC2 installed" << std::endl;
    } else {
        std::cout << "[SkillTipGuard] A60CC2 SKIP (bytes mismatch)" << std::endl;
    }

    constexpr DWORD kBtnInvokeSite = 0x008AD927;
    if (ExpectBytes(kBtnInvokeSite, kBtnHead, sizeof(kBtnHead))) {
        Memory::CodeCave(reinterpret_cast<void*>(&BtnArrayInvoke_Guard), kBtnInvokeSite, 0);
        const unsigned char op = *reinterpret_cast<volatile unsigned char*>(kBtnInvokeSite);
        std::cout << "[SkillTipGuard] BtnArrayInvoke @ 8AD927 => "
                  << std::hex << (int)op << std::dec
                  << (op == 0xE9 ? " OK" : " FAIL") << std::endl;
    } else {
        std::cout << "[SkillTipGuard] BtnArrayInvoke SKIP @ 8AD927 (bytes mismatch)" << std::endl;
    }

    constexpr DWORD kIconCopySite = 0x008ACD44;
    if (ExpectBytes(kIconCopySite, kIconCopyHead, sizeof(kIconCopyHead))) {
        Memory::CodeCave(reinterpret_cast<void*>(&SkillIconCopy_NullGuard), kIconCopySite, 0);
        const unsigned char op = *reinterpret_cast<volatile unsigned char*>(kIconCopySite);
        std::cout << "[SkillTipGuard] SkillIconCopy null-guard @ 8ACD44 => "
                  << std::hex << (int)op << std::dec
                  << (op == 0xE9 ? " OK" : " FAIL") << std::endl;
    } else {
        std::cout << "[SkillTipGuard] SkillIconCopy SKIP @ 8ACD44 (bytes mismatch)" << std::endl;
    }

    constexpr DWORD kIconCopy2Site = 0x008ACE00;
    if (ExpectBytes(kIconCopy2Site, kIconCopyHead, sizeof(kIconCopyHead))) {
        Memory::CodeCave(reinterpret_cast<void*>(&SkillIconCopy2_NullGuard), kIconCopy2Site, 0);
        const unsigned char op = *reinterpret_cast<volatile unsigned char*>(kIconCopy2Site);
        std::cout << "[SkillTipGuard] SkillIconCopy2 null-guard @ 8ACE00 => "
                  << std::hex << (int)op << std::dec
                  << (op == 0xE9 ? " OK" : " FAIL") << std::endl;
    } else {
        std::cout << "[SkillTipGuard] SkillIconCopy2 SKIP @ 8ACE00 (bytes mismatch)" << std::endl;
    }
}
