// hyper_book_loader.cpp — slim hyper Skill/*.img on live for K-open; promote _full on cast only.
//
// Hook points (no new VA patches):
//   CUserLocal::Update @0x0094A144 — poll GW_CharacterStat.nJob via ZtlSecureFuse_short @0x004746DD
//     (chained in LazyCompatInit.cpp next to ModRegistry::OnClientTick).
//   CUISkill::OnCreate @0x008AAA88 — ensure slim books before K-open WZ scan (post-cast promote).
//   rs_resman_flush_cached(0) after disk swap so ResMan reloads Skill/{job}.img.
//
// Paths (relative to BeiDou.exe directory):
//   Data/Skill/{job}.img       live
//   Data/Skill/_full/{job}.img full VFX archive (~507MB total)
//   Data/Skill/_slim/{job}.img optional revert stubs (~9KB)

#include "stdafx.h"
#include "HyperSkillApi.h"
#include "Client.h"
#include "Memory.h"
#include "compat/rs/rs.h"

#include <cstdio>
#include <string>

namespace {

constexpr uintptr_t kCWvsContextSingleton = 0x00BE7918;
constexpr uintptr_t kOff_CharDataInCtx = 0x20B8;
// GW_CharacterStat @ CharacterData+0 (Meso Uncap / level300 IDA cross-check).
constexpr uintptr_t kOff_JobTear = 0x39;
constexpr uintptr_t kOff_JobCs = 0x3D;
constexpr uintptr_t kZtlSecureFuseShort = 0x004746DD;

constexpr long long kFullBookThreshold = 100 * 1024; // >100KB treated as full on disk

using FuseShort_t = int(__cdecl*)(void* pTear, unsigned int checksum);

bool g_enabled = true;
bool g_revertSlim = true;
int g_lastSeenJob = -1;
int g_lastPromotedHyperBook = -1;
std::wstring g_skillDir;
bool g_loggedSkillDir = false;

static int g_promotedThisSession = -1; // keep full after cast; skip K-open slim thrash

static void HyperBookLog(const char* msg) {
    std::cout << msg << std::endl;
    FILE* f = nullptr;
    if (fopen_s(&f, "beidou-hyperskill.log", "a") == 0 && f) {
        fputs(msg, f);
        fputc('\n', f);
        fclose(f);
    }
}

static FuseShort_t FuseShort() {
    return reinterpret_cast<FuseShort_t>(kZtlSecureFuseShort);
}

static void* GetLocalCharacterData() {
    void* ctx = *reinterpret_cast<void**>(kCWvsContextSingleton);
    if (!ctx) {
        return nullptr;
    }
    return *reinterpret_cast<void**>(reinterpret_cast<char*>(ctx) + kOff_CharDataInCtx);
}

static int ReadLocalJobId() {
    void* pChar = GetLocalCharacterData();
    if (!pChar) {
        return -1;
    }
    __try {
        char* stat = static_cast<char*>(pChar);
        const unsigned int cs =
            *reinterpret_cast<unsigned int*>(stat + kOff_JobCs);
        return static_cast<int>(static_cast<short>(FuseShort()(stat + kOff_JobTear, cs)));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

static bool IsHyperBookJobId(int bookId) {
    return bookId % 10 == 3 || bookId == 700 || bookId == 710;
}

static int ResolveHyperBookForJob(int job) {
    if (job <= 0) {
        return 0;
    }
    if (IsHyperBookJobId(job)) {
        return job;
    }
    if (job >= 100) {
        const int candidate = (job / 10) * 10 + 3;
        if (IsHyperBookJobId(candidate)) {
            return candidate;
        }
    }
    return 0;
}

static bool EnsureSkillDir() {
    if (!g_skillDir.empty()) {
        return true;
    }
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        return false;
    }
    wchar_t* slash = wcsrchr(exePath, L'\\');
    if (slash) {
        *(slash + 1) = L'\0';
    }
    g_skillDir = exePath;
    g_skillDir += L"Data\\Skill\\";
    if (!g_loggedSkillDir) {
        char buf[512];
        sprintf_s(buf, "[hyperskill] autoLoad skillDir=%ls", g_skillDir.c_str());
        HyperBookLog(buf);
        g_loggedSkillDir = true;
    }
    return true;
}

static std::wstring BookPath(int bookId, const wchar_t* pool) {
    wchar_t name[32];
    swprintf_s(name, L"%d.img", bookId);
    std::wstring path = g_skillDir;
    if (pool && pool[0]) {
        path += pool;
        path += L"\\";
    }
    path += name;
    return path;
}

static bool QueryFileSize(const std::wstring& path, long long* outSize) {
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
        return false;
    }
    ULARGE_INTEGER size;
    size.LowPart = info.nFileSizeLow;
    size.HighPart = info.nFileSizeHigh;
    *outSize = static_cast<long long>(size.QuadPart);
    return true;
}

static bool CopyBookFromPool(int bookId, const wchar_t* pool, const char* label) {
    if (!EnsureSkillDir()) {
        return false;
    }
    const std::wstring src = BookPath(bookId, pool);
    const std::wstring dst = BookPath(bookId, nullptr);

    long long srcSize = 0;
    if (!QueryFileSize(src, &srcSize)) {
        char buf[160];
        sprintf_s(buf, "[hyperskill] autoLoad skip %s %d (no pool %ls)", label, bookId, src.c_str());
        HyperBookLog(buf);
        return false;
    }

    long long dstSize = -1;
    if (QueryFileSize(dst, &dstSize) && dstSize == srcSize) {
        return false; // already matches
    }

    if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
        char buf[192];
        sprintf_s(buf, "[hyperskill] autoLoad COPY FAIL %s %d err=%lu", label, bookId, GetLastError());
        HyperBookLog(buf);
        return false;
    }

