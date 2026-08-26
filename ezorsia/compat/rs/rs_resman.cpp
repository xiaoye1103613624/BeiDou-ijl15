// rs_resman.cpp - Custom.wz resource manager (SysOpt UI customization)
// Fail-soft: any mount/hook failure must NOT abort client boot (no E_FAIL).
#include "stdafx.h"
#include "../hook.h"
#include "../wvs/wvsapp.h"
#include "../wvs/util.h"
#include "../ztl/ztl.h"
#include "../../bootlog/CrashDiag.h"
#include "../../Client.h"
#include <windows.h>
#include <psapi.h>
#include <vector>
#include <algorithm>
#include <tuple>
#include <iostream>
#include <comdef.h>

static IWzNameSpacePtr g_customNS;
static std::vector<Ztl_bstr_t> g_overrides;
static bool g_hooksAttached = false;
static bool g_mountOk = false;

void rs_resman_init(); // forward: used as module-address anchor for path probe

// Pattern scan helper
static void* PatFind(const char* dll, const char* pat) {
    HMODULE h = GetModuleHandleA(dll);
    if (!h) { h = LoadLibraryA(dll); if (!h) return nullptr; }
    MODULEINFO mi;
    if (!GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi))) return nullptr;
    auto* base = (unsigned char*)mi.lpBaseOfDll;
    size_t sz = mi.SizeOfImage;
    unsigned char buf[256], mask[256]; size_t n = 0;
    while (*pat) {
        if (*pat == ' ') { pat++; continue; }
        if (pat[0] == '?' && pat[1] == '?') { mask[n] = 0; buf[n] = 0; }
        else {
            auto hx = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            int hi = hx(pat[0]), lo = hx(pat[1]);
            if (hi < 0 || lo < 0) return nullptr;
            buf[n] = (unsigned char)((hi << 4) | lo); mask[n] = 0xFF;
        }
        pat += 2; n++;
    }
    if (n == 0 || sz < n) return nullptr;
    for (size_t i = 0; i <= sz - n; i++) {
        size_t j;
        for (j = 0; j < n; j++) if ((base[i + j] & mask[j]) != (buf[j] & mask[j])) break;
        if (j == n) return &base[i];
    }
    return nullptr;
}

// IWzNameSpaceImpl hook: intercept resource lookups
typedef HRESULT(__stdcall* OL_fn)(void*, int, BSTR, int*, VARIANT*);
static OL_fn g_ol_orig = nullptr;

HRESULT __stdcall rs_ns_hook(void* pThis, int idx, BSTR path, int* used, VARIANT* ret) {
    CrashDiag_NoteWzPath(path);
    HRESULT hr = g_ol_orig(pThis, idx, path, used, ret);
    if (SUCCEEDED(hr)) return hr;
    if (!g_mountOk || !g_customNS) return hr;
    try {
        Ztl_bstr_t sp(path);
        if (!std::binary_search(g_overrides.begin(), g_overrides.end(), sp)) return hr;
        return g_customNS->raw__OnGetLocalObject(idx, path, used, ret);
    } catch (...) {
        return hr;
    }
}

// CWzProperty hook: merge Custom properties during serialization
typedef HRESULT(__stdcall* Ser_fn)(void*, IWzArchive*);
static Ser_fn g_ser_orig = nullptr;

HRESULT __stdcall rs_ser_hook(void* pThis, IWzArchive* arch) {
    HRESULT hr = g_ser_orig(pThis, arch);
    if (FAILED(hr) || !g_mountOk || !arch) return hr;
    try {
        if (!std::binary_search(g_overrides.begin(), g_overrides.end(), arch->absoluteUOL)) return hr;
        IWzPropertyPtr prop = get_rm()->GetObjectA(L"Custom/" + arch->absoluteUOL).GetUnknown();
        if (!prop) return S_OK;
        IEnumVARIANTPtr en = prop->_NewEnum;
        if (!en) return S_OK;
        IWzProperty* self = reinterpret_cast<IWzProperty*>(pThis);
        while (true) {
            Ztl_variant_t v; ULONG c;
            if (FAILED(en->Next(1, &v, &c)) || c == 0) break;
            Ztl_bstr_t s = V_BSTR(&v);
            IUnknownPtr u = prop->item[s].GetUnknown();
            IWzPropertyPtr sub;
            // Leaf / non-nested replacements (canvas, int, ...). Nested props recurse via serialize.
            if (!u || FAILED(u->QueryInterface(&sub))) {
                // Force replace: stock SysOpt already has backgrnd; bNoReplace=false alone
                // is not enough on some PCOM builds when the name exists.
                try { self->Remove(s); } catch (...) {}
                self->Add(s, prop->item[s], false);
            }
        }
    } catch (...) {
        // never break serialize path
    }
    return S_OK;
}

