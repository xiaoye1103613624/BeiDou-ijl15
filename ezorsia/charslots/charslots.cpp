#include "stdafx.h"
#include "CharSlotsApi.h"
#include "compat/hook.h"

// GMS v083 character select slots 15 -> 30 (from Characterslot-30 / kaentake).

namespace {

unsigned int g_aCharPtr[48];

DWORD g_aCharPtr_Addr = (DWORD)&g_aCharPtr;
DWORD g_aCharPtr_Addr_m4 = (DWORD)&g_aCharPtr - 4;
DWORD g_aCharPtr_Addr_p4 = (DWORD)&g_aCharPtr + 4;

DWORD ret_OnCreateBuild = 0x0060464C;
DWORD ret_SelChar = 0x006059B6;
DWORD ret_Refresh = 0x0060598C;
DWORD ret_OnKey = 0x00604FEA;
DWORD ret_OMB = 0x006051E3;
DWORD ret_DrawName1 = 0x00605C0B;
DWORD ret_DrawName2 = 0x00605E87;

__declspec(naked) void OnCreateBuild_cave() {
    __asm {
        push 0x1E
        mov  eax, g_aCharPtr_Addr
        jmp  ret_OnCreateBuild
    }
}

__declspec(naked) void SelChar_cave() {
    __asm {
        mov eax, g_aCharPtr[edi * 4]
        cmp dword ptr [eax], 0
        jmp ret_SelChar
    }
}

__declspec(naked) void Refresh_cave() {
    __asm {
        push dword ptr g_aCharPtr[eax * 4]
        mov  ecx, edi
        jmp  ret_Refresh
    }
}

__declspec(naked) void OnKey_cave() {
    __asm {
        mov ecx, g_aCharPtr_Addr_m4
        mov ecx, [ecx + eax * 4]
        cmp dword ptr [ecx], 0
        jmp ret_OnKey
    }
}

__declspec(naked) void OMB_cave() {
    __asm {
        lea eax, [edx + edx * 2]
        shl eax, 2
        add eax, g_aCharPtr_Addr_p4
        jmp ret_OMB
    }
}

__declspec(naked) void DrawName1_cave() {
    __asm {
        mov  eax, g_aCharPtr[eax * 4]
        push ecx
        jmp  ret_DrawName1
    }
}

__declspec(naked) void DrawName2_cave() {
    __asm {
        mov  eax, g_aCharPtr[eax * 4]
        push ecx
        jmp  ret_DrawName2
    }
}

} // namespace

void AttachCharSlotsMod() {
    // Alloc / loop / select-delete bounds: 15 -> 30 (0x1E)
    Patch1(0x005F49B2, 0x1E);
    Patch1(0x005F49C3, 0x1E);
    Patch1(0x005F49D4, 0x1E);
    Patch1(0x005F56E7, 0x1E);
    Patch1(0x005F56F8, 0x1E);
    Patch1(0x005F5709, 0x1E);
    Patch1(0x005F9B22, 0x1E);

    Patch1(0x005F5109, 0x1E);
    Patch1(0x005F516B, 0x1E);
    Patch1(0x005F72A9, 0x1E);
    Patch1(0x005F7C7A, 0x1E);
    Patch1(0x005F9E1E, 0x1E);
    Patch1(0x005FA2F6, 0x1E);
    Patch1(0x006059AC, 0x1E);

    // Create gate char[14] -> char[29]
    Patch1(0x005F6440, 0xEB);
    Patch1(0x005F644C, 0xBA);
    Patch4(0x005F644D, 0x1E);
    Patch1(0x005F6451, 0x90);

    // 29 * 684 = 19836
    Patch4(0x005F7EA2, 19836);

    Patch1(0x005F9E4D, 0x1D);
    Patch4(0x005F9E8A, 19836);
    Patch4(0x005F9E9B, 19836);
    Patch4(0x005F9EAC, 464);

    // Cash-shop slot purchase cap 15 -> 30
    Patch1(0x0046C757, 0x1E);
    Patch1(0x0047AC39, 0x1E);
    Patch1(0x008F1B3E, 0x1E);

    // Relocate 15-entry char ptr cache to g_aCharPtr[48]
    PatchJmp(0x00604647, &OnCreateBuild_cave);
    PatchJmp(0x006059AF, &SelChar_cave);
    PatchJmp(0x00605986, &Refresh_cave);
    PatchJmp(0x00604FE3, &OnKey_cave);
    PatchJmp(0x006051DC, &OMB_cave);
    PatchJmp(0x00605C06, &DrawName1_cave);
    PatchJmp(0x00605E82, &DrawName2_cave);
}
