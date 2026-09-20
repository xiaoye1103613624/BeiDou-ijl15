#pragma once

// ============================================================
// coloringprism.h: the four entry points the window exposes to the rest of a DLL.
//
// The window itself Detours nothing. Every address it needs is one the client shares with
// features a host is likely to already own -- the cash-item use path and the drag-and-drop
// handler -- and a second Detour on any of those breaks whatever hooked them first. So the
// window is DISPATCHED INTO rather than self-installing, and this header is the contract
// for that. See INTEGRATION.md sections 4.3, 4.6 and 4.7.
//
// coloringprism.cpp includes this itself, so the compiler checks these against the
// definitions rather than leaving the two to drift.

// Startup. Installs no hooks; it exists so the feature is visible in the attach list
// beside everything else, and so it can log that the window is ready.
void AttachColoringPrismMod();

// Is this the Coloring Prism (5782000)? For the cash-item use hooks to test before
// handing the click over.
//
// The client's own get_consume_cash_type does NOT accept group 578, so a prism
// double-click is dropped before any packet exists. That is why the use path has to be
// dispatched rather than left to the stock route; INTEGRATION.md 4.6 has the detail.
bool ColorPrism_IsPrismItem(int nItemID);

// Open the window for a prism at inventory position nPOS. Consumption happens on the
// server when the dye is confirmed, so the caller should swallow the use rather than
// letting the stock path also consume the item.
void ColorPrism_OnUse(int nPOS, int nItemID);

// Offer a drag-and-drop to the window, from a CDraggableItem::OnDropped hook.
//
// `pTo` is the window the drop was addressed to, as the engine resolved it; the window
// accepts either its own pointer or its pointer plus four, because CWnd inherits
// IUIMsgHandler at +4 and the engine may latch onto either. A NULL pTo is allowed and is
// not a bug: the window falls back to testing whether the cursor is over its 32x32 well,
// which is what makes a drop work when the engine latched onto nothing.
//
// `invType` and `invPos` are the SOURCE of the drag -- where the dragged item came from --
// and are NOT parameters of OnDropped. Read them off the drag tracker; INTEGRATION.md 4.7
// shows where.
//
// Returns true if the drop was consumed, in which case the caller must not also move the
// item. A refusal falls through to the ordinary inventory move.
bool ColorPrism_HandleItemDrop(void* pTo, int invType, int invPos);

// Offer a dragged SKILL to the window, from a CDraggableSkill::OnDropped hook (0x004FAA22,
// vtable 0x00B39810 slot 1). A skill drag is NOT a CDraggableItem: the id lives at
// draggable+0x18 and the drop dispatches through its own vtable, so this is a separate entry
// point from ColorPrism_HandleItemDrop rather than a variant of it.
//
// coloringprism.cpp Detours that address itself, so a host normally needs to do nothing --
// unless its own DLL already hooks 0x004FAA22, in which case the same one-owner rule applies
// and it should dispatch to this instead.
bool ColorPrism_HandleSkillDrop(void* pTo, int skillId);
