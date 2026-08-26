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
#include "bootlog/CrashDiag.h"
#include "compat/LazyCompatInit.h"
#include "compat/rs/rs.h"
#include "shoulders/ShoulderApi.h"
#include "equipaddon/EquipAddonApi.h"
#include "gamedata/GameDataGuardApi.h"
#include "maxhpmp/MaxHpMpApi.h"
#include "level300/Level300Api.h"
#include "mesouncap/MesoUncapApi.h"
#include "highershoplist/HigherShopListApi.h"
#include "personalshop/PersonalShopApi.h"
#include "quicklogin/QuickLoginApi.h"
#include "charslots/CharSlotsApi.h"
#include "airskill/AirSkillApi.h"
#include "maptransfer/MapTransferExpandApi.h"
#include "statdetail/StatDetailExtApi.h"
#pragma comment(lib, "ws2_32.lib")

#ifndef BISECT_DISABLE_LATE_UI_HOOKS
#define BISECT_DISABLE_LATE_UI_HOOKS 0
#endif

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
			Client::enableAirSkill = reader.GetBoolean("optional", "enableAirSkill", true);
			Client::enableEquipCategoryOverride =
					reader.GetBoolean("optional", "enableEquipCategoryOverride", false);
			Client::enableFusionAnvilTooltipHooks =
					reader.GetBoolean("optional", "enableFusionAnvilTooltipHooks", false);
			// MapleRoot Full: SetResManParam(..., retain, -1) + optional SweepCache/CField flush.
			Client::enableResManTimeout = reader.GetBoolean("optional", "enableResManTimeout", false);
			Client::resManRetainMs = reader.GetInteger("optional", "resManRetainMs", 60000);
			if (Client::resManRetainMs < 0) {
				Client::resManRetainMs = 60000;
			}
			Client::enableResManFlush = reader.GetBoolean("optional", "enableResManFlush", true);
			Client::resManSweepMs = reader.GetInteger("optional", "resManSweepMs", 60000);
			if (Client::resManSweepMs < 1000) {
				Client::resManSweepMs = 1000;
			}
			Client::resManLowVaFlushMb = reader.GetInteger("optional", "resManLowVaFlushMb", 96);
			if (Client::resManLowVaFlushMb < 0) {
				Client::resManLowVaFlushMb = 0;
			}
			// Prefer enableGrowthCompanion; also accept legacy enableEquipGrowthTip.
			Client::enableGrowthCompanionTip =
					reader.GetBoolean("optional", "enableGrowthCompanion",
							reader.GetBoolean("optional", "enableEquipGrowthTip", true));
			// width/height = login + character-select. soScreenResolution = field after enter.
			// Relogin restores login size for the UI only; never treat a missing Global.opt
			// value as “wipe the saved field tier”. Omit soScreenResolution only to keep
			// the field at login dims.
			rs_tier = reader.GetInteger("general", "soScreenResolution", -1);
			rs_field_follow_login = (rs_tier < 0);
		}

		// 诊断日志：VEH 先挂上；Detour 在其它 Hook 之后。不改刷新率/显卡相关逻辑。
		CrashDiag_Init(hModule);

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
		HookHpMpAlertRecv(true);
		HookSelectCharMacFix(true);
		//Hook_get_unknown(true);
		//Hook_get_resource_object(true); //helper function hooks  //ty teto for helping me get started
		//Hook_com_ptr_t_IWzProperty__ctor(true);
		//Hook_com_ptr_t_IWzProperty__dtor(true);

		Client::UpdateGameStartup();

		// Custom.wz SysOpt stretch mount (fail-soft). Must run before resolution apply.
		rs_resman_init();
		if (rs_tier < 0) {
			rs_tier = rs_tier_from_dims(Client::m_nGameWidth, Client::m_nGameHeight);
			rs_field_follow_login = true;
		}
		if (rs_tier > RS_TIER_MAX) rs_tier = 0;

		std::cout << "Applying resolution " << Client::m_nGameWidth << "x" << Client::m_nGameHeight << std::endl;
		Client::UpdateResolution();
		// Critical: rs_on_enter_field follow-login uses rs_login_* (defaults 800x600).
		// Without this, field enter re-applies 800 layout inside an HD window → split HUD.
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
		Client::MoreHook();
		try {
			// Apply Stat width / BtDetail before first CUIStat create (login-safe).
			StatDetailExt::EnsureHooks();
		} catch (...) {
		}
		// BossHP / WorldMap deferred to LazyCompat FieldInit (owns CField::Init).
		// 慎重：RefreshRate / 写 IWzGr2D+0x84 / 显卡相关改动曾导致 E_FAIL，默认保持关闭。
		Client::DeleteChar();
		try {
			AttachGameDataGuard();
		} catch (...) {
		}
		AttachShoulderSlotsFix();
		// Login inventory apply (getCharInfo) happens BEFORE first CField.
		// Install Addon LoginAllow + Apply caves here so −52…−62 survive relog.
		// Get/Set Detours + UI stay on FieldInit EnsureHooks (avoid #35/#38).
		try {
			EquipAddon::InstallLoginPersistEarly();
		} catch (...) {
		}
		// Packet length parity with PacketCreator.addCharStats / addCharacterInfo:
		//   MaxHpMp: writeInt HP/MP (+ Fuse_long CS==0 for Level300 EXP FakeTear)
		//   Level300: writeShort(level) + writeLong(exp)
		//   MesoUncap: writeLong(meso)
		// Missing any → CharacterData desync → error 38 (EOF) on channel enter.
		try {
			AttachMaxHpMpMod();
		} catch (...) {
		}
		try {
			AttachLevel300Mod();
		} catch (...) {
		}
		try {
			AttachMesoUncapMod();
		} catch (...) {
		}
		// MapTransfer expand: CharacterData Decode reads TROCK_MAP_SIZE / VIP_TROCK_MAP_SIZE ints.
		// Must match PacketCreator.addTeleportInfo or channel enter desyncs (error 38).
		try {
			AttachMapTransferExpandMod();
		} catch (...) {
		}
		try {
			HigherShopList::ApplyPatches();
		} catch (...) {
		}
		try {
			AttachPersonalShopMod();
		} catch (...) {
		}
		try {
			CharSlots::ApplyPatches();
		} catch (...) {
		}
		try {
			if (Client::quickLogin) {
				AttachQuickLoginMod();
			}
		} catch (...) {
		}
		try {
			if (Client::allowCashTrade) {
				AttachAllowCashTradeMod();
			}
		} catch (...) {
		}
		try {
			if (Client::enableAirSkill) {
				AttachAirSkillMod();
			}
		} catch (...) {
		}
		LazyCompatInit::InstallBootstrapHook();
		rs_register();
		CrashDiag_AttachHooks();
		{
			INIReader dbgReader("config.ini");
			if (dbgReader.ParseError() == 0) {
				Client::disablePacketHook = dbgReader.GetBoolean("debug", "disablePacketHook", false);
				Client::disableBossHP = dbgReader.GetBoolean("debug", "disableBossHP", false);
				Client::disableWorldMap = dbgReader.GetBoolean("debug", "disableWorldMap", false);
				if (dbgReader.GetBoolean("debug", "CrashDiagTestAV", false)) {
					CrashDiag_DebugTriggerAccessViolation();
				}
				if (dbgReader.GetBoolean("debug", "CrashDiagTestEPointer", false)) {
					CrashDiag_DebugTriggerEPointer();
				}
			}
		}
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





