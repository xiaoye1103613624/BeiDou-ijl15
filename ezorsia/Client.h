#pragma once

#include <string>

class Client
{
public:
	static void UpdateGameStartup();
	static void EnableNewIGCipher();
	static void UpdateResolution();
	static void UpdateLogin();
	static void FixMouseWheel();
	static void Chinese();
	static void LongQuickSlot();
	static void FixDateFormat();
	static void FixItemType();
	static void JumpCap();
	static void FixChatPosHook();
	static void NoPassword();
	static void MoreHook();
	static void WorldMap();
	static void RefreshRate(); 
	static void DeleteChar();
	static const int m_nIGCipherHash = 0XC65053F2;
	static int m_nGameHeight;
	static int m_nGameWidth;
	static int MsgAmount;
	static bool CustomLoginFrame;
	static bool WindowedMode;
	static bool RemoveLogos;
	static int setDamageCap;
	static int setMAtkCap;
	static int setAccCap;
	static int setAvdCap;
	static double setAtkOutCap;
	static bool useTubi;
	static bool bigLoginFrame;
	static bool SwitchChinese;
	static bool debug;
	static bool noPassword;
	static bool disablePacketHook;
	static bool disableBossHP;
	static bool disableWorldMap;
	/** Growth companion tip beside equip tip (config optional.enableGrowthCompanion). */
	static bool enableGrowthCompanionTip;
	/** Override Si/Pocket equip category labels in tooltip (unsafe on some paths — default off). */
	static bool enableEquipCategoryOverride;
	/** FusionAnvil equip tooltip hooks (PrintValue breakdown / transmog line). Default off — DLL ZXString AV. */
	static bool enableFusionAnvilTooltipHooks;
	/** After world select, auto-focus channel window (Enter -> ch1). */
	static bool quickLogin;
	/** NOP cash-item trade checks so cash gear can enter trade inventory. */
	static bool allowCashTrade;
	/** Allow casting most skills while airborne. */
	static bool enableAirSkill;
	/** Archer hyper jobs/skills (313/323 DoActiveSkill routing). */
	static bool enableHyperSkill;
	/** Promote Data/Skill/_full hyper books on char enter / job change. */
	static bool autoLoadHyperSkillBooks;
	/** Revert previous hyper book to Data/Skill/_slim when switching jobs. */
	static bool autoLoadHyperRevertSlim;
	/** MapleRoot-style ResMan AUTO_SERIALIZE retain (ms). Default off — stock uses -1. */
	static bool enableResManTimeout;
	static int resManRetainMs;
	/** When timeout on: patch SweepCache delays + CField FlushCachedObjects(0) (MR flushcache). */
	static bool enableResManFlush;
	/** SweepCache outer/object-age ms (stock 60000 / 300000). Default 10000. */
	static int resManSweepMs;
	/** FlushCachedObjects(0) when maxFree (MB) drops below this (0=off). Default 128. */
	static int resManLowVaFlushMb;
	static bool climbSpeedAuto;
	static float climbSpeed;
	static int speedMovementCap;
	static unsigned char imeType;
	static DWORD jumpCap;
	static std::string ServerIP_AddressFromINI;
	static int serverIP_Port;
	static bool talkRepeat;
	static int talkTime;
};