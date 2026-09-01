#pragma once

// Shoulder 115 / BP20 / -20 + pendant2 112/BP51 + rings 111/BP52–53
// + badge 118/BP54 + totem1 120/BP55 + Addon BP56–62.
// Route A (CD64 EXE + flag): native aEquipped ZRefs −52…−62 — see ADDON_NATIVE_CD_GROW.md.
// Vanilla EXE: shadow/sidecar inventory; Occ OFF; no BP33 cave; 109 vanilla.
void AttachShoulderSlotsFix();
// Call after DamageSkin::EnsureHooks on first CField so unequip detour stays outermost.
void Shoulder_RehookDblClickUnequipOutermost();
// Sync one slot into the DLL HitTest table used by GetBodyPartFromPoint
// (PE BE2260[≥50] must not be written — cash overflow). index = BP-1.
void Shoulder_SetExtHitTestSlot(int index, int x, int y);
// Re-assert BP20 coords + red cave + rewrite shoulder_diag.log (falsifiable stamp).
void Shoulder_ReassertUiAndDiag();
// True when EXE heap slab is CD64 (push 0x700 @0x778F02) AND native flag ON.
bool Shoulder_UseNativeCd64Slots();
// ZRef* for BP52/53 ring shadows (pad+pItem). Null if bp outside 52–53.
extern "C" void* __cdecl Shoulder_ShadowZRefForBp(int bp);
