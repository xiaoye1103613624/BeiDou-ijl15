#pragma once

#include <windows.h>

// KA-style dual diagnostic logs for BeiDou-ijl15:
//   beidou-crash.log      - process-fatal AV / illegal insn / stack overflow (VEH, once)
//   beidou-com-error.log  - E_POINTER via _com_raise_error (max 16, still throws)
//
// Hook targets (IDA BeiDou_GMS_083, imagebase 0x400000):
//   _com_raise_error          VA 0x00A605C3  RVA 0x6605C3  (__cdecl, noreturn)
//   IWzResMan::GetObjectA     VA 0x00403A93  RVA 0x003A93  (__thiscall / fastcall)
//   CWndMan::TranslateMessage VA 0x009E7D77  RVA 0x5E7D77  (msg context only)

void CrashDiag_Init(HMODULE hModule);
void CrashDiag_AttachHooks();

// Late-only: hook _com_raise_error after ResMan/FsInit (never DllMain).
// Logs 0x80030002 / 0x80004003 IErrorInfo description into beidou-com-error.log
// and the RecentWZ ring (so beidou-wz-last.log can show the missing UOL).
void CrashDiag_AttachComRaiseDiag();

// WndProc / TranslateMessage outer context (InterlockedExchange; clear after original).
void CrashDiag_SetMsgContext(DWORD threadId, UINT msg, WPARAM wParam, LPARAM lParam);
void CrashDiag_ClearMsgContext();

// Optional: record a WZ path from other NameSpace hooks (e.g. rs_ns_hook).
void CrashDiag_NoteWzPath(const wchar_t* path);

// Dump recent WZ paths to beidou-wz-last.log (no hooks required).
// Returns false if all write attempts failed; optional outGle is last Win32 error.
bool CrashDiag_DumpRecentWzPaths(const char* reason, DWORD* outGle = nullptr);

// Record IErrorInfo / com_error description for the next wz-last dump
// (fail_desc= / suspect_fail=). Call before DumpRecentWzPaths on throw.
void CrashDiag_NoteFailDescription(const wchar_t* desc);

// Path-spy TLS: in-flight GetObjectA + last call that returned VT_ERROR / FAILED hr.
// Used when IErrorInfo.Description is empty (common on 0x8007000D enter-map).
void CrashDiag_NoteGetObjectInFlight(const wchar_t* path);
void CrashDiag_ClearGetObjectInFlight();
void CrashDiag_NoteGetObjectFail(const wchar_t* path, HRESULT hr);
// If fail_desc empty, synthesize from getobj_fail / inflight for dump + BootLog.
void CrashDiag_EnsureFailDescFromGetObjectSpy();
