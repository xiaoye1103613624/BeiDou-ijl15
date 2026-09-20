#include "../stdafx.h"
#include "DeferredBootPatches.h"
#include "BootLog.h"
#include "../Client.h"
#include "../Memory.h"
#include "../compat/LazyCompatInit.h"
#include "../compat/rs/rs.h"
#include "../equipaddon/EquipAddonApi.h"
#include "../higherstoragelist/HigherStorageListApi.h"
#include "../highershoplist/HigherShopListApi.h"
#include "../newchardice/NewCharDiceApi.h"
#include "../maxhpmp/MaxHpMpApi.h"
#include "../level300/Level300Api.h"
#include "../mesouncap/MesoUncapApi.h"
#include "../gamedata/GameDataGuardApi.h"
#include "../gamedata/EnterGameCanvasNullGuardsApi.h"
#include "../gamedata/SkillTipCrashGuardsApi.h"
#include "../personalshop/PersonalShopApi.h"
#include "../quicklogin/QuickLoginApi.h"
#include "../charslots/CharSlotsApi.h"
#include "../shoulders/ShoulderApi.h"
#include "../userinfomount/UserInfoMountApi.h"

static bool g_wantVirtuProtect = true;

void DeferredBoot_SetWantVirtuProtect(bool want) {
	g_wantVirtuProtect = want;
}

bool DeferredBoot_GetWantVirtuProtect() {
	return g_wantVirtuProtect;
}

static void AttachLevel300ModSafe() {
	__try {
		AttachLevel300Mod();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		BootLog("*** AttachLevel300Mod SEH 0x%08X — continuing boot without Level300",
				GetExceptionCode());
	}
}

void ApplyDeferredBootPatches() {
	static bool applied = false;
	if (applied)
		return;
	applied = true;

	// MUST VirtualProtect here: UpdateGameStartup writes .rdata/.rsrc
	// (e.g. FillBytes 0xC08459 asInvoker). With UseVirtuProtect=false that
	// memset AVs (EAX=0x20202020, target=0xC08459) → DllMain historically
	// returned failure → STATUS_DLL_INIT_FAILED (0xC0000142). Config want is
	// only restored after these PE image writes.
	Memory::UseVirtuProtect = true;
	BootLog("post-Gr2D UseVirtuProtect forced=1 (config want=%d)", g_wantVirtuProtect ? 1 : 0);
	BootLogStage("post-Gr2D ApplyDeferredBootPatches BEGIN");

	// Was still running from DllMain — many WriteInt/CodeCave into .text before
	// FindScreenMode when UseVirtuProtect=true → Gr2D E_FAIL.
	Client::UpdateGameStartup();

	Client::UpdateResolution();
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

	HigherStorageList::ApplyPatches();
	HigherShopList::ApplyPatches();
	AttachNewCharDiceMod();
	AttachMaxHpMpMod();
	AttachLevel300ModSafe();
	AttachMesoUncapMod();
	AttachGameDataGuard();
	AttachEnterGameCanvasNullGuards();
	AttachSkillTipCrashGuards();
	AttachPersonalShopMod();
	if (Client::quickLogin) {
		AttachQuickLoginMod();
	}
	if (Client::allowCashTrade) {
		AttachAllowCashTradeMod();
	}
	AttachCharSlotsMod();
	AttachShoulderSlotsFix();
	UserInfoMount::ApplyPatches();

	Client::ExpandItem();
	try {
		EquipAddon::InstallLoginPersistEarly();
	} catch (...) {
	}

	// Bootstrap may already be installed from DllMain — idempotent.
	LazyCompatInit::InstallBootstrapHook();
	rs_register();

	BootLogStage("post-Gr2D ApplyDeferredBootPatches END");
	// Leave UseVirtuProtect=true: later field/UI Memory::Write* still need it.
	// config UseVirtuProtect=false only applies to pre-Gr2D DllMain (forced off).
}
