#include "stdafx.h"
#include "equiptooltip_style.h"
#include "Memory.h"

// Safe equip tooltip styling: binary patches only (dot/bullet removal).
// Centering hooks and code caves removed — patches apply unconditionally at attach.

namespace {
bool g_styleAttached = false;

static constexpr uintptr_t kAddInfoExStoreDot = 0x008F3AF2;
static constexpr uintptr_t kPrintLineSkipDot = 0x008F516C;
} // namespace

void AttachEquipTooltipStyleHooks() {
    if (g_styleAttached) {
        return;
    }
    g_styleAttached = true;

    // AddInfoEx: xor eax,eax; mov [esi+1Ch], eax  (force bUseDot=0).
    Memory::WriteByte(kAddInfoExStoreDot + 0, 0x33);
    Memory::WriteByte(kAddInfoExStoreDot + 1, 0xC0);
    Memory::WriteByte(kAddInfoExStoreDot + 2, 0x89);
    Memory::WriteByte(kAddInfoExStoreDot + 3, 0x46);
    Memory::WriteByte(kAddInfoExStoreDot + 4, 0x1C);
    Memory::WriteByte(kAddInfoExStoreDot + 5, 0x90);

    // PrintLine (sub_8F5056): skip StringPool#1609 bullet block unconditionally.
    Memory::WriteByte(kPrintLineSkipDot + 0, 0xE9);
    Memory::WriteByte(kPrintLineSkipDot + 1, 0x3A);
    Memory::WriteByte(kPrintLineSkipDot + 2, 0x01);
    Memory::WriteByte(kPrintLineSkipDot + 3, 0x00);
    Memory::WriteByte(kPrintLineSkipDot + 4, 0x00);
    Memory::WriteByte(kPrintLineSkipDot + 5, 0x90);

    // Do NOT NOP @ 0x8ED45B: jz 8ED6DA when [ebp+var_18]==0 (edi cleared @ 8ED42C).
    // Forcing fall-through sends no-job-stat equips into the job-class canvas path and crashes.
}