    char buf[192];
    sprintf_s(buf, "[hyperskill] autoLoad %s %d OK (%lld -> %lld bytes)",
              label, bookId, dstSize, srcSize);
    HyperBookLog(buf);
    return true;
}

static bool PromoteFullBook(int bookId) {
    const bool changed = CopyBookFromPool(bookId, L"_full", "promote");
    // Stay full for this session — K-open must NOT slim back (24MB copy freeze).
    g_promotedThisSession = bookId;
    return changed;
}

static bool RevertSlimBook(int bookId) {
    if (!g_revertSlim || bookId == 700 || bookId == 710) {
        return false;
    }
    if (bookId == g_promotedThisSession) {
        return false;
    }
    long long liveSize = 0;
    const std::wstring live = BookPath(bookId, nullptr);
    if (QueryFileSize(live, &liveSize) && liveSize <= kFullBookThreshold) {
        return false; // already slim
    }
    return CopyBookFromPool(bookId, L"_slim", "revert");
}

static bool EnsureSlimOnLive(int bookId) {
    if (bookId <= 0) {
        return false;
    }
    // After cast-promote, keep full book until job change / logout.
    if (bookId == g_promotedThisSession) {
        return false;
    }
    long long liveSize = 0;
    const std::wstring live = BookPath(bookId, nullptr);
    if (QueryFileSize(live, &liveSize) && liveSize <= kFullBookThreshold) {
        return false; // already slim
    }
    return CopyBookFromPool(bookId, L"_slim", "ensureSlim");
}

static bool EnsureSlimBooksForJob(int job, const char* reason) {
    if (!g_enabled || !EnsureSkillDir() || job <= 0) {
        return false;
    }

    const int hyperBook = ResolveHyperBookForJob(job);
    bool changed = false;

    // K-open loads every job tab book — keep live slim (~9KB). Full VFX only on cast promote.
    if (hyperBook > 0) {
        changed |= EnsureSlimOnLive(hyperBook);
        g_lastPromotedHyperBook = hyperBook;
    } else {
        g_lastPromotedHyperBook = 0;
    }

    changed |= EnsureSlimOnLive(700);
    changed |= EnsureSlimOnLive(710);

    if (changed) {
        rs_resman_flush_cached(0);
        char buf[160];
        sprintf_s(buf, "[hyperskill] autoLoad slim job=%d hyperBook=%d reason=%s flush=1",
                  job, hyperBook, reason ? reason : "?");
        HyperBookLog(buf);
    }
    return changed;
}

