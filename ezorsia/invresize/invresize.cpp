// ============================================================
// invresize.cpp -- drag-to-resize item inventory for MapleStory v83
//
// Drag the item inventory's right edge, bottom edge or bottom-right corner to
// change how many COLUMNS and ROWS of item slots are visible. The grid snaps to
// whole slots. The chosen size is stored per character in CConfig's
// CHARACTER_OPT block ("invCols" / "invRows").
//
// Purely client-side. There is no server half and no WZ edit: the window frame
// is composited at runtime from art the client already ships.
//
// ---------------------------------------------------------------------------
// WHY THIS IS ONLY TWO FUNCTIONS OF PATCHING
//
// The entire (slot <-> pixel) model of CUIItem lives in two members:
//
//   CUIItem::GetSlotRect              0x0081E2C8  ret 8  (nPos, RECT*) -> BOOL
//   CUIItem::GetSlotPositionFromPoint 0x0081DB7E  ret 8  (x, y) -> 1-based slot
//
// GetSlotRect holds the only SetRect (0x0081E397) and SetRectEmpty (0x0081E333)
// in the class; the hit-test holds the only PtInRect (0x0081DC06) and asks
// GetSlotRect for every rect it tests. A rel32 scan finds 5 callers of the
// former (0x0081DBF7, 0x0081DE52, 0x0081DF89, 0x0081E061, 0x0081EC32) and 4 of
// the latter (0x004F1260, 0x004F30A6, 0x0081D48F, 0x0081D8C8), and none does
// slot arithmetic of its own -- including Draw's icon blit at 0x0081DE52. So
// replacing those two moves the whole grid: icons, tooltips, drag pick-up, drop
// targets, double-click-to-use and right-click all follow.
//
// That is also why this is not a replacement window. CDraggableItem::OnDropped
// routes to the inventory drop handlers 0x004F11B4 / 0x004F3057 by class
// identity, and both call 0x0081DB7E on the singleton at 0x00BED654. Replacing
// CUIItem would break every drop from shop / storage / trade / equip, the meso
// row, and sort/gather, for the same feature.
//
// ---------------------------------------------------------------------------
// STOCK GEOMETRY, read out of GetSlotRect 0x0081E37B-0x0081E397:
//
//   v    = nPos - [this+0x5E0]        row = v / 4        col = v % 4
//   top  = 34*row + 50   bottom = top  + 32   ; imul eax,eax,0x22 / add eax,0x32
//   left = 36*col +  8   right  = left + 32   ; lea ecx,[ecx+ecx*8] / add ecx,8
//
// Column pitch 36, row pitch 34, icon 32x32 at origin (8,50). NOTE the art's
// cell BORDER is at x=7 and the ICON at x=8; they are different things, and
// conflating them shifts every column 1px left. From those:
//
//   W(cols) = 36*cols + 31        H(rows) = 34*rows + 85
//
// which reproduces the stock 175x289 at 4x6 exactly and agrees with the two
// immediates the constructor feeds CreateWnd (add eax,0xAF = 175 at 0x0081C517,
// push 0x121 = 289 at 0x0081C512).
//
// ---------------------------------------------------------------------------
// NARROW MODE IS FORCED
//
// CUIItem has a second layout, m_bFull at +0x604, the 603px 4-tab view. It has
// its own rect branch inside GetSlotRect (a +5*group x-term and a row fold at
// 0x0081E360-0x0081E376), its own 97-slot hit bound (push 0x61 at 0x0081DBD0)
// and its own copy of the whole layout inside ToggleFull. Supporting both would
// double this file for a view the feature replaces anyway, so the constructor's
// config read at 0x0081C4BE is patched to xor eax,eax and ToggleFull
// (0x0081E541) is replaced with a 4 <-> 8 column preset toggle.
//
// Safe to force unconditionally: 0x0049F5DA is a plain CConfig getter with
// exactly one caller (this one), its setter 0x0049F62C has exactly one caller
// (0x0081E562, inside the ToggleFull we replace), and the only two writers of
// +0x604 are 0x0081C4C3 and 0x0081E555. m_bFull can never become 1.
//
// ---------------------------------------------------------------------------
// THE ART
//
// There is no resizable inventory background in the WZ, so the chrome is
// composited at runtime into a generated canvas and installed into CWnd+0x68
// (the holder CWnd::Draw @0x009E0502 blits from: cmp [esi+0x68],ebx / je).
//
// EVERY band comes from UI/UIWindow.img/Item/FullBackgrnd (603x289). The
// obvious alternative -- caps from Item/backgrnd (175x289) and the field from
// FullBackgrnd -- does not work: backgrnd is APERIODIC DITHER with no tile
// period anywhere (144 distinct columns across its 4 cells, 62 across its title
// band, no two cells alike), its gutters are a 238/255 dither against
// FullBackgrnd's flat 255, the two sit 1px apart in x, and they carry different
// drop shadows. FullBackgrnd is regular: 16px flat runs with a true period of
// 32, its first four cell columns byte-identical to each other and all six cell
// rows byte-identical, so one 36x34 tile reproduces any grid seam-free.
//
// Do NOT tile from FullBackgrnd's cell columns 4..15: those belong to later tab
// groups, which the stock full-mode rect math offsets by an extra 5px each.
//
// preview_invresize_bg.py renders this exact recipe off-line to PNG, which is
// the cheap way to check a band inset without building and launching.
//
// Cost of single-sourcing: the default 4x6 window is geometry-identical to
// stock but its paper tone comes from FullBackgrnd rather than backgrnd. It is
// a uniform, seam-free change, not a pixel-identical restoration.
//
// ---------------------------------------------------------------------------
// WHAT THIS FILE WRITES
//
//   full replacements : 0x0081E2C8 GetSlotRect, 0x0081DB7E GetSlotPositionFromPoint,
//                       0x0081E541 ToggleFull                      (PatchJmp)
//   Detour            : 0x0081C6C9 CUIItem::OnCreate
//   5-byte cave       : 0x0081C4BE (force narrow)
//   layout immediates : 0x0081C4D4 0x0081C512 0x0081C517 0x0081C967 0x0081CCB9
//                       0x0081CCC0 0x0081CD67 0x0081DCD8 0x0081DD5F 0x0081DDF9
//                       0x0081D293 0x0081D296
//   7-byte rewrites   : 0x0081D083 0x0081D2C4 (lea *4 -> imul *cols)
//
// All of it is byte-guarded up front; on any mismatch nothing is written and
// the inventory keeps its stock fixed 4x6 window.
//
// If another mod in your tree Detours CUIItem::Draw (0x0081DC20) or
// OnMouseButton (0x0081D42E), that is fine -- this file touches neither.
// ============================================================

