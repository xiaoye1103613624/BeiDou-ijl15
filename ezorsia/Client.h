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
	static void ExpandItem();
	static void ExpandItemUI();
	static void ExpandItemSlotLimits();
	static void ExpandItemSort2();
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
	static bool climbSpeedAuto;
	static float climbSpeed;
	static int speedMovementCap;
	static unsigned char imeType;
	static DWORD jumpCap;
	static std::string ServerIP_AddressFromINI;
	static int serverIP_Port;
	static bool talkRepeat;
	static int talkTime;
	static bool quickLogin;
	static bool allowCashTrade;
	/** 装备成长属性旁挂 tip；默认 true，仅悬停时懒创建，启动路径零 tip。 */
	static bool enableGrowthCompanionTip;
	/** 冒险家创建角色投骰子（封包末尾追加 STR/DEX/INT/LUK）；默认 true */
	static bool enableNativeAdventurerDice;
	/** 道具 tip 底部显示「ID: n」；默认 true（optional.showItemTipId） */
	static bool showItemTipId;
	static bool expandItem;
	static bool expandItemUI;
	static bool expandItemSlotLimits;
	static bool expandItemSort2;
};