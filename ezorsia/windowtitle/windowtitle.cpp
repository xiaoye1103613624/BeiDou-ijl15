// windowtitle.cpp — sync Win32 game caption with local character info.

//

// IDA (BeiDou.exe @ 0x400000, BeiDou_GMS_083.exe.i64):

//   CWvsApp*            0x00BE7B38  HWND @ +0x4

//   CWvsContext*        0x00BE7918  CharacterData* @ +0x20B8

//   GetCharacterName    0x004AC308  returns GW_CharacterStat+4 (name buf)

//   GW_CharacterStat    +0x0 dwCharacterID; Level tear +0x33/CS+0x35;

//                       Job tear +0x39/CS+0x3D

//   ZtlSecureFuse_byte  0x0047465D  (level300 may return ushort via LVL3)

//   ZtlSecureFuse_short 0x004746DD

//   CConfig*            0x00BEBF9C  GetWorld 0x98781E / GetChannel 0x98787A

//   CWvsApp::CallUpdate 0x009F84D0  (per-frame; hooked once in rs.cpp)

//   StringPool #1163    window title at CreateWindow (see ReplacementFuncs)

//

// No existing hook addresses were changed.



#include "stdafx.h"

#include "WindowTitleApi.h"

#include "compat/ClientAddresses.h"

#include "compat/wvs/util.h"

#include "compat/ztl/zcom.h"



#include <comdef.h>

#include <cstdio>

#include <cstring>

#include <string>

#include <unordered_map>



namespace {



constexpr const wchar_t* kBaseTitle = L"XiaoYeMS v0.0.1";

constexpr uintptr_t kAddr_CWvsContext = 0x00BE7918;

constexpr uintptr_t kAddr_CConfig = 0x00BEBF9C;

constexpr uintptr_t kOff_CharacterData = 0x20B8;

constexpr uintptr_t kAddr_GetCharacterName = 0x004AC308;

constexpr uintptr_t kAddr_FuseByte = 0x0047465D;

constexpr uintptr_t kAddr_FuseShort = 0x004746DD;

constexpr uintptr_t kAddr_GetChannel = 0x0098787A;

constexpr int kOff_CharId = 0x0;

constexpr int kOff_LevelTear = 0x33;

constexpr int kOff_LevelCs = 0x35;

constexpr int kOff_JobTear = 0x39;

constexpr int kOff_JobCs = 0x3D;



using FuseByteFn = int(__cdecl*)(void* tear, unsigned int checksum);

using FuseShortFn = short(__cdecl*)(void* tear, unsigned int checksum);

using GetChannelFn = int(__thiscall*)(void* cfg);

using GetNameFn = const char*(__thiscall*)(void* ctx);



long long g_limitBreak = -1; // <0 → omit from caption until server sync

std::wstring g_lastTitle;

DWORD g_lastTick = 0;

DWORD g_sessionStartTick = 0;

int g_lastLoggedCharId = 0;

std::unordered_map<int, std::wstring> g_jobNameCache;



HWND GameHwnd() {

    __try {

        void* app = *reinterpret_cast<void**>(ClientAddresses::kCWvsAppPtr);

        if (!app) {

            return nullptr;

        }

        HWND hwnd = *reinterpret_cast<HWND*>(

                reinterpret_cast<char*>(app) + ClientAddresses::kCWvsAppHwndOffset);

        if (hwnd && IsWindow(hwnd)) {

            return hwnd;

        }

    } __except (EXCEPTION_EXECUTE_HANDLER) {

    }

    return nullptr;

}



void* CharacterData() {

    __try {

        void* ctx = *reinterpret_cast<void**>(kAddr_CWvsContext);

        if (!ctx) {

            return nullptr;

        }

        return *reinterpret_cast<void**>(reinterpret_cast<char*>(ctx) + kOff_CharacterData);

    } __except (EXCEPTION_EXECUTE_HANDLER) {

        return nullptr;

    }

}



int ReadCharId(void* charData) {

    __try {

        return *reinterpret_cast<int*>(reinterpret_cast<char*>(charData) + kOff_CharId);

    } __except (EXCEPTION_EXECUTE_HANDLER) {

        return 0;

    }

}



const char* ReadCharName() {

    __try {

        void* ctx = *reinterpret_cast<void**>(kAddr_CWvsContext);

        if (!ctx) {

            return nullptr;

        }

        return reinterpret_cast<GetNameFn>(kAddr_GetCharacterName)(ctx);

    } __except (EXCEPTION_EXECUTE_HANDLER) {

        return nullptr;

    }

}



int ReadLevel(void* charData) {

    __try {

        void* tear = reinterpret_cast<char*>(charData) + kOff_LevelTear;

        unsigned int cs = *reinterpret_cast<unsigned int*>(

                reinterpret_cast<char*>(charData) + kOff_LevelCs);

        return reinterpret_cast<FuseByteFn>(kAddr_FuseByte)(tear, cs);

    } __except (EXCEPTION_EXECUTE_HANDLER) {

        return 0;

    }

}



int ReadJob(void* charData) {

    __try {

        void* tear = reinterpret_cast<char*>(charData) + kOff_JobTear;

        unsigned int cs = *reinterpret_cast<unsigned int*>(

                reinterpret_cast<char*>(charData) + kOff_JobCs);

        return static_cast<int>(reinterpret_cast<FuseShortFn>(kAddr_FuseShort)(tear, cs));

    } __except (EXCEPTION_EXECUTE_HANDLER) {

        return 0;

    }

}



int ReadChannel() {

    __try {

        void* cfg = *reinterpret_cast<void**>(kAddr_CConfig);

        if (!cfg) {

            return -1;

        }

        // CConfig stores 0-based channel index; UI shows 1-based.

        const int raw = reinterpret_cast<GetChannelFn>(kAddr_GetChannel)(cfg);

        if (raw < 0) {

            return -1;

        }

        return raw + 1;

    } __except (EXCEPTION_EXECUTE_HANDLER) {

        return -1;

    }

}



std::wstring Utf8OrAcpToWide(const char* s) {

    if (!s || !*s) {

        return L"";

    }

    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);

