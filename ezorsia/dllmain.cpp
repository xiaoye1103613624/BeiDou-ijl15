// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "NMCO.h"
#include "ijl15.h"
#include "INIReader.h"
#include "ReplacementFuncs.h"
#include <comutil.h>
#include "BossHP.h"
#include "HpMpAlert.h"
#include "SelectCharMacFix.h"
#include "compat/ModRegistry.h"
#include "compat/LazyCompatInit.h"
#include "higherstoragelist/HigherStorageListApi.h"
#include "highershoplist/HigherShopListApi.h"
#include "maxhpmp/MaxHpMpApi.h"
#include "level300/Level300Api.h"
#include "mesouncap/MesoUncapApi.h"
#include "gamedata/GameDataGuardApi.h"
#include "personalshop/PersonalShopApi.h"
#include "quicklogin/QuickLoginApi.h"
#include "charslots/CharSlotsApi.h"
#include "shoulders/ShoulderApi.h"
#include "compat/rs/rs.h"
#include "compat/hook.h"
#include "Memory.h"
#include "bootlog/BootLog.h"
#include "bootlog/LoadTraceApi.h"
#include "bootlog/CrashDiag.h"
#pragma comment(lib, "ws2_32.lib")

// SEH wrapper must live outside DllMain (C2712: DllMain has C++ unwinding).
static void AttachLevel300ModSafe() {
	__try {
		AttachLevel300Mod();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		BootLog("*** AttachLevel300Mod SEH 0x%08X — continuing boot without Level300",
				GetExceptionCode());
	}
}

// config.ini can use IP or hostname (ServerIP_Address=...).
// The patch expects an IPv4 dotted string; resolve hostnames to IPv4.
// On failure, fall back to the original value.
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
		BootLog_Init(hModule);
		BootLog_InstallCrashVeh();
		// CrashDiag_AttachHooks (GetObjectA / ComRaiseError) MUST NOT run in DllMain —
		// early GetObjectA hook → logo/set_stage AV + 0x80004003「无效的指针」.
		// Paths still recorded via rs_resman CrashDiag_NoteWzPath when that path is used.
		CrashDiag_Init(hModule);
		BootLogStage("DllMain ATTACH begin");
		INIReader reader("config.ini");
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
			Client::quickLogin = reader.GetBoolean("optional", "quickLogin", true);
			Client::allowCashTrade = reader.GetBoolean("optional", "allowCashTrade", true);
			Client::enableGrowthCompanionTip =
					reader.GetBoolean("optional", "enableGrowthCompanion",
							reader.GetBoolean("optional", "enableEquipGrowthTip", false));
			rs_tier = reader.GetInteger("general", "soScreenResolution", -1);
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
		Client::DeleteChar();

		// Early patches that are safe at DllMain (no late UI).
		// Miles AIL_quick_startup ExitProcess(0) stub + boot stage trace (same as Client_1).
		AttachLoadTrace();
		HigherStorageList::ApplyPatches();
		HigherShopList::ApplyPatches();
		AttachMaxHpMpMod();
		// Must match PacketCreator writeShort(level)+writeLong(exp); without this,
		// CHARLIST mis-aligns → garbage avatar UOLs → 0x80030002 on channel select.
		// Soft-fail: Level300 must not take down logo/boot (SEH + Fuse recurse guard).
		AttachLevel300ModSafe();
		AttachMesoUncapMod();
		AttachGameDataGuard();
		AttachPersonalShopMod();
		if (Client::quickLogin) {
			AttachQuickLoginMod();
		}
		if (Client::allowCashTrade) {
			AttachAllowCashTradeMod();
		}
		AttachCharSlotsMod();
		AttachShoulderSlotsFix();

		// Late UI + packet modules attach on first CField via LazyCompat.
		// (replaces early ModRegistry::Initialize + BossHP::Hook in DllMain)
		LazyCompatInit::InstallBootstrapHook();
		rs_register();

		BootLog("GetModuleFileName hook created");
		std::cout << "GetModuleFileName hook created" << std::endl;
		ijl15::CreateHook(); //NMCO::CreateHook();

		BootLog("NMCO hook initialized");
		std::cout << "NMCO hook initialized" << std::endl;
		BootLogStage("DllMain ATTACH end");
		break;
	}
	default: break;
	case DLL_PROCESS_DETACH:
		ExitProcess(0);
	}
	return TRUE;
}
