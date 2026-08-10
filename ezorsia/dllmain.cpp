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
#include "compat/rs/rs.h"
#include <iostream>
#ifndef BEIDOU_MINIMAL_PLUGIN
#include "compat/LazyCompatInit.h"
#include "higherstoragelist/HigherStorageListApi.h"
#include "highershoplist/HigherShopListApi.h"
#include "maxhpmp/MaxHpMpApi.h"
#include "level300/Level300Api.h"
#include "mesouncap/MesoUncapApi.h"
#include "personalshop/PersonalShopApi.h"
#include "charslots/CharSlotsApi.h"
#include "shoulders/ShoulderApi.h"
#include "equipaddon/EquipAddonApi.h"
#include "pendant2/Pendant2Api.h"
#include "gamedata/GameDataGuardApi.h"
#include "quicklogin/QuickLoginApi.h"
#include "bootlog/LoadTraceApi.h"
#endif
#include "bootlog/BootLog.h"
#include "bootlog/CrashDiag.h"
#pragma comment(lib, "ws2_32.lib")

// Keep in sync with LazyCompatInit.cpp / ModRegistry.cpp.
// 0 = full late UI (C497-class). Skill.wz restored; K hang was Data not Level300.
#ifndef BISECT_DISABLE_LATE_UI_HOOKS
#define BISECT_DISABLE_LATE_UI_HOOKS 0
#endif

// Boot bisect 2026-08-07: full Release → InitializeGr2D E_FAIL (0x80004005) on native
// Gr2D while stock ijl15 boots GREEN.
// 0 = full attach path; 1 = ijl15+IP only (LOGIN GREEN proven).
// Stages (when LOGIN_ONLY=0): use BEIDOU_BOOT_STAGE to re-enable chunks.
//   1=core hooks  2=+rs_resman  3=+UpdateResolution  4=+Client patches  5=+features/LoadTrace
// 2026-08-07 flash-crash: LOGIN_ONLY=1 skips UpdateResolution → soft Exit(0) ~5s/16MB.
// Full attach + ImmDisableIME + AttachLoadTrace OFF; pair with DX9 proxy on Client_1.
#ifndef BEIDOU_BOOT_LOGIN_ONLY
#define BEIDOU_BOOT_LOGIN_ONLY 0
#endif
#ifndef BEIDOU_BOOT_STAGE
#define BEIDOU_BOOT_STAGE 5
#endif

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