    if (n <= 1) {

        n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);

        if (n <= 1) {

            return L"";

        }

        std::wstring out(static_cast<size_t>(n - 1), L'\0');

        MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[0], n);

        return out;

    }

    std::wstring out(static_cast<size_t>(n - 1), L'\0');

    MultiByteToWideChar(CP_ACP, 0, s, -1, &out[0], n);

    return out;

}



std::wstring ReadWzStringField(IWzPropertyPtr prop, const wchar_t* key) {

    if (!prop || !key) {

        return L"";

    }

    Ztl_variant_t value = prop->item[key];

    if (value.vt == VT_BSTR && value.bstrVal) {

        return std::wstring(value.bstrVal, SysStringLen(value.bstrVal));

    }

    return L"";

}



std::wstring LookupJobNameWz(int jobId) {

    if (!get_rm() || jobId <= 0) {

        return L"";

    }

    try {

        wchar_t path[80] = {};

        static const wchar_t* kFmts[] = {

                L"String/Skill.img/%d",

                L"String/Skill.img/%07d",

        };

        static const wchar_t* kKeys[] = {L"name", L"bookName"};

        for (const wchar_t* fmt : kFmts) {

            swprintf_s(path, fmt, jobId);

            IWzPropertyPtr prop = get_rm()->GetObjectA(path).GetUnknown();

            if (!prop) {

                continue;

            }

            for (const wchar_t* key : kKeys) {

                const std::wstring name = ReadWzStringField(prop, key);

                if (!name.empty()) {

                    return name;

                }

            }

        }

    } catch (...) {

    }

    return L"";

}



std::wstring JobName(int jobId) {

    if (jobId <= 0) {

        return L"";

    }

    const auto it = g_jobNameCache.find(jobId);

    if (it != g_jobNameCache.end()) {

        return it->second;

    }

    const std::wstring name = LookupJobNameWz(jobId);

    g_jobNameCache[jobId] = name;

    return name;

}



void AppendSpaced(std::wstring& out, const std::wstring& piece) {

    if (piece.empty()) {

        return;

    }

    if (!out.empty()) {

        out += L"  ";

    }

    out += piece;

}



void AppendBracketInt(std::wstring& out, int value) {

    wchar_t buf[32];

    swprintf_s(buf, L"[%d]", value);

    out += buf;

}



