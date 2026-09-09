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
#include "equipaddon/EquipAddonApi.h"
#include "higherstoragelist/HigherStorageListApi.h"
#include "highershoplist/HigherShopListApi.h"
#include "newchardice/NewCharDiceApi.h"
#include "maxhpmp/MaxHpMpApi.h"
#include "level300/Level300Api.h"
#include "mesouncap/MesoUncapApi.h"
#include "gamedata/GameDataGuardApi.h"
#include "gamedata/SkillTipCrashGuardsApi.h"
#include "personalshop/PersonalShopApi.h"
#include "quicklogin/QuickLoginApi.h"
#include "charslots/CharSlotsApi.h"
#include "shoulders/ShoulderApi.h"
#include "maptransfer/MapTransferExpandApi.h"
#include "compat/rs/rs.h"
#include "compat/hook.h"
#include "Memory.h"
#include "bootlog/BootLog.h"
#include "windowtitle/WindowTitleApi.h"
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
		// Must load beside ijl15.dll — not CWD. Persist already writes there; relative
		// "config.ini" made boot miss soScreenResolution whenever Start-in ≠ client dir.
		std::string configIniPath;
		{
			char dllPath[MAX_PATH] = {};
			if (hModule && GetModuleFileNameA(hModule, dllPath, MAX_PATH)) {
				std::string p(dllPath);
				const size_t slash = p.find_last_of("\\/");
				if (slash != std::string::npos)
					configIniPath = p.substr(0, slash + 1) + "config.ini";
			}
			if (configIniPath.empty())
				configIniPath = "config.ini";
		}
		INIReader reader(configIniPath);
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
			// Default true: hover-lazy companion tip (SetToolTip_String2); not enter-path.
			Client::enableGrowthCompanionTip =
					reader.GetBoolean("optional", "enableGrowthCompanion",
							reader.GetBoolean("optional", "enableEquipGrowthTip", true));
			Client::enableNativeAdventurerDice =
					reader.GetBoolean("optional", "enableNativeAdventurerDice", true);
			Client::expandItem = reader.GetBoolean("optional", "expandItem", true);
			Client::expandItemUI = reader.GetBoolean("optional", "expandItemUI", true);
			Client::expandItemSlotLimits = reader.GetBoolean("optional", "expandItemSlotLimits", true);
			Client::expandItemSort2 = reader.GetBoolean("optional", "expandItemSort2", true);
			// width/height = login + char-select. soScreenResolution = field after enter.
			// Omit soScreenResolution → follow-login (field stays at login dims).
			rs_tier = reader.GetInteger("general", "soScreenResolution", -1);
			rs_field_follow_login = (rs_tier < 0);
			BootLog("config.ini loaded from %s soScreenResolution=%d follow_login=%d",
					configIniPath.c_str(), rs_tier, rs_field_follow_login ? 1 : 0);
		} else {
			BootLog("config.ini ParseError=%d path=%s — using defaults",
					reader.ParseError(), configIniPath.c_str());
		}
		// Belt-and-suspenders: Win32 profile API on the same absolute path (covers
		// INIReader miss / encoding edge cases for the persisted field tier).
		rs_sync_tier_from_ini();

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
		WindowTitle::InstallEarlyUpdate();
		Hook_lpfn_NextLevel(true);
		HookSaveGlobal(true);
		HookSelectCharMacFix(true);

		Client::UpdateGameStartup();

		// Custom.wz SysOpt stretch (fail-soft). Needed so resolution combo + OK/Cancel
		// at y=338/372 sit on the stretched backgrnd (vanilla dialog clips them).
		rs_resman_init();
		if (rs_tier < 0) {
			rs_tier = rs_tier_from_dims(Client::m_nGameWidth, Client::m_nGameHeight);
			rs_field_follow_login = true;
		}
		if (rs_tier > RS_TIER_MAX) rs_tier = 0;

		std::cout << "Applying resolution " << Client::m_nGameWidth << "x" << Client::m_nGameHeight << std::endl;
		Client::UpdateResolution();
		// RS set_stage / follow-login use rs_login_* (defaults 800x600). Without sync,
		// login restore re-applies 800 UpdateResolution inside an HD window → empty
		// wooden-board mid/bottom planks and squeezed ID/PW (see rs.cpp restore path).
		rs_set_login_dims(Client::m_nGameWidth, Client::m_nGameHeight);
		rs_width = Client::m_nGameWidth;
		rs_height = Client::m_nGameHeight;
		rs_adjust_cy = (rs_height > 600) ? (rs_height - 600) / 2 : 0;
		Client::FixMouseWheel();
		Client::Chinese();
		Client::LongQuickSlot();
		Client::FixDateFormat();
		Client::FixItemType();
		Client::JumpCap();
		Client::FixChatPosHook();
		Client::NoPassword();
		BootLogStage("pre MoreHook");
		Client::MoreHook();
		BootLogStage("post MoreHook");
		Client::DeleteChar();

		// Early patches that are safe at DllMain (no late UI).
		// Miles AIL_quick_startup ExitProcess(0) stub + boot stage trace (same as Client_1).
		AttachLoadTrace();
		HigherStorageList::ApplyPatches();
		HigherShopList::ApplyPatches();
		AttachNewCharDiceMod();
		AttachMaxHpMpMod();
		// Must match PacketCreator writeShort(level)+writeLong(exp); without this,
		// CHARLIST mis-aligns → garbage avatar UOLs → 0x80030002 on channel select.
		// Soft-fail: Level300 must not take down logo/boot (SEH + Fuse recurse guard).
		AttachLevel300ModSafe();
		AttachMesoUncapMod();
		AttachGameDataGuard();
		// SkillTipCrashGuards OFF 2026-09-02: same commit as broken MapEnterNullGuard;
		// keep vanilla until enter-game is stable again.
		// AttachSkillTipCrashGuards();
		AttachPersonalShopMod();
		if (Client::quickLogin) {
			AttachQuickLoginMod();
		}
		if (Client::allowCashTrade) {
			AttachAllowCashTradeMod();
		}
		AttachCharSlotsMod();
		AttachShoulderSlotsFix();
		// MapTransfer expand TEMPORARILY OFF (2026-08-30): select-char -> set_stage AV
		// at BeiDou.exe+0x6292F9 (null+0x3D) with preceding hr=0x800401F8/0x80004003.
		// Hook sites IDA-verified OK on S9 EXE, but expand still correlated with channel-enter
		// crash; keep vanilla 5/10 until decode/UI path is proven. Must stay in sync with
		// GameConstants.TROCK_MAP_SIZE / VIP_TROCK_MAP_SIZE on the server.
		// AttachMapTransferExpandMod();

		Client::ExpandItem();
		try {
			EquipAddon::InstallLoginPersistEarly();
		} catch (...) {
		}

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
