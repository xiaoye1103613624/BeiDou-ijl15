#pragma once

// Addon equip panel 鈥?poll BED64C/ctx+0x35C0 + Equip-overlay dock.
// Stamp: ADDON_APPEND_ONLY_EXTEND_20260804
//   Rule: sidecar = storage only; logic = vanilla parity (mode-2 wear/unequip).
//   Vanilla EXE CD offsets UNCHANGED; BP54鈥?2 = plugin sidecar arena only.
//   Wear/unequip = PacketSendOnly (SendChange); PreferSend ONLY for extended 54鈥?2.
//   Never write 鈭?4/鈭?5 into native 52-slot (cash face/eye alias).
//   Forbidden specials: empty invent / dual 鈭?54 mode-3 / Addon forceUpdate.
//   See docs/ADDON_APPEND_ONLY_EXTEND.md
void AttachEquipAddonMod();

// ZRef* for BP54鈥?2 (pad+pItem). Vanilla sidecar only; unused on CD64 inventory.
// Never native aEquipped[54+] 鈥?52-slot array aliases cash face/eye.
extern "C" void* __cdecl EquipAddon_SidecarZRefForBp(int bp);

namespace EquipAddon {
// NO-OP stub was wrong: caves MUST run before getCharInfo (DllMain after shoulders).
// Get/Set Detours + UI remain FieldInit-only (EnsureHooks).
void InstallLoginPersistEarly();
void EnsureHooks(); // Get/Set + Apply + UiHooks (Layer/OnTick/Park OFF)
void OnTick();
void WirePocketAndSiSlots();
}