#include "stdafx.h"
#include "InvResizeApi.h"
#include "compat/hook.h"
#include "Memory.h"
#include "wvs/config.h"
#include "wvs/util.h"
#include "ztl/ztl.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <windows.h>

// ---- BeiDou-ijl15 shims for Kaentake helpers used by this file ------------
#ifndef TO_PVOID
#define TO_PVOID(x) reinterpret_cast<void*>(static_cast<uintptr_t>(x))
#endif

inline void PatchMemory(void* addr, unsigned char* bytes, size_t size) {
    Memory::WriteByteArray(reinterpret_cast<DWORD>(addr), bytes, static_cast<int>(size));
}

static void LogMessage(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsprintf_s(buf, fmt, ap);
    va_end(ap);
    std::cout << "[invresize] " << buf << std::endl;
    FILE* f = nullptr;
    if (fopen_s(&f, "invresize_debug.txt", "a") == 0 && f) {
        fprintf(f, "%s\n", buf);
        fclose(f);
    }
}

static void ErrorMessage(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsprintf_s(buf, fmt, ap);
    va_end(ap);
    LogMessage("%s", buf);
    MessageBoxA(nullptr, buf, "Inventory resize", MB_OK | MB_ICONWARNING);
}

#ifndef LOG_ONCE
#define LOG_ONCE(...) do { \
    static bool s_once = false; \
    if (!s_once) { s_once = true; LogMessage(__VA_ARGS__); } \
} while (0)
#endif