static void rs_resman_reset() {
    g_overrides.clear();
    g_customNS = nullptr;
    g_mountOk = false;
    // Do not detach hooks once attached - Detours detach mid-run is unsafe.
    // Hooks no-op when g_mountOk is false.
}

// Strip filename only (do NOT Dir_upDir - trailing slash / odd GetModuleFileName
// shapes would climb one level too high, e.g. out of BeiDou-Client_1).
static bool rs_dir_from_module_path(wchar_t* path) {
    if (!path || !path[0]) return false;
    wchar_t* slash = wcsrchr(path, L'\\');
    wchar_t* slash2 = wcsrchr(path, L'/');
    if (slash2 && (!slash || slash2 > slash)) slash = slash2;
    if (!slash) return false;
    *slash = L'\0';
    return path[0] != L'\0';
}

static bool rs_file_exists_w(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// Resolve directory that contains Custom.wz.
// Prefer ijl15.dll dir (same place as config.ini), then exe dir, then CWD.
// Never use CWvsApp::Dir_upDir here - it can overshoot parent of client root.
static bool rs_resolve_custom_dir(char* outDir, size_t outDirCch, char* outWzWin, size_t outWzCch) {
    wchar_t candidates[3][MAX_PATH] = {};
    int n = 0;

    // 1) ijl15.dll directory (most reliable for BeiDou plugin layout)
    HMODULE hDll = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&rs_resman_init), &hDll) && hDll) {
        if (GetModuleFileNameW(hDll, candidates[n], MAX_PATH) && rs_dir_from_module_path(candidates[n])) {
            ++n;
        }
    }
    // 2) exe directory
    if (GetModuleFileNameW(nullptr, candidates[n], MAX_PATH) && rs_dir_from_module_path(candidates[n])) {
        // skip duplicate
        if (n == 0 || _wcsicmp(candidates[0], candidates[n]) != 0) ++n;
        else candidates[n][0] = L'\0';
    }
    // 3) current working directory
    if (GetCurrentDirectoryW(MAX_PATH, candidates[n])) {
        bool dup = false;
        for (int i = 0; i < n; ++i) {
            if (_wcsicmp(candidates[i], candidates[n]) == 0) { dup = true; break; }
        }
        if (!dup) ++n;
        else candidates[n][0] = L'\0';
    }

    for (int i = 0; i < n; ++i) {
        if (!candidates[i][0]) continue;
        wchar_t wz[MAX_PATH] = {};
        _snwprintf_s(wz, _TRUNCATE, L"%s\\Custom.wz", candidates[i]);
        std::wcout << L"[RS] Custom.wz probe: " << wz << std::endl;
        if (!rs_file_exists_w(wz)) continue;

        // Narrow dir for IWzFileSystem::Init (ACP / CP_ACP on CN Windows)
        char dirA[MAX_PATH] = {};
        char wzA[MAX_PATH] = {};
        if (!WideCharToMultiByte(CP_ACP, 0, candidates[i], -1, dirA, MAX_PATH, nullptr, nullptr)) {
            std::cout << "[RS] Custom.wz: WideCharToMultiByte(dir) failed" << std::endl;
            continue;
        }
        if (!WideCharToMultiByte(CP_ACP, 0, wz, -1, wzA, MAX_PATH, nullptr, nullptr)) {
            std::cout << "[RS] Custom.wz: WideCharToMultiByte(wz) failed" << std::endl;
            continue;
        }
        // IWzFileSystem expects slash style like stock client
        for (char* p = dirA; *p; ++p) if (*p == '\\') *p = '/';

        if (outDir && outDirCch) strncpy_s(outDir, outDirCch, dirA, _TRUNCATE);
        if (outWzWin && outWzCch) strncpy_s(outWzWin, outWzCch, wzA, _TRUNCATE);
        std::cout << "[RS] Custom.wz found under: " << dirA << std::endl;
        return true;
    }

    std::cout << "[RS] Custom.wz not found (dll/exe/cwd) - SysOpt UI stretch skipped" << std::endl;
    return false;
}

