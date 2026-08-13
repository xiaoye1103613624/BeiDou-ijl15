#pragma once

#include <windows.h>

// Client boot / load log for flash-crash diagnosis.
// Output: <client_dir>\client_boot.log
// After BootLog_SetQuiet(true), only crash VEH lines are written (no flush-spam).

void BootLog_Init(HMODULE hModule);
void BootLog(const char* fmt, ...);
void BootLogStage(const char* stage); // set last-stage + log "STAGE ..."
const char* BootLog_LastStage();
void BootLog_InstallCrashVeh();
void BootLog_SetQuiet(bool quiet); // stop routine logging (keep crash VEH)
bool BootLog_IsQuiet();
