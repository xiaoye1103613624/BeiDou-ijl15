#include "stdafx.h"
#include "DropItemAuraApi.h"
#include "compat/hook.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/field.h"
#include "compat/WzLib/IWzGr2DLayer.h"
#include "compat/ztl/zcom.h"

#include <unordered_map>
#include <vector>

namespace {
constexpr uintptr_t kOnDropEnterFieldJmp = 0x00506385;
constexpr uintptr_t kOnDropEnterFieldRet = 0x0050638A;
constexpr uintptr_t kDecode1             = 0x004065F3;
constexpr uintptr_t kDropDtor            = 0x0050ACA8;
constexpr uintptr_t kLoadSingleLayer     = 0x0043EEFC;
constexpr uintptr_t kCreateLayer         = 0x00426C7E;
constexpr unsigned long kAuraFrameMs         = 250;
constexpr int           kMaxFrames           = 4;   // loop at most 4 frames (not 16)
constexpr int           kMinFrames           = 2;
constexpr int           kMaxLayerLoadsPerTick = 2;
constexpr int           kMaxPendingAdvancePerTick = 1;
constexpr int           kMaxPendingAuras     = 32;
constexpr int           kMaxConcurrentAuras  = 32;
constexpr unsigned long kSpriteRetryMs       = 16;
constexpr unsigned long kPendingGiveUpMs     = 8000;
// Reference itemaura.cpp / dda14e8a: overlay=0 root canvas, packet dropto foot, z=-1.
constexpr int           kAuraLayerZ          = -1;

typedef unsigned char(__thiscall* Decode1Fn)(void*);
const Decode1Fn Decode1 = reinterpret_cast<Decode1Fn>(kDecode1);

typedef void*(__cdecl* LoadSingleLayerFn)(void**, const wchar_t*, int, int, int, int, int, int, int, int*);
const LoadSingleLayerFn LoadSingleLayer = reinterpret_cast<LoadSingleLayerFn>(kLoadSingleLayer);

const wchar_t* g_gradeNames[4] = {L"Rare", L"Epic", L"Unique", L"Legendary"};

static const int kNoZOverride = INT_MIN;
static int g_nextLayerZ = kNoZOverride;
static BYTE* s_createLayerTramp = nullptr;

static inline void SetNextZ(int z) { g_nextLayerZ = z; }
static inline void ClearNextZ() { g_nextLayerZ = kNoZOverride; }

struct CreateLayerHook {
    int** hook(int** ppOut, int nLeft, int nTop, int uWidth, int uHeight,
            int z, unsigned long* vCanvas, int* dwFilter) {
        if (g_nextLayerZ != kNoZOverride) {
            z = g_nextLayerZ;
        }
        typedef int**(__fastcall* TrampFn)(void*, void*, int**, int, int, int, int, int, unsigned long*, int*);
        return reinterpret_cast<TrampFn>(s_createLayerTramp)(
                this, nullptr, ppOut, nLeft, nTop, uWidth, uHeight, z, vCanvas, dwFilter);
    }
};

static bool IsReadablePtr(const void* ptr, size_t bytes) {
    if (!ptr) {
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) {
        return false;
    }
    if (mbi.State != MEM_COMMIT) {
        return false;
    }
    const DWORD protect = mbi.Protect & 0xFF;
    if (protect == PAGE_NOACCESS || protect == PAGE_GUARD) {
        return false;
    }
    const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    const uintptr_t ptrEnd = reinterpret_cast<uintptr_t>(ptr) + bytes;
    return ptrEnd <= regionEnd;
}

static bool IsValidComLayerPtr(void* layer) {
    if (!layer) {
        return false;
    }
    __try {
        void* const* vtable = *reinterpret_cast<void* const* const*>(layer);
        if (!vtable || !IsReadablePtr(vtable, sizeof(void*) * 8)) {
            return false;
        }
        for (int i = 0; i < 4; ++i) {
            const void* fn = vtable[i];
            if (!fn || !IsReadablePtr(fn, 1)) {
                return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void LayerSetVisible(void* layer, bool visible) {
    if (!IsValidComLayerPtr(layer)) {
        return;
    }
    __try {
        // Reference itemaura.cpp: toggle alpha via Putcolor, not put_visible.
        static_cast<IWzGr2DLayer*>(layer)->Putcolor(visible ? 0xFFFFFFFFu : 0u);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void LayerPutZ(void* layer, int z) {
    if (!IsValidComLayerPtr(layer)) {
        return;
    }
    __try {
        static_cast<IWzGr2DLayer*>(layer)->put_z(z);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void COMRelease(void* layer) {
    if (!layer) {
        return;
    }
    LayerSetVisible(layer, false);
    __try {
        reinterpret_cast<void(__thiscall*)(void*)>(
                (*reinterpret_cast<unsigned long**>(layer))[2])(layer);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool TryLoadSingleLayer(void** ppOut, const wchar_t* path, int x, int y, int z) {
    *ppOut = nullptr;
    __try {
        // Reference itemaura.cpp: overlay=0 (root canvas), absolute dropX/dropY.
        LoadSingleLayer(ppOut, path, 0, 0, x, y, 0, z, 255, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *ppOut = nullptr;
        return false;
    }
    if (!*ppOut) {
        return false;
    }
    LayerPutZ(*ppOut, z);
    return IsValidComLayerPtr(*ppOut);
}

static int g_layerLoadsThisTick = 0;

static bool SafeLoadLayer(void** ppOut, const wchar_t* path, int x, int y) {
    *ppOut = nullptr;
    if (g_layerLoadsThisTick >= kMaxLayerLoadsPerTick) {
        return false;
    }
    ++g_layerLoadsThisTick;
    SetNextZ(kAuraLayerZ);
    const bool ok = TryLoadSingleLayer(ppOut, path, x, y, kAuraLayerZ);
    ClearNextZ();
    return ok;
}

struct AuraState {
    void* pDrop = nullptr;
    int objectId = 0;
    int nIndex = 0;
    int dropX = 0;
    int dropY = 0;
    int frameIdx = 0;
    int nFrames = 0;
    unsigned long lastFrameTick = 0;
    unsigned long expireTick = 0;
    void* pFronts[kMaxFrames]{};
};

struct PendingAura {
    void* pDrop = nullptr;
    int objectId = 0;
    int nIndex = 0;
    int dropX = 0;
    int dropY = 0;
    unsigned long readyTick = 0;
    unsigned long queuedTick = 0;
    AuraState building{};
    int nextFrame = 0;
};

static std::unordered_map<void*, AuraState> g_auras;
static std::vector<PendingAura> g_pending;
static void* g_trackedField = nullptr;
static bool g_hooksAttached = false;
static bool g_packetHandlerRegistered = false;

static void ReleaseAuraState(AuraState& st) {
    for (int i = 0; i < st.nFrames; ++i) {
        COMRelease(st.pFronts[i]);
    }
}

static int TotalAuraCount() {
    return static_cast<int>(g_auras.size() + g_pending.size());
}

static int LowestTrackedGrade() {
    int grade = INT_MAX;
    for (const auto& entry : g_auras) {
        if (entry.second.nIndex < grade) {
            grade = entry.second.nIndex;
        }
    }
    for (const auto& pending : g_pending) {
        if (pending.nIndex < grade) {
            grade = pending.nIndex;
        }
    }
    return grade == INT_MAX ? -1 : grade;
}

static void CullLowestGradeActive() {
    void* victimDrop = nullptr;
    int victimGrade = INT_MAX;
    for (const auto& entry : g_auras) {
        if (entry.second.nIndex < victimGrade) {
            victimGrade = entry.second.nIndex;
            victimDrop = entry.first;
        }
    }
    if (!victimDrop) {
        return;
    }
    const auto it = g_auras.find(victimDrop);
    if (it != g_auras.end()) {
        ReleaseAuraState(it->second);
        g_auras.erase(it);
    }
}

static void CullLowestGradePending() {
    size_t victimIdx = g_pending.size();
    int victimGrade = INT_MAX;
    for (size_t i = 0; i < g_pending.size(); ++i) {
        if (g_pending[i].nIndex < victimGrade) {
            victimGrade = g_pending[i].nIndex;
            victimIdx = i;
        }
    }
    if (victimIdx >= g_pending.size()) {
        return;
    }
    ReleaseAuraState(g_pending[victimIdx].building);
    g_pending.erase(g_pending.begin() + static_cast<ptrdiff_t>(victimIdx));
}

static void EnforceCapacity(int incomingGrade) {
    while (static_cast<int>(g_auras.size()) >= kMaxConcurrentAuras) {
        CullLowestGradeActive();
        if (static_cast<int>(g_auras.size()) >= kMaxConcurrentAuras) {
            break;
        }
    }
    while (g_pending.size() >= static_cast<size_t>(kMaxPendingAuras)) {
        const int worst = LowestTrackedGrade();
        if (worst >= 0 && incomingGrade > worst) {
            CullLowestGradePending();
        } else {
            break;
        }
    }
}

static bool HasAuraForObjectId(int objectId) {
    if (objectId == 0) {
        return false;
    }
    for (const auto& pending : g_pending) {
        if (pending.objectId == objectId) {
            return true;
        }
    }
    for (const auto& entry : g_auras) {
        if (entry.second.objectId == objectId) {
            return true;
        }
    }
    return false;
}

static void QueueAuraSpawn(void* pDrop, int objectId, int nIndex, int dropX, int dropY) {
    if (!pDrop || objectId == 0 || nIndex < 0 || nIndex > 3) {
        return;
    }
    if (g_auras.find(pDrop) != g_auras.end() || HasAuraForObjectId(objectId)) {
        return;
    }

    const int lowest = LowestTrackedGrade();
    if (TotalAuraCount() >= kMaxConcurrentAuras
            || g_pending.size() >= static_cast<size_t>(kMaxPendingAuras)) {
        if (lowest < 0 || nIndex < lowest) {
            return;
        }
        EnforceCapacity(nIndex);
        if (nIndex == lowest) {
            CullLowestGradeActive();
            if (g_pending.size() >= static_cast<size_t>(kMaxPendingAuras)) {
                CullLowestGradePending();
            }
        }
        if (TotalAuraCount() >= kMaxConcurrentAuras
                && g_pending.size() >= static_cast<size_t>(kMaxPendingAuras)) {
            return;
        }
    }

    PendingAura pending{};
    pending.pDrop = pDrop;
    pending.objectId = objectId;
    pending.nIndex = nIndex;
    pending.dropX = dropX;
    pending.dropY = dropY;
    pending.readyTick = GetTickCount() + kSpriteRetryMs;
    pending.queuedTick = GetTickCount();
    g_pending.push_back(pending);
}

static const unsigned char* PacketBuffer(void* pPacket) {
    if (!pPacket) {
        return nullptr;
    }
    __try {
        return *reinterpret_cast<const unsigned char* const*>(reinterpret_cast<char*>(pPacket) + 0x08);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static int ReadPacketObjectId(const unsigned char* buf) {
    if (!buf) {
        return 0;
    }
    __try {
        return *reinterpret_cast<const int*>(buf + 7);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int ReadPacketMod(const unsigned char* buf) {
    if (!buf) {
        return -1;
    }
    __try {
        return static_cast<int>(buf[6]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

static bool IsSpawnDropMod(int mod) {
    return mod == 0 || mod == 1;
}

static void ReadDropPosFromPacket(void* pPacket, int& dropX, int& dropY) {
    dropX = 0;
    dropY = 0;
    const unsigned char* buf = PacketBuffer(pPacket);
    if (!buf) {
        return;
    }
    __try {
        dropX = static_cast<int>(*reinterpret_cast<const short*>(buf + 21));
        dropY = static_cast<int>(*reinterpret_cast<const short*>(buf + 23));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dropX = 0;
        dropY = 0;
    }
}

static void RemoveAllForDrop(void* pDrop) {
    if (!pDrop) {
        return;
    }
    for (auto it = g_pending.begin(); it != g_pending.end();) {
        if (it->pDrop == pDrop) {
            ReleaseAuraState(it->building);
            it = g_pending.erase(it);
        } else {
            ++it;
        }
    }
    const auto it = g_auras.find(pDrop);
    if (it != g_auras.end()) {
        ReleaseAuraState(it->second);
        g_auras.erase(it);
    }
}

static void ClearAllAuraState() {
    for (auto& entry : g_auras) {
        ReleaseAuraState(entry.second);
    }
    g_auras.clear();
    for (auto& pending : g_pending) {
        ReleaseAuraState(pending.building);
    }
    g_pending.clear();
    g_layerLoadsThisTick = 0;
    g_trackedField = nullptr;
}

static void RemoveSpawnedForObjectId(int objectId) {
    if (objectId == 0) {
        return;
    }
    for (auto it = g_auras.begin(); it != g_auras.end();) {
        if (it->second.objectId == objectId) {
            ReleaseAuraState(it->second);
            it = g_auras.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = g_pending.begin(); it != g_pending.end();) {
        if (it->objectId == objectId) {
            ReleaseAuraState(it->building);
            it = g_pending.erase(it);
        } else {
            ++it;
        }
    }
}

static bool LoadOneAuraFrame(PendingAura& pending) {
    if (g_layerLoadsThisTick >= kMaxLayerLoadsPerTick) {
        return false;
    }

    const int i = pending.nextFrame;
    if (i >= kMaxFrames) {
        return true;
    }

    if (pending.building.nFrames == 0) {
        pending.building.pDrop = pending.pDrop;
        pending.building.objectId = pending.objectId;
        pending.building.nIndex = pending.nIndex;
        pending.building.dropX = pending.dropX;
        pending.building.dropY = pending.dropY;
        pending.building.frameIdx = 0;
        pending.building.lastFrameTick = GetTickCount();
        pending.building.expireTick = GetTickCount() + 60000u;
    }

    wchar_t path[256];
    swprintf_s(path, 256, L"Effect/BasicEff.img/dropItemAura/%s/front/%d",
            g_gradeNames[pending.nIndex], i);

    void* pF = nullptr;
    if (!SafeLoadLayer(&pF, path, pending.dropX, pending.dropY)) {
        return pending.building.nFrames >= kMinFrames;
    }

    pending.building.pFronts[i] = pF;
    if (i > 0) {
        LayerSetVisible(pF, false);
    }
    pending.building.nFrames = i + 1;
    pending.nextFrame = i + 1;
    return false;
}

static bool AdvancePendingAura(PendingAura& pending) {
    if (!pending.pDrop || !IsReadablePtr(pending.pDrop, 0x40)) {
        return false;
    }

    while (pending.nextFrame < kMaxFrames) {
        if (g_layerLoadsThisTick >= kMaxLayerLoadsPerTick) {
            break;
        }
        const int before = pending.nextFrame;
        const bool done = LoadOneAuraFrame(pending);
        if (done) {
            break;
        }
        if (pending.nextFrame == before) {
            break;
        }
    }

    if (pending.building.nFrames < kMinFrames) {
        return false;
    }

    EnforceCapacity(pending.nIndex);
    const auto existIt = g_auras.find(pending.pDrop);
    if (existIt != g_auras.end()) {
        ReleaseAuraState(existIt->second);
        g_auras.erase(existIt);
    }
    pending.building.frameIdx = 0;
    pending.building.lastFrameTick = GetTickCount();
    for (int i = 0; i < pending.building.nFrames; ++i) {
        LayerSetVisible(pending.building.pFronts[i], i == 0);
    }
    g_auras[pending.pDrop] = pending.building;
    pending.building = AuraState{};
    return true;
}

static BOOL CALLBACK FindGameWindow(HWND hWnd, LPARAM lParam) {
    unsigned long pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid == GetCurrentProcessId()) {
        *reinterpret_cast<HWND*>(lParam) = hWnd;
        return FALSE;
    }
    return TRUE;
}

static unsigned long g_timerId = 0;
static HWND g_hGame = nullptr;

static void EnsureTimer();
static void CALLBACK AuraTimerProc(HWND, UINT, UINT_PTR, DWORD dwTime);

static void TrackFieldChange() {
    CField* field = get_field();
    void* const fieldPtr = field ? static_cast<void*>(field) : nullptr;
    if (g_trackedField != fieldPtr) {
        if (g_trackedField != nullptr || !g_auras.empty() || !g_pending.empty()) {
            ClearAllAuraState();
        }
        g_trackedField = fieldPtr;
    }
}

static void EnsureTimer() {
    if (g_timerId != 0) {
        return;
    }
    if (g_hGame == nullptr) {
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&g_hGame));
    }
    if (g_hGame != nullptr) {
        g_timerId = SetTimer(g_hGame, 0xD1A1u, kAuraFrameMs, AuraTimerProc);
    }
}

static void CALLBACK AuraTimerProc(HWND, UINT, UINT_PTR, DWORD dwTime) {
    g_layerLoadsThisTick = 0;
    TrackFieldChange();

    int advanced = 0;
    for (auto it = g_pending.begin(); it != g_pending.end() && advanced < kMaxPendingAdvancePerTick;) {
        if (dwTime < it->readyTick) {
            ++it;
            continue;
        }
        if (it->queuedTick != 0 && dwTime - it->queuedTick >= kPendingGiveUpMs) {
            ReleaseAuraState(it->building);
            it = g_pending.erase(it);
            continue;
        }
        if (AdvancePendingAura(*it)) {
            it = g_pending.erase(it);
        } else {
            ++it;
        }
        ++advanced;
    }

    for (auto it = g_auras.begin(); it != g_auras.end();) {
        AuraState& st = it->second;
        if (!st.pDrop || !IsReadablePtr(st.pDrop, 0x40)) {
            ReleaseAuraState(st);
            it = g_auras.erase(it);
            continue;
        }
        if (dwTime >= st.expireTick) {
            ReleaseAuraState(st);
            it = g_auras.erase(it);
            continue;
        }
        if (st.nFrames > 1 && dwTime - st.lastFrameTick >= kAuraFrameMs) {
            const int next = (st.frameIdx + 1) % st.nFrames;
            LayerSetVisible(st.pFronts[st.frameIdx], false);
            LayerSetVisible(st.pFronts[next], true);
            st.frameIdx = next;
            st.lastFrameTick = dwTime;
        }
        ++it;
    }
}

static unsigned long s_onDropEnterFieldRet = static_cast<unsigned long>(kOnDropEnterFieldRet);

static unsigned char __stdcall OnDropEnterFieldHelper(void* pPacket, void* pDrop) {
    const unsigned char nItemGrade = pPacket ? Decode1(pPacket) : 0;
    const unsigned char petPickupFlag = pPacket ? Decode1(pPacket) : 0;

    if (!pPacket || !pDrop || !IsReadablePtr(pDrop, 0x40)) {
        return petPickupFlag;
    }

    const int nIndex = (nItemGrade >= 1 && nItemGrade <= 4) ? static_cast<int>(nItemGrade - 1) : -1;
    if (nIndex < 0) {
        return petPickupFlag;
    }

    const unsigned char* buf = PacketBuffer(pPacket);
    const int mod = ReadPacketMod(buf);
    if (!IsSpawnDropMod(mod)) {
        return petPickupFlag;
    }

    int dropX = 0;
    int dropY = 0;
    const int objectId = ReadPacketObjectId(buf);
    ReadDropPosFromPacket(pPacket, dropX, dropY);

    EnsureTimer();
    QueueAuraSpawn(pDrop, objectId, nIndex, dropX, dropY);
    return petPickupFlag;
}

static void __declspec(naked) OnDropEnterFieldHook() {
    __asm {
        push esi
        push ecx
        call OnDropEnterFieldHelper
        jmp dword ptr [s_onDropEnterFieldRet]
    }
}

static BYTE* s_dtorTramp = nullptr;

static void __fastcall DropDtorHook(void* pEntry) {
    RemoveAllForDrop(pEntry);
    RemoveAllForDrop(static_cast<char*>(pEntry) + 0x10);
    reinterpret_cast<void(__thiscall*)(void*)>(s_dtorTramp)(pEntry);
}

static bool HandleDropMapPacketSideEffect(void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
    const unsigned char* buf = packet ? packet->Data() : nullptr;
    if (!buf || packet->Size() < 11) {
        return false;
    }

    if (opcode == 0x10C) {
        const int mod = static_cast<int>(buf[6]);
        if (mod == 3) {
            RemoveSpawnedForObjectId(ReadPacketObjectId(buf));
        }
    } else if (opcode == 0x10D) {
        RemoveSpawnedForObjectId(ReadPacketObjectId(buf));
    }
    return false;
}

static void RegisterPacketSideEffects() {
    if (g_packetHandlerRegistered) {
        return;
    }
    g_packetHandlerRegistered = true;
    PacketDispatcher::RegisterLegacyHandler(0x10C, HandleDropMapPacketSideEffect);
    PacketDispatcher::RegisterLegacyHandler(0x10D, HandleDropMapPacketSideEffect);
}

static void InstallCreateLayerHook() {
    if (s_createLayerTramp) {
        return;
    }
    s_createLayerTramp = reinterpret_cast<BYTE*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!s_createLayerTramp) {
        return;
    }
    memcpy(s_createLayerTramp, reinterpret_cast<void*>(kCreateLayer), 5);
    s_createLayerTramp[5] = 0xE9;
    const unsigned long rel = static_cast<unsigned long>(
            (kCreateLayer + 5) - (reinterpret_cast<uintptr_t>(s_createLayerTramp + 5) + 5));
    *reinterpret_cast<unsigned long*>(s_createLayerTramp + 6) = rel;

    union {
        int** (CreateLayerHook::*pmf)(int**, int, int, int, int, int, unsigned long*, int*);
        uintptr_t addr;
    } entry{};
    entry.pmf = &CreateLayerHook::hook;
    PatchJmp(kCreateLayer, reinterpret_cast<void*>(entry.addr));
}

static void InstallDropDtorHook() {
    s_dtorTramp = reinterpret_cast<BYTE*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!s_dtorTramp) {
        return;
    }
    memcpy(s_dtorTramp, reinterpret_cast<void*>(kDropDtor), 5);
    s_dtorTramp[5] = 0xE9;
    const unsigned long rel = static_cast<unsigned long>((kDropDtor + 5) - (reinterpret_cast<uintptr_t>(s_dtorTramp + 5) + 5));
    *reinterpret_cast<unsigned long*>(s_dtorTramp + 6) = rel;
    PatchJmp(kDropDtor, &DropDtorHook);
}

} // namespace

namespace DropItemAura {
void AttachHooks() {
    if (g_hooksAttached) {
        return;
    }
    g_hooksAttached = true;

    RegisterPacketSideEffects();
    PatchJmp(kOnDropEnterFieldJmp, &OnDropEnterFieldHook);
    InstallCreateLayerHook();
    InstallDropDtorHook();
}
} // namespace DropItemAura