static void SyncHyperBooksForJob(int job) {
    // Job change: allow slim again for the previous promoted book.
    g_promotedThisSession = -1;
    EnsureSlimBooksForJob(job, "jobChange");
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

static void EnsureSlimForSkillUiHook() {
    if (!g_enabled || !Client::enableHyperSkill || !Client::autoLoadHyperSkillBooks) {
        return;
    }
    const int job = ReadLocalJobId();
    EnsureSlimBooksForJob(job, "kOpen");
}

constexpr DWORD kCUISkillOnCreatePrologue = 0x008AAA88;
constexpr DWORD kCUISkillOnCreateEhProlog = 0x008AAA8D;
constexpr DWORD kAce42aEhHandler = 0x00ACE42A;

__declspec(naked) void CUISkill_OnCreate_SlimHook() {
    __asm {
        pushad
    }
    EnsureSlimForSkillUiHook();
    __asm {
        popad
        mov eax, kAce42aEhHandler
        jmp kCUISkillOnCreateEhProlog
    }
}

static void InstallCUISkillSlimHook() {
    static const unsigned char kOnCreatePrologue[] = {0xB8, 0x2A, 0xE4, 0xAC, 0x00};
    if (ExpectBytes(kCUISkillOnCreatePrologue, kOnCreatePrologue, sizeof(kOnCreatePrologue))) {
        Memory::CodeCave(reinterpret_cast<void*>(&CUISkill_OnCreate_SlimHook),
                         kCUISkillOnCreatePrologue, 0);
        const unsigned char op = *reinterpret_cast<volatile unsigned char*>(kCUISkillOnCreatePrologue);
        char buf[128];
        sprintf_s(buf, "[hyperskill] CUISkill OnCreate slim-hook @ %08X => %02X %s",
                  kCUISkillOnCreatePrologue, op, op == 0xE9 ? "OK" : "FAIL");
        HyperBookLog(buf);
    } else {
        HyperBookLog("[hyperskill] CUISkill OnCreate slim-hook SKIP @ 8AAA88 (stock bytes mismatch)");
    }
}

} // namespace

extern "C" void __cdecl HyperBookLoader_PromoteForSkill(int skillId) {
    if (!g_enabled || !Client::enableHyperSkill || !Client::autoLoadHyperSkillBooks || skillId <= 0) {
        return;
    }
    if (!EnsureSkillDir()) {
        return;
    }
    const int bookId = skillId / 10000;
    if (bookId <= 0) {
        return;
    }
    if (!IsHyperBookJobId(bookId) && bookId != 700 && bookId != 710) {
        return;
    }
    if (PromoteFullBook(bookId)) {
        rs_resman_flush_cached(0);
        char buf[128];
        sprintf_s(buf, "[hyperskill] autoLoad castPromote skill=%d book=%d flush=1", skillId, bookId);
        HyperBookLog(buf);
    }
}

void HyperBookLoader_SetEnabled(bool enabled) {
    g_enabled = enabled;
}

void HyperBookLoader_SetRevertSlim(bool revert) {
    g_revertSlim = revert;
}

void HyperBookLoader_OnTick() {
    if (!g_enabled || !Client::enableHyperSkill || !Client::autoLoadHyperSkillBooks) {
        return;
    }

    const int job = ReadLocalJobId();
    if (job <= 0) {
        if (g_lastSeenJob > 0) {
            g_lastSeenJob = -1;
        }
        return;
    }
    if (job == g_lastSeenJob) {
        return;
    }

    g_lastSeenJob = job;
    SyncHyperBooksForJob(job);
}

void AttachHyperBookLoader() {
    g_enabled = Client::autoLoadHyperSkillBooks;
    g_revertSlim = Client::autoLoadHyperRevertSlim;
    g_lastSeenJob = -1;
    g_lastPromotedHyperBook = -1;
    g_promotedThisSession = -1;
    g_skillDir.clear();
    g_loggedSkillDir = false;

    char buf[160];
    sprintf_s(buf, "[hyperskill] AttachHyperBookLoader enabled=%d revertSlim=%d (tick@CUserLocal 94A144)",
              g_enabled ? 1 : 0, g_revertSlim ? 1 : 0);
    HyperBookLog(buf);
    if (g_enabled) {
        InstallCUISkillSlimHook();
    }
}
