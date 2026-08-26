// nmcogame.dll stub — disables nProtect GameGuard for a private server.
// Exports the same NMCO_* entry points as no-ops (return 0) so BeiDou.exe's
// imports resolve but no integrity checks run (allows a custom d3d8.dll wrapper).
// __cdecl: caller cleans the stack, so arg counts don't matter.
#include <windows.h>

extern "C" {

__declspec(dllexport) int __cdecl NMCO_CallNMFunc(void) { return 0; }
__declspec(dllexport) void __cdecl NMCO_MemoryFree(void* p) { (void)p; }
__declspec(dllexport) int __cdecl NMCO_SetLocale(void) { return 0; }
__declspec(dllexport) int __cdecl NMCO_SetLocaleAndRegion(void) { return 0; }
__declspec(dllexport) int __cdecl NMCO_SetPatchOption(void) { return 0; }
__declspec(dllexport) int __cdecl NMCO_SetUseFriendModuleOption(void) { return 0; }
__declspec(dllexport) int __cdecl NMCO_SetUseNGMOption(void) { return 0; }
__declspec(dllexport) int __cdecl NMCO_SetVersionFileUrl(void) { return 0; }
__declspec(dllexport) int __cdecl NMCO_SetVersionFileUrlA(const char* s) { (void)s; return 0; }
__declspec(dllexport) int __cdecl NMCO_SetVersionFileUrlW(const wchar_t* s) { (void)s; return 0; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p) {
    (void)h; (void)r; (void)p;
    return TRUE;
}

}
