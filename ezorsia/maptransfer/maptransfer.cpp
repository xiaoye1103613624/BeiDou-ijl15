// maptransfer.cpp — expand CUIMapTransfer / CharacterData MapTransfer slots.
// Source: https://gist.github.com/v3921358/b313cff34067f62020b53163d51288b0
// BeiDou.exe @ 0x400000 — patch sites verified against live EXE (push 5 / push 0A, lea +0xB8 field list).
// Storage is plugin-static arrays (not client BSS), per no-uncertain-address-patch rule.

#include "stdafx.h"
#include "MapTransferExpandApi.h"
#include "Memory.h"

#include <algorithm>
#include <iterator>

// Gist author: size available is over 127 (already tested with 999).
#define MAP_TRANSFER_SIZE 999
#define MAP_TRANSFER_EX_SIZE 999
#define MAX_MAP_TRANSFER_SIZE (MAP_TRANSFER_SIZE > MAP_TRANSFER_EX_SIZE ? MAP_TRANSFER_SIZE : MAP_TRANSFER_EX_SIZE)

namespace {

unsigned int g_adwMapTransfer[MAP_TRANSFER_SIZE];
unsigned int g_adwMapTransferEx[MAP_TRANSFER_EX_SIZE];
unsigned int g_adwFieldList[MAX_MAP_TRANSFER_SIZE];

void WriteBytes(DWORD addr, unsigned char* bytes, int size) {
    Memory::WriteByteArray(addr, bytes, size);
}

void WriteAbsLeaEcx(DWORD insnAddr, void* abs) {
    // 8D ?? xx xx xx xx  →  lea ecx, [abs]  (ModRM 0x0D)
    Memory::WriteByte(insnAddr + 1, 0x0D);
    Memory::WriteInt(insnAddr + 2, reinterpret_cast<DWORD>(abs));
}

void WriteAbsLeaEdi(DWORD insnAddr, void* abs) {
    Memory::WriteByte(insnAddr + 1, 0x3D);
    Memory::WriteInt(insnAddr + 2, reinterpret_cast<DWORD>(abs));
}

void ExpandPatches();

// --- naked caves (match gist register / return addresses) ---

const DWORD kRet_Decode_MapTransfer = 0x004E63DF;
__declspec(naked) void Cave_Decode_MapTransfer() {
    __asm {
        push MAP_TRANSFER_SIZE
        lea esi, [g_adwMapTransfer]
        jmp kRet_Decode_MapTransfer
    }
}

const DWORD kRet_Decode_MapTransferEx = 0x004E63F8;
__declspec(naked) void Cave_Decode_MapTransferEx() {
    __asm {
        push MAP_TRANSFER_EX_SIZE
        lea esi, [g_adwMapTransferEx]
        jmp kRet_Decode_MapTransferEx
    }
}

const DWORD kRet_OnCreate = 0x00839876;
__declspec(naked) void Cave_OnCreate() {
    __asm {
        mov eax, [esi + 0xF0] // m_bCanTransferContinent
        cmp eax, 1
        jge vip
        mov eax, MAP_TRANSFER_SIZE
        jmp back
    vip:
        mov eax, MAP_TRANSFER_EX_SIZE
    back:
        jmp kRet_OnCreate
    }
}

const DWORD kRet_OnCreateScrollBarRange = 0x00839763;
__declspec(naked) void Cave_OnCreateScrollBarRange() {
    __asm {
        mov ecx, [esi + 0xAC]
        push eax
        mov eax, [esi + 0xF0]
        cmp eax, 1
        pop eax
        jge vip
        push (MAP_TRANSFER_SIZE - 4)
        jmp back
    vip:
        push (MAP_TRANSFER_EX_SIZE - 4)
    back:
        jmp kRet_OnCreateScrollBarRange
    }
}

const DWORD kRet_OnRegister = 0x0083A0A8;
__declspec(naked) void Cave_OnRegister() {
    __asm {
        mov esi, [edi + 0xF0]
        cmp esi, 1
        jge vip
        mov esi, MAP_TRANSFER_SIZE
        jmp back
    vip:
        mov esi, MAP_TRANSFER_EX_SIZE
    back:
        jmp kRet_OnRegister
    }
}

const DWORD kRet_SetRet = 0x0083A501;
__declspec(naked) void Cave_SetRet() {
    __asm {
        mov ecx, [esi + 0xF0]
        cmp ecx, 1
        jge vip
        mov ecx, MAP_TRANSFER_SIZE
        jmp back
    vip:
        mov ecx, MAP_TRANSFER_EX_SIZE
    back:
        jmp kRet_SetRet
    }
}

const DWORD kRet_Draw = 0x0083990C;
__declspec(naked) void Cave_Draw() {
    __asm {
        mov esi, [ebx + 0xF0]
        cmp esi, 1
        jge vip
        mov esi, MAP_TRANSFER_SIZE
        jmp back
    vip:
        mov esi, MAP_TRANSFER_EX_SIZE
    back:
        jmp kRet_Draw
    }
}

const DWORD kRet_GetMapIndexFromPoint = 0x00839EA3;
__declspec(naked) void Cave_GetMapIndexFromPoint() {
    __asm {
        mov eax, [esi + 0xF0]
        cmp eax, 1
        jge vip
        mov eax, MAP_TRANSFER_SIZE
        jmp back
    vip:
        mov eax, MAP_TRANSFER_EX_SIZE
    back:
        jmp kRet_GetMapIndexFromPoint
    }
}

const DWORD kRet_UpdateFieldList = 0x0083A6E3;
__declspec(naked) void Cave_UpdateFieldList() {
    __asm {
        mov esi, [edi + 0xF0]
        cmp esi, 1
        jge vip
        mov esi, MAP_TRANSFER_SIZE
        jmp back
    vip:
        mov esi, MAP_TRANSFER_EX_SIZE
    back:
        jmp kRet_UpdateFieldList
    }
}

const DWORD kRet_OnMapTransferResult = 0x00A252CA;
__declspec(naked) void Cave_OnMapTransferResult() {
    __asm {
        cmp eax, 1
        jge vip
        mov eax, MAP_TRANSFER_SIZE
        jmp back
    vip:
        mov eax, MAP_TRANSFER_EX_SIZE
    back:
        jmp kRet_OnMapTransferResult
    }
}

void ExpandPatches() {
    std::fill(std::begin(g_adwMapTransfer), std::end(g_adwMapTransfer), 999999999u);
    std::fill(std::begin(g_adwMapTransferEx), std::end(g_adwMapTransferEx), 999999999u);
    std::fill(std::begin(g_adwFieldList), std::end(g_adwFieldList), 999999999u);

    // CharacterData::Decode MapTransfer / MapTransferEx
    Memory::CodeCave(Cave_Decode_MapTransfer, 0x004E63D7, 8);
    Memory::CodeCave(Cave_Decode_MapTransferEx, 0x004E63F0, 8);

    // CUIMapTransfer::OnCreate — enable scrollbar + expand counts
    {
        unsigned char nops2[] = {0x90, 0x90};
        WriteBytes(0x008396FE, nops2, 2);
    }
    Memory::CodeCave(Cave_OnCreateScrollBarRange, 0x0083975B, 8);
    WriteAbsLeaEcx(0x00839866, g_adwMapTransfer);
    WriteAbsLeaEcx(0x0083985E, g_adwMapTransferEx);
    Memory::CodeCave(Cave_OnCreate, 0x0083986C, 10);
    // FieldList → plugin array
    Memory::WriteByte(0x0083987E + 1, 0x05);
    Memory::WriteInt(0x0083987E + 2, reinterpret_cast<DWORD>(g_adwFieldList));
    Memory::WriteByte(0x00839892 + 1, 0x05);
    Memory::WriteInt(0x00839892 + 2, reinterpret_cast<DWORD>(g_adwFieldList));

    // OnRegister (full-list check)
    Memory::CodeCave(Cave_OnRegister, 0x0083A09E, 10);
    Memory::WriteByte(0x0083A0B4 + 1, 0x0D);
    Memory::WriteInt(0x0083A0B4 + 2, reinterpret_cast<DWORD>(g_adwFieldList));
    {
        unsigned char leaEdi[] = {0x8D, 0x3D};
        WriteBytes(0x0083A11C, leaEdi, 2);
    }
    Memory::WriteInt(0x0083A11C + 2, reinterpret_cast<DWORD>(g_adwFieldList));

    // OnDelete / DeleteSelectedField
    {
        unsigned char pushScaled[] = {0x34, 0x85};
        WriteBytes(0x0083A2E3 + 1, pushScaled, 2);
        Memory::WriteInt(0x0083A2E3 + 3, reinterpret_cast<DWORD>(g_adwFieldList));
        WriteBytes(0x0083A3A9 + 1, pushScaled, 2);
        Memory::WriteInt(0x0083A3A9 + 3, reinterpret_cast<DWORD>(g_adwFieldList));
    }

    // SetRet
    Memory::CodeCave(Cave_SetRet, 0x0083A4F1, 16);
    {
        unsigned char cmpScaled[] = {0x04, 0x8D};
        WriteBytes(0x0083A51F + 1, cmpScaled, 2);
        Memory::WriteInt(0x0083A51F + 3, reinterpret_cast<DWORD>(g_adwFieldList));
        unsigned char pushScaled[] = {0x34, 0x85};
        WriteBytes(0x0083A57B + 1, pushScaled, 2);
        Memory::WriteInt(0x0083A57B + 3, reinterpret_cast<DWORD>(g_adwFieldList));
    }

    // GetResult
    {
        unsigned char movScaled[] = {0x04, 0x85};
        WriteBytes(0x0083A6B3 + 1, movScaled, 2);
        Memory::WriteInt(0x0083A6B3 + 3, reinterpret_cast<DWORD>(g_adwFieldList));
    }

    // Draw — size cave + NOP and/add esi,5 that rebuild vanilla 5/10
    Memory::CodeCave(Cave_Draw, 0x00839902, 10);
    {
        unsigned char nops3[] = {0x90, 0x90, 0x90};
        WriteBytes(0x00839914, nops3, 3);
        WriteBytes(0x0083991C, nops3, 3);
    }
    {
        unsigned char leaScaled[] = {0x04, 0xBD};
        WriteBytes(0x00839A72 + 1, leaScaled, 2);
        Memory::WriteInt(0x00839A72 + 3, reinterpret_cast<DWORD>(g_adwFieldList));
    }

    // GetMapIndexFromPoint
    Memory::CodeCave(Cave_GetMapIndexFromPoint, 0x00839E99, 10);
    Memory::WriteByte(0x00839E2E + 1, 5);
    Memory::WriteInt(0x00839E2E + 2, reinterpret_cast<DWORD>(g_adwFieldList));

    // UpdateFieldList
    Memory::CodeCave(Cave_UpdateFieldList, 0x0083A6D3, 16);
    Memory::WriteByte(0x0083A6ED + 1, 0x1D);
    Memory::WriteInt(0x0083A6ED + 2, reinterpret_cast<DWORD>(g_adwFieldList));

    // CWvsContext::OnMapTransferResult
    WriteAbsLeaEdi(0x00A252BA, g_adwMapTransfer);
    WriteAbsLeaEdi(0x00A252B2, g_adwMapTransferEx);
    Memory::CodeCave(Cave_OnMapTransferResult, 0x00A252C0, 10);
}

} // namespace

void AttachMapTransferExpandMod() {
    ExpandPatches();
}