static bool rs_try_mount_custom() {
    char ep[MAX_PATH] = {};
    char wzWin[MAX_PATH] = {};
    if (!rs_resolve_custom_dir(ep, MAX_PATH, wzWin, MAX_PATH)) {
        return false;
    }

    IWzWritableNameSpacePtr wroot;
    if (FAILED(get_root()->QueryInterface(&wroot)) || !wroot) {
        std::cout << "[RS] Custom.wz: root QI failed" << std::endl;
        return false;
    }

    IWzNameSpacePtr ns;
    PcCreateObject<IWzNameSpacePtr>(L"NameSpace", ns, nullptr);
    if (!ns) {
        std::cout << "[RS] Custom.wz: PcCreateObject NameSpace failed" << std::endl;
        return false;
    }

    Ztl_variant_t vr;
    wroot->AddObject(L"Custom", static_cast<IUnknown*>(ns), &vr);
    g_customNS = vr.GetUnknown();
    if (!g_customNS) {
        std::cout << "[RS] Custom.wz: AddObject Custom failed" << std::endl;
        return false;
    }

    IWzFileSystemPtr fs;
    PcCreateObject<IWzFileSystemPtr>(L"NameSpace#FileSystem", fs, nullptr);
    if (!fs) {
        std::cout << "[RS] Custom.wz: PcCreateObject FileSystem failed" << std::endl;
        rs_resman_reset();
        return false;
    }

    HRESULT hrInit = fs->Init(ep);
    if (FAILED(hrInit)) {
        std::cout << "[RS] Custom.wz: FileSystem Init failed hr=0x"
                  << std::hex << hrInit << std::dec << " path=" << ep << std::endl;
        rs_resman_reset();
        return false;
    }

    IWzSeekableArchivePtr arch;
    try {
        arch = fs->item[L"Custom.wz"].GetUnknown();
    } catch (const _com_error& e) {
        std::cout << "[RS] Custom.wz open failed com 0x" << std::hex << e.Error() << std::dec << std::endl;
        arch = nullptr;
    } catch (...) {
        arch = nullptr;
    }
    if (!arch) {
        std::cout << "[RS] Custom.wz open failed (item) path=" << ep << "/Custom.wz" << std::endl;
        rs_resman_reset();
        return false;
    }

    IWzPackagePtr pkg;
    PcCreateObject<IWzPackagePtr>(L"NameSpace#Package", pkg, nullptr);
    if (!pkg) {
        std::cout << "[RS] Custom.wz: PcCreateObject Package failed" << std::endl;
        rs_resman_reset();
        return false;
    }

    HRESULT hrPkg = pkg->Init(L"83", L"Custom", arch);
    if (FAILED(hrPkg)) {
        std::cout << "[RS] Custom.wz: Package Init(key=83) failed hr=0x"
                  << std::hex << hrPkg << std::dec << std::endl;
        rs_resman_reset();
        return false;
    }

    HRESULT hrMount = g_customNS->Mount(L"/", pkg, 1);
    if (FAILED(hrMount)) {
        std::cout << "[RS] Custom.wz: Mount failed hr=0x"
                  << std::hex << hrMount << std::dec << std::endl;
        rs_resman_reset();
        return false;
    }

    std::vector<std::tuple<Ztl_bstr_t, IEnumVARIANTPtr>> stack;
    stack.emplace_back(L"", g_customNS->_NewEnum);
    while (!stack.empty()) {
        auto item = stack.back(); stack.pop_back();
        Ztl_bstr_t p = std::get<0>(item);
        IEnumVARIANTPtr en = std::get<1>(item);
        if (!en) continue;
        while (true) {
            Ztl_variant_t v; ULONG c;
            if (FAILED(en->Next(1, &v, &c)) || c == 0) break;
            Ztl_bstr_t uol = (p.length() > 0 ? (p + L"/") : Ztl_bstr_t(L"")) + V_BSTR(&v);
            Ztl_variant_t vo = get_rm()->GetObjectA(L"Custom/" + uol);
            IUnknownPtr un = vo.GetUnknown();
            if (un) {
                IWzNameSpacePtr s;
                if (SUCCEEDED(un->QueryInterface(&s))) { stack.emplace_back(uol, s->_NewEnum); continue; }
                IWzPropertyPtr pp;
                if (SUCCEEDED(un->QueryInterface(&pp))) { stack.emplace_back(uol, pp->_NewEnum); }
            }
            g_overrides.push_back(uol);
        }
    }
    std::sort(g_overrides.begin(), g_overrides.end());
    g_mountOk = !g_overrides.empty();

    if (!g_mountOk) {
        std::cout << "[RS] Custom.wz mounted but no overrides" << std::endl;
        rs_resman_reset();
        return false;
    }

    // NameSpace / PCOM hooks ONLY after successful mount (conditional).
    if (!g_hooksAttached) {
        g_ol_orig = (OL_fn)PatFind("NAMESPACE.DLL", "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 81 EC 80");
        if (g_ol_orig) {
            Memory::SetHook(true, (void**)&g_ol_orig, CastHook(&rs_ns_hook));
        } else {
            std::cout << "[RS] Custom.wz: NAMESPACE pattern miss (lookup overlay off)" << std::endl;
        }
        g_ser_orig = (Ser_fn)PatFind("PCOM.DLL", "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 68");
        if (g_ser_orig) {
            Memory::SetHook(true, (void**)&g_ser_orig, CastHook(&rs_ser_hook));
        } else {
            std::cout << "[RS] Custom.wz: PCOM pattern miss (serialize merge off)" << std::endl;
        }
        // Consider hooks attached even if one pattern misses - avoid double-attach.
        g_hooksAttached = true;
    }

    std::cout << "[RS] Custom.wz mounted overrides=" << g_overrides.size()
              << " ns_hook=" << (g_ol_orig ? 1 : 0)
              << " ser_hook=" << (g_ser_orig ? 1 : 0) << std::endl;
    {
        char path[MAX_PATH + 32]{};
        char exe[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, exe, MAX_PATH)) {
            char* slash = strrchr(exe, '\\');
            if (slash) *(slash + 1) = '\0';
            sprintf_s(path, "%srs_ui.log", exe);
        } else {
            strcpy_s(path, "rs_ui.log");
        }
        if (FILE* f = nullptr; fopen_s(&f, path, "a") == 0 && f) {
            fprintf(f, "[RS] Custom.wz mounted overrides=%zu ns=%d ser=%d\n",
                    g_overrides.size(), g_ol_orig ? 1 : 0, g_ser_orig ? 1 : 0);
            fclose(f);
        }
    }
    return true;
}

