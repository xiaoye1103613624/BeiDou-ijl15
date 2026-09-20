#include "stdafx.h"
#include "Memory.h"
#include "detours.h"

bool Memory::UseVirtuProtect = true;

bool Memory::SetHook(bool attach, void** ptrTarget, void* ptrDetour)
{
    if (DetourTransactionBegin() != NO_ERROR)
    {
        return false;
    }

    HANDLE pCurThread = GetCurrentThread();

    if (DetourUpdateThread(pCurThread) == NO_ERROR)
    {
        auto pDetourFunc = attach ? DetourAttach : DetourDetach;

        if (pDetourFunc(ptrTarget, ptrDetour) == NO_ERROR)
        {
            if (DetourTransactionCommit() == NO_ERROR)
            {
                return true;
            }
        }
    }

    DetourTransactionAbort();
    return false;
}

void Memory::FillBytes(const DWORD dwOriginAddress, const unsigned char ucValue, const int nCount) {
	// Always VirtualProtect: targets often include PE .rsrc (e.g. 0xC08459 asInvoker).
	// UseVirtuProtect=false + raw memset → AV in VCRUNTIME140 memset → DllMain fails
	// as STATUS_DLL_INIT_FAILED (0xC0000142). Never write if protect change failed.
	if (nCount <= 0) return;
	DWORD dwOldProtect = 0;
	if (!VirtualProtect((LPVOID)dwOriginAddress, nCount, PAGE_EXECUTE_READWRITE, &dwOldProtect)) {
		return;
	}
	memset((void*)dwOriginAddress, ucValue, nCount);
	VirtualProtect((LPVOID)dwOriginAddress, nCount, dwOldProtect, &dwOldProtect);
}

/*
 注意: 原文本长度>=新文本长度
 示例: Memory::ReplaceString(0x00B3C158, "双击发送消息", "Double-click to send a note.");
*/
void Memory::ReplaceString(const DWORD dwOriginAddress, const char* sContent, const char* oContent)
{
    WriteString(dwOriginAddress, sContent);
    const size_t sSize = strlen(sContent);
    const size_t oSize = strlen(oContent);
    FillBytes(dwOriginAddress + sSize, 0, oSize + 1 - sSize);
}

/*
oSize: 要填充的长度，>=填充的字符串长度，中文=2个长度
示例: Memory::WriteString(0x00AF2B28, "对联盟", 11);
*/
void Memory::WriteString(const DWORD dwOriginAddress, const char* sContent, const int oSize)
{
    WriteString(dwOriginAddress, sContent);
    const size_t sSize = strlen(sContent);
    FillBytes(dwOriginAddress + sSize, 0, oSize + 1 - sSize);
}

void Memory::WriteString(const DWORD dwOriginAddress, const char* sContent) {
    const size_t nSize = strlen(sContent);
	if (nSize == 0) return;
	DWORD dwOldProtect = 0;
	if (!VirtualProtect((LPVOID)dwOriginAddress, nSize, PAGE_EXECUTE_READWRITE, &dwOldProtect)) {
		return;
	}
	memcpy((void*)dwOriginAddress, sContent, nSize);
	VirtualProtect((LPVOID)dwOriginAddress, nSize, dwOldProtect, &dwOldProtect);
}

void Memory::WriteByte(const DWORD dwOriginAddress, const unsigned char ucValue) {
    if (dwOriginAddress < 0x10000) {
        return;
    }
	DWORD dwOldProtect = 0;
	if (!VirtualProtect((LPVOID)dwOriginAddress, sizeof(unsigned char), PAGE_EXECUTE_READWRITE, &dwOldProtect)) {
		return;
	}
	*(unsigned char*)dwOriginAddress = ucValue;
	VirtualProtect((LPVOID)dwOriginAddress, sizeof(unsigned char), dwOldProtect, &dwOldProtect);
}

void Memory::WriteShort(const DWORD dwOriginAddress, const unsigned short usValue) {
	DWORD dwOldProtect = 0;
	if (!VirtualProtect((LPVOID)dwOriginAddress, sizeof(unsigned short), PAGE_EXECUTE_READWRITE, &dwOldProtect)) {
		return;
	}
	*(unsigned short*)dwOriginAddress = usValue;
	VirtualProtect((LPVOID)dwOriginAddress, sizeof(unsigned short), dwOldProtect, &dwOldProtect);
}

void Memory::WriteInt(const DWORD dwOriginAddress, const unsigned int dwValue) {
	DWORD dwOldProtect = 0;
	if (!VirtualProtect((LPVOID)dwOriginAddress, sizeof(unsigned int), PAGE_EXECUTE_READWRITE, &dwOldProtect)) {
		return;
	}
	*(unsigned int*)dwOriginAddress = dwValue;
	VirtualProtect((LPVOID)dwOriginAddress, sizeof(unsigned int), dwOldProtect, &dwOldProtect);
}

void Memory::WriteDouble(const DWORD dwOriginAddress, const double dwValue) {
	DWORD dwOldProtect = 0;
	if (!VirtualProtect((LPVOID)dwOriginAddress, sizeof(double), PAGE_EXECUTE_READWRITE, &dwOldProtect)) {
		return;
	}
	*(double*)dwOriginAddress = dwValue;
	VirtualProtect((LPVOID)dwOriginAddress, sizeof(double), dwOldProtect, &dwOldProtect);
}

void Memory::WriteByteArray(const DWORD dwOriginAddress, unsigned char* ucValue, const int ucValueSize) {
	if (ucValueSize <= 0 || !ucValue) {
		return;
	}
	DWORD dwOldProtect = 0;
	if (!VirtualProtect((LPVOID)dwOriginAddress, ucValueSize, PAGE_EXECUTE_READWRITE, &dwOldProtect)) {
		return;
	}
	memcpy((void*)dwOriginAddress, ucValue, ucValueSize);
	VirtualProtect((LPVOID)dwOriginAddress, ucValueSize, dwOldProtect, &dwOldProtect);
}

void Memory::CodeCave(void* ptrCodeCave, const DWORD dwOriginAddress, const int nNOPCount) { //tested and working
	__try {
		if (dwOriginAddress < 0x10000 || ptrCodeCave == nullptr) {
			return;
		}
		if (nNOPCount) FillBytes(dwOriginAddress, 0x90, nNOPCount); // create space for the jmp
		WriteByte(dwOriginAddress, 0xe9); // jmp instruction
		WriteInt(dwOriginAddress + 1, (int)(((int)ptrCodeCave - (int)dwOriginAddress) - 5)); // [jmp(1 byte)][address(4 bytes)] //this means you need to clear a space of at least 5 bytes (nNOPCount bytes)
	} __except (EXCEPTION_EXECUTE_HANDLER) {}
}