std::wstring FormatTodayDate() {

    SYSTEMTIME st = {};

    GetLocalTime(&st);

    wchar_t buf[32];

    swprintf_s(buf, L"%04d\u5e74%02d\u6708%02d\u65e5", st.wYear, st.wMonth, st.wDay);

    return buf;

}



std::wstring FormatOnlineDuration() {

    if (g_sessionStartTick == 0) {

        return L"";

    }

    DWORD elapsedMs = GetTickCount() - g_sessionStartTick;

    const unsigned long long totalMinutes = elapsedMs / 60000ULL;

    const int hours = static_cast<int>(totalMinutes / 60ULL);

    const int minutes = static_cast<int>(totalMinutes % 60ULL);

    wchar_t buf[32];

    swprintf_s(buf, L"[%d\u65f6%d\u5206]", hours, minutes);

    return buf;

}



void UpdateSessionClock(int charId) {

    if (charId > 0) {

        if (g_lastLoggedCharId <= 0) {

            g_sessionStartTick = GetTickCount();

        }

        g_lastLoggedCharId = charId;

        return;

    }

    g_lastLoggedCharId = 0;

    g_sessionStartTick = 0;

}



void ApplyTitle(const std::wstring& title) {

    if (title == g_lastTitle) {

        return;

    }

    HWND hwnd = GameHwnd();

    if (!hwnd) {

        return;

    }

    SetWindowTextW(hwnd, title.c_str());

    g_lastTitle = title;

}



std::wstring BuildTitle(void* charData) {

    std::wstring title = kBaseTitle;



    int charId = 0;

    std::wstring name;

    int level = 0;

    int jobId = 0;

    int channel = -1;



    if (charData) {

        charId = ReadCharId(charData);

        name = Utf8OrAcpToWide(ReadCharName());

        level = ReadLevel(charData);

        jobId = ReadJob(charData);

        channel = ReadChannel();

    }

    UpdateSessionClock(charId);



    if (charId > 0 || !name.empty()) {

        std::wstring segment = L"\u89d2\u8272\uff1a"; // 角色：

        if (charId > 0) {

            AppendBracketInt(segment, charId);

        }

        if (!name.empty()) {

            segment += L"[";

            segment += name;

            segment += L"]";

        }

        AppendSpaced(title, segment);

    }



    if (level > 0) {

        std::wstring segment = L"\u7b49\u7ea7\uff1a"; // 等级：

        AppendBracketInt(segment, level);

        AppendSpaced(title, segment);

    }



    if (jobId > 0) {

        const std::wstring jobName = JobName(jobId);

        std::wstring segment = L"\u804c\u4e1a\uff1a"; // 职业：

        AppendBracketInt(segment, jobId);

        if (!jobName.empty()) {

            segment += L"[";

            segment += jobName;

            segment += L"]";

        }

        AppendSpaced(title, segment);

    }



    if (channel > 0) {

        std::wstring segment = L"\u9891\u9053\uff1a"; // 频道：

        AppendBracketInt(segment, channel);

        AppendSpaced(title, segment);

    }



    if (g_limitBreak >= 0) {

        std::wstring segment = L"\u7834\u529f\u503c\uff1a"; // 破功值：

        wchar_t buf[32];

        swprintf_s(buf, L"[%lld]", g_limitBreak);

        segment += buf;

        AppendSpaced(title, segment);

    }



    AppendSpaced(title, FormatTodayDate());



    const std::wstring duration = FormatOnlineDuration();

    if (!duration.empty()) {

        AppendSpaced(title, duration);

    }



    return title;

}



} // namespace



namespace WindowTitle {



void InstallEarlyUpdate() {
    // Title refresh runs from rs.cpp CWvsApp::CallUpdate_hook (rs_attach).
    // Do NOT DetourAttach CallUpdate here — rs_attach hooks the same VA and
    // double-hooking caused boot AV @ BeiDou.exe+0x5F8510 (2026-09-03).
}



void SetLimitBreak(long long value) {

    g_limitBreak = value;

    g_lastTitle.clear(); // force refresh next tick

}



long long GetLimitBreak() {

    return g_limitBreak;

}



void OnTick() {

    const DWORD now = GetTickCount();

    if (g_lastTick != 0 && (now - g_lastTick) < 250) {

        return;

    }

    g_lastTick = now;

    ApplyTitle(BuildTitle(CharacterData()));

}



} // namespace WindowTitle

