#pragma once
#include <cstdint>

class CompatInPacket;

// ============================================================
// weapontint.h: the HSB re-tint applied to a worn Cash weapon.
//
// A tint is an absolute hue rotation plus a saturation and a value delta, which
// is exactly what the modern Coloring Prism dialog exposes as Tone / Chroma /
// Brightness. The identity tint (all three zero) means "vanilla", so no separate
// "is dyed" flag is needed anywhere: on the wire, in the DB, or in this struct.
// ============================================================
struct WeaponTint {
    short       hue    = 0;   //    0..359  degrees, absolute rotation
    signed char chroma = 0;   // -100..100  saturation delta, percent
    signed char bright = 0;   // -100..100  value delta, percent

    bool IsIdentity() const { return hue == 0 && chroma == 0 && bright == 0; }
    bool operator==(const WeaponTint& o) const {
        return hue == o.hue && chroma == o.chroma && bright == o.bright;
    }
    bool operator!=(const WeaponTint& o) const { return !(*this == o); }
    uint32_t Key() const {
        return (static_cast<uint32_t>(static_cast<uint16_t>(hue)) << 16)
             | (static_cast<uint32_t>(static_cast<uint8_t>(chroma)) << 8)
             |  static_cast<uint32_t>(static_cast<uint8_t>(bright));
    }
};

constexpr short kTintHueMax    = 359;
constexpr signed char kTintDeltaMin = -100;
constexpr signed char kTintDeltaMax =  100;

// ============================================================
// LOOK TINTS -- hair and eye colour, the window's Hair / Face tabs.
//
// These share the equip tint's table, its opcodes and its render hook. They are
// keyed by a KIND SENTINEL rather than by the hair/face WZ id, and that choice is
// load-bearing for four reasons:
//
//   1. Storage. Hair and eyes are not inventory rows, so an id key would imply a
//      per-(character, hairId) table; a kind key is six columns on `characters`.
//   2. The client already owns the id. Hair is AvatarLook.anHairEquip[0] and face
//      is AvatarLook.nFace, read off whichever avatar the renderer just handed
//      over -- for a REMOTE player that avatar's look is the only correct source,
//      so a server-supplied id could only agree with it or be wrong.
//   3. It deletes a duplicated classifier. With ids, CharacterSubdirOf would have
//      to reproduce the client's ItemIdToCharacterSubdir, including any runtime
//      widening of it; with a kind the DLL just says L"Hair" / L"Face".
//   4. Players expect a dyed colour to survive a restyle. Keyed by id it would not.
//
// 1 and 2 cannot collide with the equip ids sharing the table: those are 1xxxxxx.
// ============================================================
constexpr int kTintKey_Hair = 1;
constexpr int kTintKey_Face = 2;

inline bool IsLookTintKey(int key) { return key == kTintKey_Hair || key == kTintKey_Face; }

// An item's EFFECT layers are dyed separately from its body, so they need a second key
// per item. Biasing the id keeps one table and one wire format: equip ids top out at
// 1999999, so id + 10000000 cannot collide with a real id, with the two look sentinels,
// or with another item's effect key. Only ~3.4% of installed equips have effect layers
// at all (mostly cash weapons), so most items never produce one of these.
constexpr int kTintKey_EffectBias = 10000000;
inline bool IsEffectTintKey(int key) { return key >= kTintKey_EffectBias; }
inline int  EffectTintKeyFor(int itemId) { return itemId + kTintKey_EffectBias; }
inline int  ItemOfEffectTintKey(int key) { return key - kTintKey_EffectBias; }

// Custom opcode pair from a private 0x372x block, following an even/odd
// request/reply convention.
// Server side is net.opcodes.RecvOpcode.WEAPON_TINT_ACTION / SendOpcode.WEAPON_TINT_SYNC.
constexpr unsigned short kWeaponTintActionOpcode = 0x372E;   // client -> server
constexpr unsigned short kWeaponTintSyncOpcode   = 0x372F;   // server -> client

// --- inbound (routed from the packet dispatcher) -----------------------------
void WeaponTint_HandleSync(CompatInPacket* p);

// --- hook installation (hook.h / AttachClientHooks) --------------------------
void AttachWeaponTintMod();

// --- item-effect layers ------------------------------------------------------
// The Effects tab's colour applied to a self-animating item-effect layer (a cape
// aura), which is built outside CAvatar::PrepareActionLayer and so is invisible to
// the hook that tints everything else. Wrap the layer build: Begin swaps tinted
// clones into Effect/ItemEff.img/<itemId>/effect and returns whether it swapped
// anything, End puts the originals back. ALWAYS call End if Begin was called, even
// when it returned false. Not reentrant.
//
// Nothing in this package calls these: the renderer that builds those layers is not
// bundled. They are here so a host that has one can dye that art through the same
// tab, the same tint key and the same database columns as everything else.
bool WeaponTint_BeginItemEffSwap(void* pAvatar, int itemId);
void WeaponTint_EndItemEffSwap();