namespace InvResize {

// ---- client addresses -------------------------------------------------------
constexpr uintptr_t kAddr_GetSlotRect       = 0x0081E2C8;
constexpr uintptr_t kAddr_GetSlotPosPoint   = 0x0081DB7E;
constexpr uintptr_t kAddr_OnCreate          = 0x0081C6C9;
constexpr uintptr_t kAddr_ToggleFull        = 0x0081E541;
constexpr uintptr_t kAddr_NarrowGetterCall  = 0x0081C4BE;
constexpr uintptr_t kAddr_Instance          = 0x00BED654;   // TSingleton<CUIItem>
constexpr uintptr_t kAddr_ScrollPosArray    = 0x00BF0EB4;   // int[5], per-tab scroll ROW
constexpr uintptr_t kAddr_CWvsContext       = 0x00BE7918;
constexpr uintptr_t kAddr_GetCharacterData  = 0x00425D0B;
constexpr uintptr_t kAddr_ZRefRelease       = 0x00428C44;
constexpr uintptr_t kAddr_SetWndCanvas      = 0x0041E527;   // ZRef<IWzCanvas>::operator=
constexpr uintptr_t kAddr_CWnd_Destroy      = 0x009E00AF;
constexpr uintptr_t kAddr_CWnd_CreateWnd    = 0x009DE4D2;
constexpr uintptr_t kAddr_CWnd_GetAbsLeft   = 0x009E03C5;
constexpr uintptr_t kAddr_CWnd_GetAbsTop    = 0x009E0447;
constexpr uintptr_t kAddr_CWnd_Invalidate   = 0x009E04C9;
constexpr uintptr_t kAddr_ApplySavedTab     = 0x0081D38D;
constexpr uintptr_t kAddr_InputSystem       = 0x00BEC33C;
constexpr uintptr_t kAddr_GetCursorPos      = 0x0059A388;
constexpr uintptr_t kAddr_SetCursorState    = 0x0059A6D9;

// ---- CUIItem field offsets --------------------------------------------------
constexpr int kOff_CreateId  = 0x14;    // nonzero only while the window is built
constexpr int kOff_Width     = 0x24;    // CWnd::m_width
constexpr int kOff_Height    = 0x28;    // CWnd::m_height
constexpr int kOff_BgCanvas  = 0x68;    // ZRef<IWzCanvas>, what CWnd::Draw blits
constexpr int kOff_CloseX    = 0x590;   // close-button x, W - 20
constexpr int kOff_FirstSlot = 0x5E0;   // 1-based index of the top-left visible slot
constexpr int kOff_TabIndex  = 0x5E4;   // 0..4
constexpr int kOff_TabArray  = 0x447;   // GW_CharacterData + 0x447 + 4*tab -> ZArray

// ---- grid geometry (the client's own numbers; see header) -------------------
constexpr int kIcon      = 32;
constexpr int kColPitch  = 36;
constexpr int kRowPitch  = 34;
constexpr int kGridLeft  = 8;    // ICON origin. The art's cell BORDER is at 7.
constexpr int kGridTop   = 50;

constexpr int kMinCols = 4,  kMaxCols = 16;
constexpr int kMinRows = 3,  kMaxRows = 14;

// Ceiling on visible cells. Two patched sites are signed imm8 fields -- the
// Draw loop's `add eax,0x18` at 0x0081DDF9 and the scroll-range bias
// `add eax,-0x16` at 0x0081D293, which takes -(cells-2). 120 keeps both inside
// [-128,127]. v83 servers cap a tab at 96 slots anyway, so cells beyond that
// only ever render empty.
constexpr int kMaxCells = 120;
static_assert(kMaxCells <= 127, "cells no longer fits the Draw loop's imm8");
static_assert(kMaxCells - 2 <= 128, "cells-2 no longer fits the scroll bias imm8");

constexpr int kGripThickness = 7;   // grabbable band inside the right / bottom edge

int WndW(int cols) { return kColPitch * cols + 31; }
int WndH(int rows) { return kRowPitch * rows + 85; }

// Half-pitch snapping: cols = round((W - 31) / 36), rows = round((H - 85) / 34).
// The boundary is half the 36px PITCH, not half the 32px icon -- the edge
// travels 18px past a cell boundary to gain a column, which locks the snap to
// the visible cell borders rather than to the icons.
int SnapCols(int wantW) { return (wantW - 13) / kColPitch; }
int SnapRows(int wantH) { return (wantH - 68) / kRowPitch; }

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- live state -------------------------------------------------------------
bool g_bInstalled = false;
int  g_nCols = kMinCols;          // what the patched immediates currently say
int  g_nRows = 6;
bool g_bLoaded = false;

int  g_pendCols = 0, g_pendRows = 0;   // requested from the WndProc, applied in Tick
bool g_bDragging = false;
int  g_nDragMode = 0;                  // 1 = right edge, 2 = bottom edge, 3 = corner
void* g_pDragWnd = nullptr;            // latched at grip-down; a mismatch aborts
int  g_nCursorState = 0;

constexpr int kCursor_Arrow  = 0;
constexpr int kCursor_Resize = 9;      // static up/down arrows; the client has no
                                       // diagonal or left/right resize sprite

// ---------------------------------------------------------------------------
// SEH leaves. The DLL builds with /EHsc, under which catch(...) does NOT catch
// access violations -- only __try/__except does. C2712 forbids __try in a
// function holding objects that unwind, so every guarded read is its own leaf
// with no smart pointers in scope.
// ---------------------------------------------------------------------------
void* SehInstance() {
    __try { return *reinterpret_cast<void**>(kAddr_Instance); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

int SehReadInt(void* p, int off) {
    __try { return *reinterpret_cast<int*>(reinterpret_cast<char*>(p) + off); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void SehWriteInt(void* p, int off, int v) {
    __try { *reinterpret_cast<int*>(reinterpret_cast<char*>(p) + off) = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Screen-space top-left of a CWnd. Both getters take the IUIMsgHandler
// sub-object at base+4.
void SehWndAbs(void* pBase, int& l, int& t) {
    l = 0; t = 0;
    __try {
        void* h = reinterpret_cast<char*>(pBase) + 4;
        l = reinterpret_cast<int(__thiscall*)(void*)>(kAddr_CWnd_GetAbsLeft)(h);
        t = reinterpret_cast<int(__thiscall*)(void*)>(kAddr_CWnd_GetAbsTop)(h);
    } __except (EXCEPTION_EXECUTE_HANDLER) { l = 0; t = 0; }
}

// Slot count of the currently selected tab, reproducing the client's own idiom
// from GetSlotRect 0x0081E2D4-0x0081E30D: GetCharacterData hands back an 8-byte
// ZRef out-param whose PAYLOAD IS AT +4, the array pointer lives at
// characterData + 0x447 + 4*tab, and the element count is the int immediately
// BEFORE the array data. CWvsContext holds the master reference, so the local
// one is released straight away and the raw pointer stays good.
int SehTabSlotCount(void* pThis) {
    __try {
        void* ctx = *reinterpret_cast<void**>(kAddr_CWvsContext);
        if (!ctx) return 0;
        void* out[2] = { nullptr, nullptr };
        void* ret = reinterpret_cast<void*(__thiscall*)(void*, void**)>(kAddr_GetCharacterData)(ctx, out);
        void* cd = *reinterpret_cast<void**>(reinterpret_cast<char*>(ret) + 4);
        if (out[1]) {
            reinterpret_cast<void(__thiscall*)(void**, int)>(kAddr_ZRefRelease)(out, 0);
            out[1] = nullptr;
        }
        if (!cd) return 0;
        const int nTab = *reinterpret_cast<int*>(reinterpret_cast<char*>(pThis) + kOff_TabIndex);
        if (nTab < 0 || nTab > 4) return 0;
        void* pArr = *reinterpret_cast<void**>(reinterpret_cast<char*>(cd) + kOff_TabArray + 4 * nTab);
        if (!pArr) return 0;
        const int n = *(reinterpret_cast<int*>(pArr) - 1);
        return (n < 0 || n > 100000) ? 0 : n;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool GetAbsCursor(POINT& sp) {
    sp.x = 0; sp.y = 0;
    void* pInput = *reinterpret_cast<void**>(kAddr_InputSystem);
    if (!pInput) return false;
    reinterpret_cast<void(__thiscall*)(void*, POINT*)>(kAddr_GetCursorPos)(pInput, &sp);
    return true;
}

void SetCursorState(int state) {
    void* pInput = *reinterpret_cast<void**>(kAddr_InputSystem);
    if (pInput) reinterpret_cast<void(__thiscall*)(void*, int)>(kAddr_SetCursorState)(pInput, state);
}

void Invalidate(void* pThis) {
    __try { reinterpret_cast<void(__thiscall*)(void*, const RECT*)>(kAddr_CWnd_Invalidate)(pThis, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}


// ===========================================================================
// The two replacements. Both keep the stock signature, the stock 1-based slot
// index space and the stock `ret 8`, so every caller is unaffected.
// ===========================================================================

// Shared core, so the two entry points cannot drift apart. nFirst / nLimit are
// hoisted by the caller: resolving them costs a GetCharacterData, and the
// hit-test walks up to kMaxCells slots.
bool SlotRectCore(int nPos, int nFirst, int nLimit, RECT* pOut) {
    if (nPos < nFirst || nPos >= nLimit) {
        SetRectEmpty(pOut);
        return false;
    }
    const int v   = nPos - nFirst;
    const int row = v / g_nCols;
    const int col = v % g_nCols;
    const int x   = kGridLeft + kColPitch * col;
    const int y   = kGridTop  + kRowPitch * row;
    SetRect(pOut, x, y, x + kIcon, y + kIcon);
    return true;
}

// The visible half-open slot range, [first, min(first + cells, tabSlotCount)).
// The second clamp is stock (0x0081E322-0x0081E32E) and is what stops
// past-capacity cells being drawn into or clicked, so it is reproduced.
void VisibleRange(void* pThis, int& nFirst, int& nLimit) {
    nFirst = SehReadInt(pThis, kOff_FirstSlot);
    const int nCount = SehTabSlotCount(pThis);
    nLimit = nFirst + g_nCols * g_nRows;
    if (nLimit > nCount) nLimit = nCount;
}

int __fastcall GetSlotRect_hook(void* pThis, void* /*edx*/, int nPos, RECT* pOut) {
    if (!pThis || !pOut) return 0;
    int nFirst = 0, nLimit = 0;
    VisibleRange(pThis, nFirst, nLimit);
    return SlotRectCore(nPos, nFirst, nLimit, pOut) ? 1 : 0;
}

// Returns a 1-based slot, 0 = miss. Same walk the stock one does
// (0x0081DBE6-0x0081DC11).
int __fastcall GetSlotPosFromPoint_hook(void* pThis, void* /*edx*/, int x, int y) {
    if (!pThis) return 0;
    int nFirst = 0, nLimit = 0;
    VisibleRange(pThis, nFirst, nLimit);

    POINT pt = { static_cast<LONG>(x), static_cast<LONG>(y) };
    for (int i = nFirst; i < nLimit; ++i) {
        RECT rc;
        if (!SlotRectCore(i, nFirst, nLimit, &rc)) continue;
        if (PtInRect(&rc, pt)) return i;
    }
    return 0;
}


// ===========================================================================
// Background compositor. See the header for why every band comes from
// FullBackgrnd; preview_invresize_bg.py renders the same recipe off-line.
// ===========================================================================
constexpr int kSrcW = 603;        // FullBackgrnd width
constexpr int kTilePeriod = 32;   // measured: each tiled band alternates two
                                  // 16px flat runs, so the true period is 32

constexpr int kA_H = 50, kA_Left = 81, kA_Right = 12, kA_TileX = 81;
constexpr int kB_CellX = 7, kB_RailW = 7, kB_GutterSrc = 596, kB_FrameSrc = 599, kB_FrameW = 4;
constexpr int kC_H = 37, kC_SrcY = 252, kC_Left = 31, kC_TileX = 31;
constexpr int kC_MesoSrc = 142, kC_MesoW = 30;
constexpr int kC_Right = kC_MesoW + kB_FrameW;   // 34

IWzCanvas* FullBackgrnd() {
    static bool bTried = false;
    static IWzCanvasPtr pFull;
    if (!bTried) {
        bTried = true;
        try {
            pFull = get_unknown(get_rm()->GetObjectA(
                const_cast<wchar_t*>(L"UI/UIWindow.img/Item/FullBackgrnd")));
        } catch (...) {
        }
        if (!pFull) {
            LOG_ONCE("invresize: UI/UIWindow.img/Item/FullBackgrnd is missing; the inventory "
                     "keeps its stock background and will look wrong at any size but 4x6");
        }
    }
    return pFull;
}

// Verbatim rect copy, alpha included. Source and destination extents are equal,
// so CopyEx does no scaling. CA_OVERWRITE (not CA_REMOVEALPHA) keeps the frame's
// soft drop shadow instead of flattening it to opaque.
void Blit(IWzCanvasPtr pDst, IWzCanvas* pSrc, int sx, int sy, int w, int h, int dx, int dy) {
    if (w <= 0 || h <= 0) return;
    pDst->CopyEx(dx, dy, pSrc, CANVAS_ALPHATYPE::CA_OVERWRITE, w, h, sx, sy, w, h);
}

// Repeat a kTilePeriod-wide run of source columns across [dx0, dx1).
void TileH(IWzCanvasPtr pDst, IWzCanvas* pSrc, int sx, int sy, int h, int dx0, int dx1, int dy) {
    for (int x = dx0; x < dx1; x += kTilePeriod) {
        const int w = (dx1 - x < kTilePeriod) ? (dx1 - x) : kTilePeriod;
        Blit(pDst, pSrc, sx, sy, w, h, x, dy);
    }
}

IWzCanvasPtr BuildBackground(int cols, int rows) {
    IWzCanvas* pF = FullBackgrnd();
    if (!pF) return nullptr;

    const int W = WndW(cols), H = WndH(rows);
    IWzCanvasPtr pDst;
    try {
        PcCreateObject<IWzCanvasPtr>(L"Canvas", pDst, nullptr);
        if (!pDst) return nullptr;
        // Explicitly 32-bit so the band blits are format-independent. A canvas
        // from PcCreateObject has an empty pixel format until Create sets one.
        pDst->Create(static_cast<unsigned>(W), static_cast<unsigned>(H),
                     Ztl_variant_t(0), Ztl_variant_t(static_cast<int>(CP_A8R8G8B8)));

        // Band A -- title bar and tab plate, source rows 0..49.
        Blit(pDst, pF, 0, 0, kA_Left, kA_H, 0, 0);
        TileH(pDst, pF, kA_TileX, 0, kA_H, kA_Left, W - kA_Right, 0);
        Blit(pDst, pF, kSrcW - kA_Right, 0, kA_Right, kA_H, W - kA_Right, 0);

        // Band B -- one 36x34 source cell, repeated into every cell of the grid.
        for (int r = 0; r < rows; ++r) {
            const int y = kGridTop + kRowPitch * r;
            Blit(pDst, pF, 0, kGridTop, kB_RailW, kRowPitch, 0, y);
            for (int c = 0; c < cols; ++c) {
                Blit(pDst, pF, kB_CellX, kGridTop, kColPitch, kRowPitch,
                     kB_CellX + kColPitch * c, y);
            }
            // Scrollbar-track gutter is one flat column replicated, then the
            // 4-column right frame: rule, white, shadow, transparent.
            const int gx0 = kB_CellX + kColPitch * cols;
            for (int x = gx0; x < W - kB_FrameW; ++x) {
                Blit(pDst, pF, kB_GutterSrc, kGridTop, 1, kRowPitch, x, y);
            }
            Blit(pDst, pF, kB_FrameSrc, kGridTop, kB_FrameW, kRowPitch, W - kB_FrameW, y);
        }

        // Band C -- the meso row. Drawn LAST because it deliberately overwrites
        // band B's trailing 2 rows; that overlap is why H is 34r+85 and not
        // 34r+87. FullBackgrnd's bottom band is the narrow one with flat filler
        // injected between the "mesos" label (ends at col 171) and the frame
        // (599..602), so the label is lifted out as its own cap and butted
        // straight onto the frame, reproducing the stock layout at 4x6.
        const int cy = H - kC_H;
        Blit(pDst, pF, 0, kC_SrcY, kC_Left, kC_H, 0, cy);
        TileH(pDst, pF, kC_TileX, kC_SrcY, kC_H, kC_Left, W - kC_Right, cy);
        Blit(pDst, pF, kC_MesoSrc, kC_SrcY, kC_MesoW, kC_H, W - kC_Right, cy);
        Blit(pDst, pF, kB_FrameSrc, kC_SrcY, kB_FrameW, kC_H, W - kB_FrameW, cy);
        return pDst;
    } catch (...) {
        // A failed band leaves the stock canvas in place rather than a blank
        // window: InstallBackground only overwrites +0x68 on success.
        return nullptr;
    }
}

// Install the generated canvas into CWnd+0x68. 0x0041E527 is
// ZRef<IWzCanvas>::operator=: ecx is the holder, and the argument is the ADDRESS
// of a smart pointer (mov eax,[ebp+8] / mov eax,[eax]), which it QIs for the
// canvas IID at 0x00BD82F8 -- writing the result back through that same address
// -- then releases whatever the holder had. Passing the interface directly
// instead of its address would dereference a vtable pointer as a pointer.
void SehInstallCanvas(void* pThis, IWzCanvas* pCanvas) {
    __try {
        IUnknown* raw = pCanvas;
        reinterpret_cast<void(__thiscall*)(void*, IUnknown**)>(kAddr_SetWndCanvas)(
            reinterpret_cast<char*>(pThis) + kOff_BgCanvas, &raw);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void InstallBackground(void* pThis, int cols, int rows) {
    // Cached on (cols, rows): the window is destroyed and rebuilt on every snap
    // step of a drag, and recompositing hundreds of blits per step is wasted
    // work when the size has not actually changed since the last build.
    static IWzCanvasPtr s_pCached;
    static int s_nCachedCols = -1, s_nCachedRows = -1;

    if (!s_pCached || s_nCachedCols != cols || s_nCachedRows != rows) {
        IWzCanvasPtr p = BuildBackground(cols, rows);
        if (!p) return;                       // keep whatever OnCreate installed
        s_pCached = p;
        s_nCachedCols = cols;
        s_nCachedRows = rows;
    }
    SehInstallCanvas(pThis, s_pCached);
}


// ===========================================================================
// Layout immediates. Each is an operand inside the stock code, rewritten in
// place before every CreateWnd so the window's own OnCreate lays its children
// out at the new size. Offsets point at the IMMEDIATE FIELD, not the
// instruction.
// ===========================================================================
constexpr uintptr_t kImm_CloseX    = 0x0081C4D4 + 1;   // add eax, 0x9B    -> W - 20
constexpr uintptr_t kImm_WndH      = 0x0081C512 + 1;   // push 0x121       -> H
constexpr uintptr_t kImm_WndW      = 0x0081C517 + 1;   // add eax, 0xAF    -> W
constexpr uintptr_t kImm_TabW      = 0x0081C967 + 1;   // push 0xAA        -> W - 5
constexpr uintptr_t kImm_ScrollLen = 0x0081CCB9 + 1;   // push 0xC8        -> 34r - 4
constexpr uintptr_t kImm_ScrollX   = 0x0081CCC0 + 1;   // push 0x98        -> 8 + 36c
constexpr uintptr_t kImm_CoinY     = 0x0081CD67 + 1;   // push 0x10A       -> H - 23
constexpr uintptr_t kImm_MesoX     = 0x0081DCD8 + 1;   // mov esi, 0x8A    -> W - 37
constexpr uintptr_t kImm_MesoY     = 0x0081DD5F + 1;   // push 0x10A       -> H - 23
constexpr uintptr_t kImm_DrawLimit = 0x0081DDF9 + 2;   // add eax, 0x18    -> cells      (imm8)
constexpr uintptr_t kImm_RangeBias = 0x0081D293 + 2;   // add eax, -0x16   -> -(cells-2) (imm8)
constexpr uintptr_t kImm_RangeDiv  = 0x0081D296 + 1;   // push 4           -> cols       (imm8)

// The two scroll-stride sites encode the column count as an SIB SCALE, which can
// only ever be 1/2/4/8, so they cannot be re-immediated -- the 7-byte lea is
// rewritten to a 4-byte imul plus inc plus 3 nops. Both are exact-length, and
// neither successor instruction reads flags (0x0081D08A is push ebx,
// 0x0081D2CB is mov [esi+0x5E0],eax), which matters because lea does not set
// flags and imul/inc do.
constexpr uintptr_t kSite_StrideA = 0x0081D083;   // lea eax,[edi*4+1] -> imul eax,edi,cols; inc eax
constexpr uintptr_t kSite_StrideB = 0x0081D2C4;   // lea eax,[eax*4+1] -> imul eax,eax,cols; inc eax

void WriteStride(uintptr_t addr, unsigned char modrm, int cols) {
    const unsigned char code[7] = {
        0x6B, modrm, static_cast<unsigned char>(cols),   // imul r32, r/m32, imm8
        0x40,                                            // inc eax
        0x90, 0x90, 0x90,
    };
    PatchMemory(TO_PVOID(addr), const_cast<unsigned char*>(code), sizeof(code));
}

void ApplyLayoutConstants(int cols, int rows) {
    const int W = WndW(cols), H = WndH(rows);

    Patch4(kImm_CloseX,    static_cast<unsigned>(W - 20));
    Patch4(kImm_WndH,      static_cast<unsigned>(H));
    Patch4(kImm_WndW,      static_cast<unsigned>(W));
    Patch4(kImm_TabW,      static_cast<unsigned>(W - 5));
    Patch4(kImm_ScrollLen, static_cast<unsigned>(kRowPitch * rows - 4));
    Patch4(kImm_ScrollX,   static_cast<unsigned>(kGridLeft + kColPitch * cols));
    Patch4(kImm_CoinY,     static_cast<unsigned>(H - 23));
    Patch4(kImm_MesoX,     static_cast<unsigned>(W - 37));
    Patch4(kImm_MesoY,     static_cast<unsigned>(H - 23));

    Patch1(kImm_DrawLimit, static_cast<unsigned char>(cols * rows));
    Patch1(kImm_RangeBias, static_cast<unsigned char>(0u - static_cast<unsigned>(cols * rows - 2)));
    Patch1(kImm_RangeDiv,  static_cast<unsigned char>(cols));

    WriteStride(kSite_StrideA, 0xC7, cols);   // imul eax, edi, cols
    WriteStride(kSite_StrideB, 0xC0, cols);   // imul eax, eax, cols

    g_nCols = cols;
    g_nRows = rows;
}


// ===========================================================================
// Resize commit, modelled on the client's own ToggleFull (0x0081E541), which
// does exactly this on every click of the expand button: write the close-button
// x, Destroy, then CreateWnd at the new size. That way child layout is simply
// "whatever OnCreate does", so there is no second copy of the layout to keep in
// sync.
// ===========================================================================
void ApplySize(int cols, int rows) {
    void* pThis = SehInstance();
    if (!pThis) return;

    cols = Clamp(cols, kMinCols, kMaxCols);
    rows = Clamp(rows, kMinRows, kMaxRows);
    if (cols * rows > kMaxCells) return;

    // Keep the window where it is instead of where CConfig remembers it:
    // growing from a fixed top-left is what makes an edge drag feel anchored.
    int absL = 0, absT = 0;
    SehWndAbs(pThis, absL, absT);

    ApplyLayoutConstants(cols, rows);

    // Per-tab scroll rows are stored as ROW indices for the OLD column count, so
    // they are meaningless now. Reset rather than rescale: the visible window
    // has changed anyway, and a stale row can put m_nFirstSlot past the end.
    __try {
        int* pScroll = reinterpret_cast<int*>(kAddr_ScrollPosArray);
        for (int i = 0; i < 5; ++i) pScroll[i] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    SehWriteInt(pThis, kOff_FirstSlot, 1);

    const int W = WndW(cols), H = WndH(rows);
    SehWriteInt(pThis, kOff_CloseX, W - 20);

    __try {
        reinterpret_cast<void(__thiscall*)(void*)>(kAddr_CWnd_Destroy)(pThis);
        reinterpret_cast<void(__thiscall*)(void*, int, int, int, int, int, int, void*, int)>(
            kAddr_CWnd_CreateWnd)(pThis, absL, absT, W, H, 10, 1, nullptr, 1);
        // Re-apply the selected tab to the freshly built CCtrlTab. 0x0081D38D is
        // the client's own one-liner for it (push [ecx+0x5A4], the saved tab
        // option, into the CCtrlTab at [ecx+0x5B4]); CWvsContext::OnInventoryGrow
        // calls exactly this at 0x00A1F8DA after the inventory changes shape.
        // Without it the strip renders tab 0 while m_nTabIndex still says 2.
        reinterpret_cast<void(__thiscall*)(void*)>(kAddr_ApplySavedTab)(pThis);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    Invalidate(pThis);
}

void SaveSize() {
    CConfig* pCfg = CConfig::GetInstance();
    if (!pCfg) return;
    pCfg->SetOpt_Int(CConfig::CHARACTER_OPT, "invCols", g_nCols);
    pCfg->SetOpt_Int(CConfig::CHARACTER_OPT, "invRows", g_nRows);
}

// Deferred: CHARACTER_OPT is only populated by CConfig::LoadCharacter
// (0x0049D0B6), which runs long after the mod attaches.
void LoadSizeOnce() {
    if (g_bLoaded) return;
    CConfig* pCfg = CConfig::GetInstance();
    if (!pCfg) return;
    g_bLoaded = true;
    const int c = pCfg->GetOpt_Int(CConfig::CHARACTER_OPT, "invCols", kMinCols, kMinCols, kMaxCols);
    const int r = pCfg->GetOpt_Int(CConfig::CHARACTER_OPT, "invRows", 6,        kMinRows, kMaxRows);
    if (c * r <= kMaxCells && (c != g_nCols || r != g_nRows)) {
        ApplyLayoutConstants(c, r);
    }
}


// ===========================================================================
// Hooks
// ===========================================================================
typedef void(__thiscall* t_OnCreate)(void*, void*);
auto CUIItem_OnCreate = reinterpret_cast<t_OnCreate>(kAddr_OnCreate);

// Deliberately does NOT call LoadSizeOnce(). The constructor reads the size
// immediates at its CreateWnd CALL SITE (0x0081C512 / 0x0081C517), which is
// before CreateWnd -> PreCreateWnd -> OnCreate runs. Changing the grid here
// would lay the children out at the new size inside a window already built at
// the old one. The config load and any catch-up resize happen in Tick.
void __fastcall OnCreate_hook(void* pThis, void* /*edx*/, void* pData) {
    CUIItem_OnCreate(pThis, pData);
    // The stock OnCreate has already installed Item/backgrnd at +0x68, so a
    // failure below leaves a real (if wrongly sized) window rather than a hole.
    // Derive the grid from the window the client actually built, not from the
    // globals, so a transient CUIItem built at some other size stays coherent.
    const int W = SehReadInt(pThis, kOff_Width);
    const int H = SehReadInt(pThis, kOff_Height);
    if (W == WndW(g_nCols) && H == WndH(g_nRows)) {
        InstallBackground(pThis, g_nCols, g_nRows);
    }
}

// Stock ToggleFull swapped m_bFull, which this file forces to 0 permanently.
// The button (id 0x7D5) becomes a quick 4 <-> 8 column toggle, the nearest
// thing to its old "make the inventory wide" meaning.
void __fastcall ToggleFull_hook(void* /*pThis*/, void* /*edx*/) {
    g_pendCols = (g_nCols >= 8) ? kMinCols : 8;
    g_pendRows = g_nRows;
}


// ===========================================================================
// Grip input.
//
// There is no Win32 mouse capture in this client, so the drag starts from the
// WndProc (CWndMan::TranslateMessage) and continues from the per-frame update.
// The window is NOT rebuilt inside the message handler: the handler records a
// pending size and the tick commits it, keeping CWnd construction on the main
// loop.
//
// Grips sit at y >= kGridTop, clear of the title bar: the vtable's drag-area
// gate (slot +0x24 -> 0x0092C5E4) returns 1 only for y < 25, and
// CWndMan::ProcessMouse only sets m_pMoveWnd when that gate passes, so a grip
// drag can never be mistaken for a window move.
// ===========================================================================
// 0 = none, 1 = right, 2 = bottom, 3 = corner.
int GripAt(int W, int H, int rx, int ry) {
    const bool bRight  = rx >= W - kGripThickness && rx < W && ry >= kGridTop && ry < H;
    const bool bBottom = ry >= H - kGripThickness && ry < H && rx >= kB_RailW && rx < W;
    if (bRight && bBottom) return 3;
    if (bRight)  return 1;
    if (bBottom) return 2;
    return 0;
}

// The live grip under the cursor, in window coordinates. Returns 0 when the
// inventory is absent, not laid out, or the cursor is elsewhere.
int GripUnderCursor(void*& pOutWnd, int& outRelX, int& outRelY) {
    pOutWnd = nullptr; outRelX = 0; outRelY = 0;
    void* pThis = SehInstance();
    if (!pThis) return 0;
    const int W = SehReadInt(pThis, kOff_Width);
    const int H = SehReadInt(pThis, kOff_Height);
    if (W < 80 || W > 2000 || H < 80 || H > 2000) return 0;
    POINT sp;
    if (!GetAbsCursor(sp)) return 0;
    int absL = 0, absT = 0;
    SehWndAbs(pThis, absL, absT);
    if (absL <= 0 && absT <= 0) return 0;
    const int rx = sp.x - absL, ry = sp.y - absT;
    pOutWnd = pThis; outRelX = rx; outRelY = ry;
    return GripAt(W, H, rx, ry);
}

// True only while the inventory window is actually built. This gate is load
// bearing: the singleton at 0x00BED654 outlives a close, CWnd::Destroy
// early-outs on an already-destroyed window, and CWnd::CreateWnd would then
// REOPEN it -- so without this, ReconcileLiveWindow would pop the inventory
// back open every frame after the player closed it.
//
// +0x14 is the client's OWN "am I created" predicate, not a guess:
//   CreateWnd  0x009DE4DF  inc [0xBF1604] / mov eax,[0xBF1604] / mov [ebx+0x14],eax
//   Destroy    0x009E00C2  cmp [esi+0x14],ebx / je <end>      (its own early-out)
//              0x009E013D  mov [esi+0x14],ebx                 (cleared, ebx = 0)
// Do NOT use m_pLayer (+0x18) for this. Destroy does release it, but CreateWnd
// branches on it being non-null at 0x009DE51D for an unrelated reason, so its
// value is not a clean created/destroyed signal.
bool IsWindowLive(void* pThis) {
    if (!pThis) return false;
    return SehReadInt(pThis, kOff_CreateId) != 0;
}

// Catch-up resize: the config is only readable once CConfig::LoadCharacter has
// run, and the constructor bakes the size in before OnCreate, so the first
// window of a session can be built at the wrong grid.
void ReconcileLiveWindow() {
    if (g_bDragging || g_pendCols) return;
    void* pThis = SehInstance();
    if (!IsWindowLive(pThis)) return;
    const int W = SehReadInt(pThis, kOff_Width);
    const int H = SehReadInt(pThis, kOff_Height);
    if (W <= 0 || H <= 0) return;
    if (W == WndW(g_nCols) && H == WndH(g_nRows)) return;
    ApplySize(g_nCols, g_nRows);
}

void AbortDrag() {
    if (!g_bDragging) return;
    g_bDragging = false;
    g_nDragMode = 0;
    g_pDragWnd = nullptr;
    g_pendCols = g_pendRows = 0;
    if (g_nCursorState) { SetCursorState(kCursor_Arrow); g_nCursorState = 0; }
}

} // namespace InvResize


// Call from your CWndMan::TranslateMessage hook. Returns true when it consumed
// the message (it also rewrites msg to WM_NULL, so passing it on afterwards is
// harmless).
bool InvResize_HandleMouseMessage(UINT& msg, WPARAM, LPARAM, LRESULT*) {
    using namespace InvResize;
    if (!g_bInstalled) return false;

    if (msg == WM_LBUTTONDOWN && !g_bDragging) {
        void* pWnd = nullptr; int rx = 0, ry = 0;
        const int nGrip = GripUnderCursor(pWnd, rx, ry);
        if (nGrip) {
            g_bDragging = true;
            g_nDragMode = nGrip;
            g_pDragWnd  = pWnd;      // latched; the tick aborts if the singleton moves
            // Eat the click so the engine cannot start a window move or route it
            // into the grid underneath.
            msg = WM_NULL;
            return true;
        }
        return false;
    }

    if (msg == WM_LBUTTONUP && g_bDragging) {
        g_bDragging = false;
        g_nDragMode = 0;
        g_pDragWnd  = nullptr;
        if (g_nCursorState) { SetCursorState(kCursor_Arrow); g_nCursorState = 0; }
        SaveSize();
        msg = WM_NULL;
        return true;
    }

    if (msg == WM_MOUSEMOVE && g_bDragging) {
        void* pThis = SehInstance();
        if (!pThis || pThis != g_pDragWnd) { AbortDrag(); return false; }
        POINT sp;
        if (!GetAbsCursor(sp)) return false;
        int absL = 0, absT = 0;
        SehWndAbs(pThis, absL, absT);

        int cols = g_nCols, rows = g_nRows;
        if (g_nDragMode == 1 || g_nDragMode == 3) cols = SnapCols(sp.x - absL + 1);
        if (g_nDragMode == 2 || g_nDragMode == 3) rows = SnapRows(sp.y - absT + 1);
        cols = Clamp(cols, kMinCols, kMaxCols);
        rows = Clamp(rows, kMinRows, kMaxRows);

        // Never let the pair exceed the imm8 ceiling. Give way on the axis the
        // user is NOT dragging, so the edge they are holding keeps following the
        // cursor for as long as it can.
        while (cols * rows > kMaxCells) {
            if (g_nDragMode == 1 && rows > kMinRows)      --rows;
            else if (g_nDragMode == 2 && cols > kMinCols) --cols;
            else if (cols >= rows && cols > kMinCols)     --cols;
            else if (rows > kMinRows)                     --rows;
            else break;
        }
        // Keep the whole window on screen.
        const int sw = get_screen_width(), sh = get_screen_height();
        while (cols > kMinCols && absL + WndW(cols) > sw - 8) --cols;
        while (rows > kMinRows && absT + WndH(rows) > sh - 8) --rows;

        if (cols != g_nCols || rows != g_nRows) { g_pendCols = cols; g_pendRows = rows; }
        // Not consumed: the move flows on so the cursor keeps tracking.
    }
    return false;
}


// Call once per frame from your CWvsApp::CallUpdate hook.
void InvResize_Tick() {
    using namespace InvResize;
    if (!g_bInstalled) return;

    LoadSizeOnce();

    // Lifetime safety. The drag is driven by GetAsyncKeyState and a latched
    // pointer, both decoupled from the window's existence: closing the inventory
    // with its hotkey, a channel change or a shop entry mid-drag would otherwise
    // have the next tick calling CWnd::Destroy on freed memory.
    if (g_bDragging) {
        void* pNow = SehInstance();
        if (!pNow || pNow != g_pDragWnd || !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            const bool bHadPending = g_pendCols != 0;
            AbortDrag();
            if (bHadPending) SaveSize();   // the button-up was lost, not the size
        }
    }

    if (g_pendCols && (g_pendCols != g_nCols || g_pendRows != g_nRows)) {
        ApplySize(g_pendCols, g_pendRows);
    }
    g_pendCols = g_pendRows = 0;

    ReconcileLiveWindow();

    // Cursor feedback runs from here rather than from WM_MOUSEMOVE: setting the
    // cursor inside the mouse-move path freezes it. Only ever touched while over
    // a grip or dragging, so no other window's cursor is stomped.
    int want = 0;
    if (g_bDragging) {
        want = kCursor_Resize;
    } else {
        void* pWnd = nullptr; int rx = 0, ry = 0;
        if (GripUnderCursor(pWnd, rx, ry)) want = kCursor_Resize;
    }
    if (want) {
        SetCursorState(kCursor_Resize);
        g_nCursorState = kCursor_Resize;
    } else if (g_nCursorState) {
        SetCursorState(kCursor_Arrow);
        g_nCursorState = 0;
    }
}


namespace InvResize {

// Entry bytes of every site this file writes. All-or-nothing: a half-applied
// resize is worse than none, because a grid whose hit-test and draw disagree
// hands items to the wrong slots.
bool TargetsMatch() {
    struct GUARD {
        uintptr_t uAddress;
        const unsigned char* pBytes;
        size_t uSize;
        const char* sName;
    };
    static const unsigned char kGetSlotRect[]  = { 0x55, 0x8B, 0xEC, 0x51, 0x51, 0x56, 0x57 };
    static const unsigned char kHitTest[]      = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x56, 0x57 };
    static const unsigned char kOnCreate[]     = { 0xB8, 0xEA, 0x1F, 0xAC, 0x00, 0xE8, 0xC5, 0x44, 0x24, 0x00 };
    static const unsigned char kToggleFull[]   = { 0x55, 0x8B, 0xEC, 0x51, 0x51, 0x56, 0x33, 0xC0, 0x8B, 0xF1 };
    static const unsigned char kNarrowCall[]   = { 0xE8, 0x17, 0x31, 0xC8, 0xFF };            // call 0x0049F5DA
    static const unsigned char kCloseX[]       = { 0x05, 0x9B, 0x00, 0x00, 0x00 };            // add eax, 0x9B
    static const unsigned char kWndH[]         = { 0x68, 0x21, 0x01, 0x00, 0x00 };            // push 0x121
    static const unsigned char kWndW[]         = { 0x05, 0xAF, 0x00, 0x00, 0x00 };            // add eax, 0xAF
    static const unsigned char kTabW[]         = { 0x68, 0xAA, 0x00, 0x00, 0x00 };            // push 0xAA
    static const unsigned char kScrollLen[]    = { 0x68, 0xC8, 0x00, 0x00, 0x00 };            // push 0xC8
    static const unsigned char kScrollX[]      = { 0x68, 0x98, 0x00, 0x00, 0x00 };            // push 0x98
    static const unsigned char kCoinY[]        = { 0x68, 0x0A, 0x01, 0x00, 0x00 };            // push 0x10A
    static const unsigned char kMesoX[]        = { 0xBE, 0x8A, 0x00, 0x00, 0x00 };            // mov esi, 0x8A
    static const unsigned char kMesoY[]        = { 0x68, 0x0A, 0x01, 0x00, 0x00 };            // push 0x10A
    static const unsigned char kDrawLimit[]    = { 0x83, 0xC0, 0x18, 0x3B, 0xC1 };            // add eax,0x18 / cmp eax,ecx
    static const unsigned char kRangeBias[]    = { 0x83, 0xC0, 0xEA, 0x6A, 0x04 };            // add eax,-0x16 / push 4
    static const unsigned char kStrideA[]      = { 0x8D, 0x04, 0xBD, 0x01, 0x00, 0x00, 0x00 };  // lea eax,[edi*4+1]
    static const unsigned char kStrideB[]      = { 0x8D, 0x04, 0x85, 0x01, 0x00, 0x00, 0x00 };  // lea eax,[eax*4+1]

    static const GUARD aGuards[] = {
        { kAddr_GetSlotRect,      kGetSlotRect, sizeof(kGetSlotRect), "GetSlotRect" },
        { kAddr_GetSlotPosPoint,  kHitTest,     sizeof(kHitTest),     "GetSlotPositionFromPoint" },
        { kAddr_OnCreate,         kOnCreate,    sizeof(kOnCreate),    "OnCreate" },
        { kAddr_ToggleFull,       kToggleFull,  sizeof(kToggleFull),  "ToggleFull" },
        { kAddr_NarrowGetterCall, kNarrowCall,  sizeof(kNarrowCall),  "narrow-mode config read" },
        { 0x0081C4D4,             kCloseX,      sizeof(kCloseX),      "close button x" },
        { 0x0081C512,             kWndH,        sizeof(kWndH),        "window height" },
        { 0x0081C517,             kWndW,        sizeof(kWndW),        "window width" },
        { 0x0081C967,             kTabW,        sizeof(kTabW),        "tab strip width" },
        { 0x0081CCB9,             kScrollLen,   sizeof(kScrollLen),   "scrollbar length" },
        { 0x0081CCC0,             kScrollX,     sizeof(kScrollX),     "scrollbar x" },
        { 0x0081CD67,             kCoinY,       sizeof(kCoinY),       "coin button y" },
        { 0x0081DCD8,             kMesoX,       sizeof(kMesoX),       "meso right-align x" },
        { 0x0081DD5F,             kMesoY,       sizeof(kMesoY),       "meso y" },
        { 0x0081DDF9,             kDrawLimit,   sizeof(kDrawLimit),   "draw loop limit" },
        { 0x0081D293,             kRangeBias,   sizeof(kRangeBias),   "scroll range bias" },
        { kSite_StrideA,          kStrideA,     sizeof(kStrideA),     "scroll stride (SetTab)" },
        { kSite_StrideB,          kStrideB,     sizeof(kStrideB),     "scroll stride (OnScroll)" },
    };
    for (const auto& g : aGuards) {
        if (memcmp(reinterpret_cast<void*>(g.uAddress), g.pBytes, g.uSize) != 0) {
            ErrorMessage("Inventory resize: %s at 0x%08X does not match - skipping "
                         "(the inventory keeps its stock fixed 4x6 window).",
                         g.sName, g.uAddress);
            return false;
        }
    }
    return true;
}

} // namespace InvResize


void AttachInvResizeMod() {
    using namespace InvResize;
    if (!TargetsMatch()) {
        return;
    }

    // Force narrow mode BEFORE anything can construct a CUIItem.
    static const unsigned char kXorEaxNops[] = { 0x33, 0xC0, 0x90, 0x90, 0x90 };
    PatchMemory(TO_PVOID(kAddr_NarrowGetterCall), const_cast<unsigned char*>(kXorEaxNops),
                sizeof(kXorEaxNops));

    // Seed the immediates at the stock 4x6 so the very first window is correct
    // even if the config read has not happened yet.
    ApplyLayoutConstants(kMinCols, 6);

    PatchJmp(kAddr_GetSlotRect,     CastHook(&GetSlotRect_hook));
    PatchJmp(kAddr_GetSlotPosPoint, CastHook(&GetSlotPosFromPoint_hook));
    PatchJmp(kAddr_ToggleFull,      CastHook(&ToggleFull_hook));

    ATTACH_HOOK(CUIItem_OnCreate, OnCreate_hook);

    g_bInstalled = true;
    LogMessage("Inventory resize: ready (grid %d-%d cols x %d-%d rows, max %d cells; "
               "narrow mode forced at 0x%08X)",
               kMinCols, kMaxCols, kMinRows, kMaxRows, kMaxCells, kAddr_NarrowGetterCall);
}