static bool rs_try_mount_custom_cxx() {
    try {
        return rs_try_mount_custom();
    } catch (const _com_error& e) {
        std::cout << "[RS] Custom.wz soft-fail com 0x" << std::hex << e.Error() << std::dec << std::endl;
        rs_resman_reset();
        return false;
    } catch (...) {
        std::cout << "[RS] Custom.wz soft-fail (cxx) - boot continues" << std::endl;
        rs_resman_reset();
        return false;
    }
}

// Pure SEH trampoline (no C++ objects / try-catch in this frame).
static int g_rs_mount_seh = 0;
static bool rs_try_mount_custom_seh() {
    bool ok = false;
    g_rs_mount_seh = 0;
    __try {
        ok = rs_try_mount_custom_cxx();
    } __except (g_rs_mount_seh = (int)GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

void CWvsApp::InitializeResMan_hook() {
    // Always run stock resman first - never skip boot path.
    CWvsApp::InitializeResMan(this);
    // Optional MapleRoot-style retain + SweepCache/CField flush.
    // Do NOT replace InitializeResMan with Ragezone self-built Data FS — Custom.wz
    // mounts below on the same VA. Fail-soft: never abort boot.
    //
    // IDA BeiDou.exe (imagebase 0x400000):
    //   stock SetResManParam @0x9F71D0: (17, -1, -1) via g_rm@0xBF14E8 vtable+20
    //   SweepCache sub_411BBB: outer cmp @0x411BE2 imm 60000; object-age cmps 300000
    //   CField::Init @0x529320: push 0x2BF20 → FlushCachedObjects(180000) via vtable+0x24
    if (Client::enableResManTimeout) {
        try {
            IWzResManPtr& rm = get_rm();
            if (rm) {
                const int retain =
                    (Client::resManRetainMs > 0) ? Client::resManRetainMs : 10000;
                const HRESULT hr = rm->SetResManParam(
                    static_cast<RESMAN_PARAM>(RC_AUTO_REPARSE | RC_AUTO_SERIALIZE),
                    retain,
                    -1);
                if (FAILED(hr)) {
                    std::cout << "[RS] SetResManParam soft-fail hr=0x" << std::hex
                              << static_cast<unsigned long>(hr) << std::dec << std::endl;
                } else {
                    std::cout << "[RS] ResMan retainMs=" << retain
                              << " (MapleRoot-style AUTO_SERIALIZE)" << std::endl;
                }
            }
        } catch (...) {
            std::cout << "[RS] SetResManParam exception - boot continues" << std::endl;
        }

        if (Client::enableResManFlush) {
            const unsigned int sweep =
                (Client::resManSweepMs > 0)
                    ? static_cast<unsigned int>(Client::resManSweepMs)
                    : 10000u;
            // Outer SweepCache period (cmp edx, imm32) — opcode 81 FA <imm32>
            Memory::WriteInt(0x00411BE2 + 2, sweep);
            // Object-age thresholds inside SweepCache (stock 300000).
            // Skip MR's 0x41625F — IDA shows it is NOT a delay immediate on BeiDou.exe.
            struct SweepSite {
                DWORD addr;
                int immOff; // bytes from insn start to imm32
            };
            static const SweepSite kAgeSites[] = {
                {0x00411CBD, 1}, // cmp eax, imm32 (3D)
                {0x00411D70, 2}, // cmp ecx, imm32 (81 F9)
                {0x00411E13, 2},
                {0x00411EC5, 2},
                {0x00411F68, 2},
                {0x0041201A, 2},
                {0x004120BD, 2},
                {0x00412125, 1}, // mov edi, imm32 (BF)
                {0x00412282, 2},
                {0x00412303, 2},
                {0x00412388, 2},
            };
            for (const auto& s : kAgeSites) {
                Memory::WriteInt(s.addr + s.immOff, sweep);
            }
            // CField::Init: FlushCachedObjects(0) instead of 180000.
            Memory::WriteInt(0x00529320 + 1, 0);
            std::cout << "[RS] ResMan flush patches on sweepMs=" << sweep
                      << " CFieldFlushAge=0" << std::endl;

            // Opportunistic flush once caches exist (login UI still ahead).
            try {
                IWzResManPtr& rm2 = get_rm();
                if (rm2) {
                    rm2->FlushCachedObjects(0);
                }
            } catch (...) {
            }
        }
    }
    if (!rs_try_mount_custom_seh()) {
        if (g_rs_mount_seh) {
            std::cout << "[RS] Custom.wz soft-fail (seh 0x" << std::hex << g_rs_mount_seh
                      << std::dec << ") - boot continues" << std::endl;
        }
        try { rs_resman_reset(); } catch (...) {}
    }
}

void CWvsApp::CleanUp_hook() {
    try {
        CWvsApp::CleanUp(this);
    } catch (...) {
    }
    g_mountOk = false;
    g_customNS = nullptr;
}

void rs_resman_init() {
    try {
        ATTACH_HOOK(CWvsApp::InitializeResMan, CWvsApp::InitializeResMan_hook);
        ATTACH_HOOK(CWvsApp::CleanUp, CWvsApp::CleanUp_hook);
        std::cout << "[RS] Custom.wz resman hooks registered (fail-soft)" << std::endl;
    } catch (...) {
        std::cout << "[RS] Custom.wz resman_init failed - skipped" << std::endl;
    }
}

void rs_resman_flush_cached(int nUsedBefore) {
    if (!Client::enableResManTimeout || !Client::enableResManFlush) {
        return;
    }
    try {
        IWzResManPtr& rm = get_rm();
        if (!rm) {
            return;
        }
        rm->FlushCachedObjects(nUsedBefore);
    } catch (...) {
    }
}