// Main thread, once per frame (driven from the per-frame update hook). Applies a
// tint the server pushed while the packet was being handled on the receive thread.
void WeaponTint_Tick();

// --- queries + control, used by coloringprism.cpp -----------------------------

// The item a Coloring Prism is being pointed at: where it lives in the inventory
// (so the server can find it) plus its id (so the client can render and verify it).
struct WeaponTintTarget {
    int invType = 0;     // InventoryType value; EQUIPPED for a worn item
    int invPos  = 0;     // position within that inventory, negative when worn
    int itemId  = 0;
    bool IsSet() const { return itemId > 0; }
};

// Item id in the worn Cash-weapon slot, or 0 if the player has no Cash weapon on.
// Read straight off the local CAvatar's own look, so it needs no server round trip.
int  WeaponTint_GetLocalCashWeaponId();

// The tint the server last told us is stored on a given item, per ITEM -- every
// worn equip can carry its own.
WeaponTint WeaponTint_GetSavedFor(int itemId);

// While the prism window is open its slider values REPLACE the saved tint for the
// ONE item being dyed, so that item recolors live while the rest of the outfit
// keeps its own colours. Passing active=false drops back to the saved tint.
void WeaponTint_SetPreview(int itemId, const WeaponTint& t, bool active);
bool WeaponTint_IsPreviewActive();

// Whichever tint is currently driving the render for `itemId`.
WeaponTint WeaponTint_GetEffectiveFor(int itemId);

// Wrap a CAvatar::Init / PrepareActionLayer call that belongs to a preview avatar
// this DLL owns, so its weapon layers get the effective tint even though the
// avatar is not the one CUserLocal points at. The scope is bound to that exact
// avatar; it must never make another avatar inherit a live preview tint.
// Legacy previews that only need the saved-tint table may omit the avatar. They
// never opt into the live Coloring Prism slider value.
void WeaponTint_BeginForcedScope();
void WeaponTint_BeginForcedScope(void* pAvatar);
// Salon previews can supply the saved hair and eye tints for their look. Identity
// values are meaningful here: they deliberately override the player's live tint.
void WeaponTint_BeginForcedScope(void* pAvatar, const WeaponTint& hairTint,
                                 const WeaponTint& faceTint);
void WeaponTint_EndForcedScope();

// CAvatar builds its face layer asynchronously, after CAvatar::Init has returned.
// Keep the Coloring Prism's live tint bound to its exact preview avatar for that
// deferred build; this is deliberately not a global preview flag.
void WeaponTint_BindPreviewAvatar(void* pAvatar);
void WeaponTint_UnbindPreviewAvatar(void* pAvatar);
void WeaponTint_InstallPreviewFaceTint(void* pAvatar);

// Ask the client to rebuild the local player's avatar layers so a tint change is
// visible without waiting for the next action change. Best-effort.
void WeaponTint_RefreshLocalAvatar();

// Adopt `t` as the saved tint without waiting for the server to say so. Called on
// Confirm: the window closes immediately, and without this the weapon would snap
// back to its old color for the length of the round trip. The server's reply
// snapshot is authoritative and overwrites this either way.
void WeaponTint_AdoptOptimistic(int itemId, const WeaponTint& t);

// --- outbound -----------------------------------------------------------------
void WeaponTint_RequestSnapshot();
// `layer` is 0 for the item's body and 1 for its EFFECT sprites, which carry their own
// tint. The server stores them in different columns; everything else about the two is
// identical, which is why this is a byte rather than a second pair of actions.
constexpr int kTintLayer_Body    = 0;
constexpr int kTintLayer_Effects = 1;
void WeaponTint_SendApply(const WeaponTintTarget& target, const WeaponTint& t, int prismPos, int layer);
void WeaponTint_SendRestore(const WeaponTintTarget& target, int prismPos, int layer);

// Look (hair / eye) variants. They carry a KIND instead of a WeaponTintTarget,
// because hair and eyes have no inventory address for the server to re-verify --
// the server reads the character's own hair/face instead. Separate ACTIONS on the
// same opcode, since reusing apply/restore would drive them through a resolve()
// that demands a Cash equip at a given inventory position.
void WeaponTint_SendApplyLook(int kind, const WeaponTint& t, int prismPos);
void WeaponTint_SendRestoreLook(int kind, int prismPos);

// The hair / face ids the LOCAL player is currently wearing, or 0. Read off the
// local CAvatar's own look, so no server round trip is needed to preview them.
int WeaponTint_GetLocalHairId();
int WeaponTint_GetLocalFaceId();

// Result of the last APPLY / RESTORE the server answered, for the window to report.
enum WeaponTintResult {
    kTintResult_None = 0,
    kTintResult_Ok,
    kTintResult_NoCashWeapon,
    kTintResult_NoItem,
    kTintResult_NotTinted,
    kTintResult_Failed,
};
WeaponTintResult WeaponTint_TakeLastResult();   // reads and clears
