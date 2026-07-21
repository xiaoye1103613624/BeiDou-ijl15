// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "NMCO.h"
#include "ijl15.h"
#include "INIReader.h"
#include "ReplacementFuncs.h"
#include <comutil.h>
#ifndef BEIDOU_MINIMAL_PLUGIN
#include "BossHP.h"
#endif
#include "HpMpAlert.h"
#include "SelectCharMacFix.h"
#ifndef BEIDOU_MINIMAL_PLUGIN
#include "compat/LazyCompatInit.h"
#include "higherstoragelist/HigherStorageListApi.h"
#include "highershoplist/HigherShopListApi.h"
#include "maxhpmp/MaxHpMpApi.h"
#include "level300/Level300Api.h"
#include "personalshop/PersonalShopApi.h"
#include "charslots/CharSlotsApi.h"
#include "shoulders/ShoulderApi.h"
#include "gamedata/GameDataGuardApi.h"
#endif
#pragma comment(lib, "ws2_32.lib")

// config.ini can use IP or hostname (ServerIP_Address=...).
// The patch expects an IPv4 dotted string; resolve hostnames to IPv4.
// On failure, fall back to the original value.
static std::string GetConfigIniPath(HMODULE module)
{
	char dllPath[MAX_PATH]{};
	if (GetModuleFileNameA(module, dllPath, MAX_PATH) == 0) {
		return "config.ini";
	}

	std::string path(dllPath);
	const auto slash = path.find_last_of("\\/");
	if (slash == std::string::npos) {
		return "config.ini";
	}

	return path.substr(0, slash + 1) + "config.ini";
}

// SEH helper must not live in DllMain (C2712: no __try with C++ unwinding).
static void WriteBootLine(HMODULE hModule, const char* line)
{
	char bootPath[MAX_PATH]{};
	GetModuleFileNameA(hModule, bootPath, MAX_PATH);
	if (char* slash = strrchr(bootPath, '\\')) {
		*(slash + 1) = '\0';
	}
	strcat_s(bootPath, "plugin_boot.txt");
	HANDLE boot = CreateFileA(bootPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (boot != INVALID_HANDLE_VALUE) {
		DWORD w = 0;
		WriteFile(boot, line, (DWORD)strlen(line), &w, nullptr);
		FlushFileBuffers(boot);
		CloseHandle(boot);
	}
}

static void AttachLevel300ModWithBootLog(HMODULE hModule)
{
	WriteBootLine(hModule, "before AttachLevel300Mod\r\n");
	__try {
		AttachLevel300Mod();
		WriteBootLine(hModule, "after AttachLevel300Mod\r\n");
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		WriteBootLine(hModule, "EXCEPTION in AttachLevel300Mod\r\n");
	}
}

static std::string ResolveToIpv4String(const std::string& hostOrIp)
{
	if (hostOrIp.empty()) return hostOrIp;

	IN_ADDR parsedAddr{};
	if (InetPtonA(AF_INET, hostOrIp.c_str(), &parsedAddr) == 1) {
		return hostOrIp;
	}

	WSADATA wsaData{};
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		return hostOrIp;
	}

	addrinfo hints{};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo* result = nullptr;
	const int gaiRc = getaddrinfo(hostOrIp.c_str(), nullptr, &hints, &result);
	if (gaiRc != 0 || result == nullptr) {
		WSACleanup();
		return hostOrIp;
	}

	char ipBuf[INET_ADDRSTRLEN]{};
	const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
	const PCSTR ipStr = InetNtopA(AF_INET, const_cast<IN_ADDR*>(&ipv4->sin_addr), ipBuf, sizeof(ipBuf));

	freeaddrinfo(result);
	WSACleanup();

	if (ipStr == nullptr) {
		return hostOrIp;
	}

	return std::string(ipStr);
}

