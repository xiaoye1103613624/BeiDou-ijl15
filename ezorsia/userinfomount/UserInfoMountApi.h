#pragma once

// CUIUserInfo TAMING MOB page (state 2):
// - BP20 (shoulder 115) must not land in mount equip list (+0x6D4)
// - Mount preview must draw even without a suited saddle
//
// Stamp: USERINFO_MOUNT_BP20_PREVIEW_20260917

void AttachUserInfoMountFix();

namespace UserInfoMount {
void ApplyPatches();
const char* GetStamp();
}
