#include <windows.h>

// Size-safe stub for frozen-obj links that lack CrashDiag.obj.
// Full CrashDiag.cpp is large and not required for GetSet/Apply enter path.
void CrashDiag_Init(HMODULE /*hModule*/) {}