void CreateConsole() {
	AllocConsole();
	FILE* stream;
	freopen_s(&stream, "CONOUT$", "w", stdout); //CONOUT$
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
	{
		//CreateConsole();	//console for devs, use this to log stuff if you want
		const std::string configPath = GetConfigIniPath(hModule);
		INIReader reader(configPath);
		if (reader.ParseError() == 0) {
			Client::m_nGameWidth = reader.GetInteger("general", "width", 1280);
			Client::m_nGameHeight = reader.GetInteger("general", "height", 720);
			Client::MsgAmount = reader.GetInteger("general", "MsgAmount", 26);
			Client::CustomLoginFrame = reader.GetBoolean("general", "CustomLoginFrame", true);
			Client::WindowedMode = reader.GetBoolean("general", "WindowedMode", true);
			Client::RemoveLogos = reader.GetBoolean("general", "RemoveLogos", true);
			Memory::UseVirtuProtect = reader.GetBoolean("general", "UseVirtuProtect", true);
			Client::setDamageCap = reader.GetReal("optional", "setDamageCap", 199999);
			Client::setMAtkCap = reader.GetReal("optional", "setMAtkCap", 1999);
			Client::setAccCap = reader.GetReal("optional", "setAccCap", 999);
			Client::setAvdCap = reader.GetReal("optional", "setAvdCap", 999);
			Client::setAtkOutCap = reader.GetReal("optional", "setAtkOutCap", 199999);
			Client::useTubi = reader.GetBoolean("optional", "useTubi", false);
			Client::bigLoginFrame = reader.GetBoolean("general", "bigLoginFrame", false);
			Client::SwitchChinese = reader.GetBoolean("general", "SwitchChinese", false);
			Client::speedMovementCap = reader.GetInteger("optional", "speedMovementCap", 140);
			Client::jumpCap = reader.GetInteger("optional", "jumpCap", 123);
			Client::debug = reader.GetBoolean("debug", "debug", false);
			Client::noPassword = reader.GetBoolean("debug", "noPassword", false);
			Client::disablePacketHook = reader.GetBoolean("debug", "disablePacketHook", false);
			Client::disableBossHP = reader.GetBoolean("debug", "disableBossHP", false);
			Client::disableWorldMap = reader.GetBoolean("debug", "disableWorldMap", false);
			Client::imeType = reader.GetInteger("general", "imeType", 1);
			ownLoginFrame = reader.GetBoolean("optional", "ownLoginFrame", false);
			ownCashShopFrame = reader.GetBoolean("optional", "ownCashShopFrame", false);
			EzorsiaV2WzIncluded = reader.GetBoolean("general", "EzorsiaV2WzIncluded", true);
			Client::ServerIP_AddressFromINI = ResolveToIpv4String(reader.Get("general", "ServerIP_Address", "127.0.0.1"));
			Client::serverIP_Port = reader.GetInteger("general", "serverIP_Port", 8484);
			Client::climbSpeedAuto = reader.GetBoolean("optional", "climbSpeedAuto", false);
			Client::climbSpeed = reader.GetFloat("optional", "climbSpeed", 1.0);
			Client::talkRepeat = reader.GetBoolean("optional", "talkRepeat", false);
			Client::talkTime = reader.GetInteger("optional", "talkTime", 2000);
		}

		Hook_CreateMutexA(true); //multiclient //ty darter, angel, and alias!
		HookCreateWindowExA(true); //default ezorsia
		HookGetModuleFileName(true); //default ezorsia
		HookPcCreateObject_IWzResMan(true);
		HookPcCreateObject_IWzNameSpace(true);
		HookPcCreateObject_IWzFileSystem(true);
		HookCWvsApp__Dir_BackSlashToSlash(true);
		HookCWvsApp__Dir_upDir(true);
		Hookbstr_ctor(true);
		HookIWzFileSystem__Init(true);
		HookIWzNameSpace__Mount(true);
		HookCWvsApp__InitializeResMan(false); //experimental //ty to all the contributors of the ragezone release: Client load .img instead of .wz v62~v92
		Hook_StringPool__GetString(true); //hook stringpool modification //ty !! popcorn //ty darter
		Hook_lpfn_NextLevel(true);
		HookSaveGlobal(true);
		HookSelectCharMacFix(true);
		//Hook_get_unknown(true);
		//Hook_get_resource_object(true); //helper function hooks  //ty teto for helping me get started
		//Hook_com_ptr_t_IWzProperty__ctor(true);
		//Hook_com_ptr_t_IWzProperty__dtor(true);

		Client::UpdateGameStartup();

		std::cout << "Applying resolution " << Client::m_nGameWidth << "x" << Client::m_nGameHeight << std::endl;
		Client::UpdateResolution();
		Client::FixMouseWheel();
		Client::Chinese();
		Client::LongQuickSlot();
		Client::FixDateFormat();
		Client::FixItemType();
		Client::JumpCap();
		Client::FixChatPosHook();
		Client::NoPassword();
		Client::MoreHook();
		// TEMP disabled 2026-07-19: isolate error-38 on item drop (UIWindow/ExpandItem redraw).
		// Client::ExpandItem();
		Client::DeleteChar();
#ifndef BEIDOU_MINIMAL_PLUGIN
		// Trunk / shop list row patches are pure WriteByte — safe at DllMain (before any UI).
		HigherStorageList::ApplyPatches();
		// HigherShopList 5→9: requires Shop/backgrnd ~463x499 (+160). PatchStorageBg extend-shop.
		HigherShopList::ApplyPatches();
		// HP/MP 4-byte expansion: Decode2->Decode4 + FakeTear + Fuse hooks + FixMovsx.
		// Must run before first char-stat packet decode (login / map enter).
		AttachMaxHpMpMod();
		// Level ushort expansion + EXP Decode8 caves (server writeLong).
		AttachLevel300ModWithBootLog(hModule);
		// Soften InitializeGameData EC_INVALID_GAME_DATA (StringPool#86) after Data appends.
		AttachGameDataGuard();
		// Player/hired shop: server slotMax=32; UI canvas lengthening still WZ-side.
		AttachPersonalShopMod();
		// DISABLED 2026-07-16: CharSlots addresses are for Characterslot-30/kaentake client,
		// NOT BeiDou.exe. Patch1 overwrites 0F-prefix of jge/jl (0F 8D/0F 7C) with 0x1E,
		// corrupting jumps -> ZException -21E (0x21E) on channel->charselect.
		// CharSlots::ApplyPatches();
		// HARD-OFF 2026-07-19 login bisect: shoulders not the crash; tiny 0115 + inv dup suspected.
		// AttachShoulderSlotsFix();

		// InstallBootstrapHook: RefreshRate early (login-safe); other mods at first CField.
		LazyCompatInit::InstallBootstrapHook();
#else
		// Ultra-minimal: ijl15 proxy + IP/res hooks only — no feature hooks at DllMain.
#endif
		std::cout << "GetModuleFileName hook created" << std::endl;
		ijl15::CreateHook(); //NMCO::CreateHook();

		std::cout << "NMCO hook initialized" << std::endl;
		break;
	}
	default: break;
	case DLL_PROCESS_DETACH:
		ExitProcess(0);
	}
	return TRUE;
}