#ifndef BEIDOU_MINIMAL_PLUGIN
static void AttachLevel300ModWithBootLog(HMODULE /*hModule*/)
{
	BootLogStage("AttachLevel300Mod BEGIN");
	AttachLevel300Mod();
	BootLogStage("AttachLevel300Mod END");
#if BISECT_DISABLE_LATE_UI_HOOKS
	BootLog("BISECT_DISABLE_LATE_UI_HOOKS=1 (worldmap/damageskin/fusionanvil/setitem-ui OFF)");
#endif
}
#endif

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
	// Native Gr2D_DX8 (AB1C9422): AllocConsole/CONOUT$ freopen during DllMain
	// breaks IWzGr2D::Initialize (E_FAIL → softfail). Stock ijl has no console.
	// Do NOT AllocConsole. Silence iostreams so accidental std::cout in DllMain
	// cannot trip ucrtbase abort (0xc0000409) on a detached stdout.
	std::cout.setstate(std::ios_base::failbit);
	std::cerr.setstate(std::ios_base::failbit);
	std::clog.setstate(std::ios_base::failbit);

	// SogouPY.ime has AV'd mid-boot (beidou-crash @ SogouPY.ime+…). Disable IME
	// for this process before CreateWindow/Gr2D — stock has no IME hook either.
	if (HMODULE imm = LoadLibraryA("imm32.dll")) {
		using ImmDisableIME_t = BOOL(WINAPI*)(DWORD);
		if (auto fn = reinterpret_cast<ImmDisableIME_t>(GetProcAddress(imm, "ImmDisableIME"))) {
			fn(static_cast<DWORD>(-1));
		}
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
	{
		CreateConsole();	//console for devs, use this to log stuff if you want
		BootLog_Init(hModule);
#if !BEIDOU_SIZE_TRIM_DLLMAIN
		BootLog_InstallCrashVeh();
#if !BEIDOU_BOOT_LOGIN_ONLY
		CrashDiag_Init(hModule);
		// CrashDiag_AttachHooks Detours _com_raise_error / GetObjectA — leave OFF
		// until native Gr2D login is stable (same family as AttachLoadTrace E_FAIL).
		// CrashDiag_AttachHooks();
#endif
#endif
		BootLogStage("DllMain ATTACH begin");
		BootLog("BEIDOU_BOOT_LOGIN_ONLY=%d STAGE=%d", BEIDOU_BOOT_LOGIN_ONLY, BEIDOU_BOOT_STAGE);
		const std::string configPath = GetConfigIniPath(hModule);
		BootLog("config.ini path=%s", configPath.c_str());
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
			Client::quickLogin = reader.GetBoolean("optional", "quickLogin", true);
			Client::allowCashTrade = reader.GetBoolean("optional", "allowCashTrade", true);
			// Prefer enableGrowthCompanion; also accept legacy enableEquipGrowthTip.
			Client::enableGrowthCompanionTip =
					reader.GetBoolean("optional", "enableGrowthCompanion",
							reader.GetBoolean("optional", "enableEquipGrowthTip", true));
			rs_tier = reader.GetInteger("general", "soScreenResolution", -1);
			rs_field_follow_login = (rs_tier < 0);
			BootLog("config.ini OK %dx%d ip=%s:%d", Client::m_nGameWidth, Client::m_nGameHeight,
				Client::ServerIP_AddressFromINI.c_str(), Client::serverIP_Port);
		} else {
			BootLog("config.ini PARSE FAIL code=%d (using defaults)", reader.ParseError());
		}

		BootLogStage("DllMain Client::UpdateGameStartup");
		Client::UpdateGameStartup();
#if !BEIDOU_BOOT_LOGIN_ONLY && (BEIDOU_BOOT_STAGE >= 1)
		BootLogStage("DllMain core hooks");
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
#endif
#if !BEIDOU_BOOT_LOGIN_ONLY && (BEIDOU_BOOT_STAGE >= 2)
		// Custom.wz SysOpt UI stretch — fail-soft mount (never abort boot on E_FAIL).
		BootLogStage("DllMain rs_resman_init");
		rs_resman_init();
		if (rs_tier < 0) {
			rs_tier = rs_tier_from_dims(Client::m_nGameWidth, Client::m_nGameHeight);
			rs_field_follow_login = true;
		}
		if (rs_tier > RS_TIER_MAX) rs_tier = 0;
		BootLog("RS login %dx%d field_tier=%d follow=%d", Client::m_nGameWidth, Client::m_nGameHeight,
			rs_tier, rs_field_follow_login ? 1 : 0);
#endif
#if !BEIDOU_BOOT_LOGIN_ONLY && (BEIDOU_BOOT_STAGE >= 3)
		BootLogStage("DllMain UpdateResolution");
		Client::UpdateResolution(); // WindowedMode + BeiDou login resolution
		rs_set_login_dims(Client::m_nGameWidth, Client::m_nGameHeight);
		rs_width = Client::m_nGameWidth;
		rs_height = Client::m_nGameHeight;
		rs_adjust_cy = (rs_height > 600) ? (rs_height - 600) / 2 : 0;
#endif
#if !BEIDOU_BOOT_LOGIN_ONLY && (BEIDOU_BOOT_STAGE >= 4)
		BootLogStage("DllMain Client patches");
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
#endif
#if !BEIDOU_BOOT_LOGIN_ONLY && (BEIDOU_BOOT_STAGE >= 5)
#ifndef BEIDOU_MINIMAL_PLUGIN
		BootLogStage("DllMain feature mods");
		HigherStorageList::ApplyPatches();
		BootLog("HigherStorageList OK");
		HigherShopList::ApplyPatches();
		BootLog("HigherShopList OK");
		AttachMaxHpMpMod();
		BootLog("MaxHpMp OK");
		AttachLevel300ModWithBootLog(hModule);
		AttachMesoUncapMod();
		BootLog("MesoUncap OK");
		AttachGameDataGuard();
		BootLog("GameDataGuard OK");
		AttachPersonalShopMod();
		BootLog("PersonalShop OK");
		if (Client::quickLogin) {
			AttachQuickLoginMod();
			BootLog("QuickLogin OK");
		}
		if (Client::allowCashTrade) {
			AttachAllowCashTradeMod();
			BootLog("AllowCashTrade OK");
		}
		AttachShoulderSlotsFix();
		BootLog("ShoulderSlots OK");
		LazyCompatInit::InstallBootstrapHook();
		BootLog("LazyCompat bootstrap OK");
		// LoadTrace Detours InitializeGr2D/throw_hr — on this host causes
		// IWzGr2D::Initialize E_FAIL (0x80004005) → softfail ExitProcess.
		// Keep OFF until Gr2D A/B with native AB1C9422 stays green.
		BootLog("AttachLoadTrace BEGIN (re-enabled 08-10 A/B)");
		AttachLoadTrace();
		BootLog("AttachLoadTrace END");
#else
		BootLog("BEIDOU_MINIMAL_PLUGIN — feature mods skipped");
#endif
		BootLogStage("DllMain rs_register");
		rs_register();
#endif
		BootLogStage("DllMain ijl15 proxy");
		ijl15::CreateHook();
		BootLogStage("DllMain ATTACH done — waiting for CWvsApp::Init");
		break;
	}
	default: break;
	case DLL_PROCESS_DETACH:
		ExitProcess(0);
	}
	return TRUE;
}





