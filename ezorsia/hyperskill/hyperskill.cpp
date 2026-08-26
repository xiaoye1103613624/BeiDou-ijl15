// hyperskill.cpp — MapleRoot-style hyper skills (all lines).
// IDA-confirmed VAs (BeiDou.exe / GMS v083):
//   get_job_name              0x004A77EF  (sub_4A77EF)
//   CUserLocal::DoActiveSkill 0x00966F7A  cave @0x0096792A -> continue @0x0096793B
//   meleeAttack               0x009690AE
//   shootAttack               0x009690E9
//   magicAttack               0x0096928B
//   statChange (buff)         0x00969284
//   is_attack_area_set_by_data 0x007666CB  (sub_7666CB)
//   is_ltrb_skill (shoot rect) 0x00766722  (sub_766722; MapleRoot Activeskill.h ltrb)
//   remove_bullet_skill       0x007667EE  (sub_7667EE)
//   CUIStat job-name gate     0x008C5AFC (jnz 6B; nop -> always get_job_name)
//   (cosmetic REMOVED) case25 orange: 98B70C / 8C4944 / 8AA6CB — keep vanilla fonts
//   CUISkill render H         0x008AA86F+1 21→73 (opcode@8AA86F: push 0x121→0x173)
//   CUISkill scrollbar        0x008AACD5+1 9B→F0 (push 0x9B→0xF0)
//   CUISkill macro Y          0x008AAE23+1 09→59 (opcode@8AAE23: push 0x109→0x159)
//   CUISkill level-check skip 0x008AD01A -> jmp 0x008AD227
//   DoActiveSkill "not yet"   0x00967707 PatchNop 12
//   DoActiveSkill notice nop 0x00967707 (MapleRoot; no 0x9676F0 EB)
//   SkillEntryCmp (A08E05)    hook: hyper books never gray-lock via req list
//   SuperBeginner wear-any    0x004F2D9B+2 -> 0x07
//   get_job_level (4E8F66)    0x004E8F90+2 -> 0x05 (CUIStat/other UIs; MapleRoot has NO this)
//   CUISkill tab enable max   0x00BE2C68 dd 5 -> 6  ★ root cause of "5th tab can't click"
//   CCtrlTab create W         0x008AAC72+1 -> 0xAE (170→174; fit 6 book tabs in 175px wnd)
//   CCtrlTab tab spacing      0x008AAC97+3 -> 0x1C (34→28; 6*28=168 ≤ tab bar)
//   Skill tip null guards     A60C72/9E/CC2 + 8AD927 Btn invoke (no entry patch; boot-safe)
//   Btn_Array skillexpansion   8AAD3C + 8AD920 → &Btn_Array+12; SetButton n=6 @8AD903/8AD7F8; loop 0x167 @8AADAC
//
// Cosmic / MapleRoot: SET_FIELD does NOT write masterLevel for job%10==3 (sub_4E8F04
// is job%10==2 only). Unlock is skill level>0. Do NOT patch 4E8F04 to read masterLevel
// for hyper — that stores into the master map and trips DoActiveSkill "not yet" fail.
//
// Hyper book tabs (IDA):
//   sub_8AD238 → sub_4A8C4F(+get_job_name) + beginner 0 → roots [0,300,310,311,312,313]
//   sub_8AD2D1 AddTab: enabled iff (tabIndex < dword_BE2C68). Vanilla dd=5 → index5 OFF.
//   CUIStat twin (sub_8B24B1) uses get_job_level() instead of BE2C68.
//   get_job_name hook required so 4A8C4F inserts book 313.
//   Tab bar was 170@34 (=5 tabs); 6 tabs need W+spacing so index5 keeps hit box.

#include "stdafx.h"
#include "HyperSkillApi.h"
#include "Memory.h"
extern DWORD Btn_Array[48];

#include <cstdio>
#include <iostream>

namespace {

constexpr DWORD kDoActiveCave = 0x0096792A;
constexpr DWORD kDoActiveJmpBack = 0x0096793B;
constexpr DWORD kMeleeAttack = 0x009690AE;
constexpr DWORD kShootAttack = 0x009690E9;
constexpr DWORD kMagicAttack = 0x0096928B;
constexpr DWORD kStatChange = 0x00969284;
constexpr DWORD kCombatStep = 0x00969026;
constexpr DWORD kSummonAttack = 0x009689DF;
constexpr DWORD kPrepareAttack = 0x00969229;
constexpr DWORD kDoRecovery = 0x00969217;
constexpr DWORD kDoBoundJump = 0x0096897A;

const unsigned char kOrigCaveBytes[6] = { 0x0F, 0x8F, 0x71, 0x09, 0x00, 0x00 };

int g_meleeAttack = static_cast<int>(kMeleeAttack);
int g_shootAttack = static_cast<int>(kShootAttack);
int g_magicAttack = static_cast<int>(kMagicAttack);
int g_statChange = static_cast<int>(kStatChange);
int g_combatStep = static_cast<int>(kCombatStep);
int g_summonAttack = static_cast<int>(kSummonAttack);
int g_prepareAttack = static_cast<int>(kPrepareAttack);
int g_dorecovery = static_cast<int>(kDoRecovery);
int g_doBoundJump = static_cast<int>(kDoBoundJump);
int g_doActiveJmpBack = static_cast<int>(kDoActiveJmpBack);

static void HyperSkillLog(const char* msg); // defined below

extern "C" void __cdecl HyperBookLoader_PromoteForSkill(int skillId);

bool IsHyperMatchedSkill(int id) {
    switch (id) {
    case 1050:
    case 10001050:
    case 20001050:
    case 1121012:
    case 5121011:
    case 5121012:
    case 5111013:
    case 5121013:
    case 1321011:
    case 3221010:
    case 3221009:
    case 4221009:
    case 7001002:
    case 7001003:
    case 7001001:
    case 7001000:
    case 7001004:
    case 7001006:
    case 1051:
    case 1052:
    case 11121004:
    case 11121014:
    case 11121101:
    case 11121102:
    case 11121203:
    case 1221017:
    case 12121012:
    case 12121002:
    case 12121054:
    case 12121055:
    case 13121001:
    case 13121002:
    case 13121052:
    case 13121008:
    case 13121054:
    case 14111002:
    case 14121007:
    case 14121006:
    case 14121001:
    case 14121003:
    case 4221010:
    case 15121003:
    case 2221009:
    case 15121001:
    case 15121002:
    case 15121052:
    case 2111004:
    case 2211004:
    case 2311005:
    case 12111002:
    case 2121052:
    case 2121054:
    case 2121006:
    case 5221017:
    case 5221018:
    case 5221014:
    case 1321016:
    case 2221011:
    case 5221016:
    case 1321012:
    case 1321013:
    case 3121015:
    case 4121010:
    case 1221014:
    case 11121204:
    case 1054:
    case 20001054:
    case 10001054:
    case 1131105:
    case 1231105:
    case 1331105:
    case 11131105:
    case 21131105:
    case 1131109:
    case 1231109:
    case 1331109:
    case 11131109:
    case 21131109:
    case 1131074:
    case 1231074:
    case 1331074:
    case 11131074:
    case 21131074:
    case 1131075:
    case 1231075:
    case 1331075:
    case 11131075:
    case 21131075:
    case 2131006:
    case 2231006:
    case 2331006:
    case 12131006:
    case 2131067:
    case 2231067:
    case 2331067:
    case 12131067:
    case 2131072:
    case 2231072:
    case 2331072:
    case 12131072:
    case 2131079:
    case 2231079:
    case 2331079:
    case 12131079:
    case 3131003:
    case 3231003:
    case 13131003:
    case 3131012:
    case 3231012:
    case 13131012:
    case 3131056:
    case 3231056:
    case 13131056:
    case 3131007:
    case 3231007:
    case 13131007:
    case 4131031:
    case 4231031:
    case 14131031:
    case 4131042:
    case 4231042:
    case 14131042:
    case 4131073:
    case 4231073:
    case 14131073:
    case 4131076:
    case 4231076:
    case 14131076:
    case 5131033:
    case 5231033:
    case 15131033:
    case 5131044:
    case 5231044:
    case 15131044:
    case 5131074:
    case 5231074:
    case 15131074:
    case 5131075:
    case 5231075:
    case 15131075:
    case 7101101:
    case 7101102:
    case 7101103:
    case 7101104:
    case 7101105:
    case 7101106:
    case 7101107:
    case 7101108:
        return true;
    default:
        return false;
    }
}
bool IsShootAreaSkill(int id) {
    switch (id) {
    // MapleRoot moreshit.h is_attack_area_set_by_data (+ BeiDou 5th expansions)
    case 13121001:
    case 13121002:
    case 13121052:
    case 13121054:
    case 14121007:
    case 3221010:
    case 3221009:
    case 3201005:
    case 3121015:
    case 5221017:
    case 2101004:
    case 4111005:
    case 14111002:
    case 1131074:
    case 1231074:
    case 1331074:
    case 11131074:
    case 21131074:
    case 1131075:
    case 1231075:
    case 1331075:
    case 11131075:
    case 21131075:
    case 1131105:
    case 1231105:
    case 1331105:
    case 11131105:
    case 21131105:
    case 2131067:
    case 2231067:
    case 2331067:
    case 12131067:
    case 2131072:
    case 2231072:
    case 2331072:
    case 12131072:
    case 2131079:
    case 2231079:
    case 2331079:
    case 12131079:
    case 3131003:
    case 3231003:
    case 13131003:
    case 3131056:
    case 3231056:
    case 13131056:
    case 3131012:
    case 3231012:
    case 13131012:
    case 4131042:
    case 4231042:
    case 14131042:
    case 4131073:
    case 4231073:
    case 14131073:
    case 4131076:
    case 4231076:
    case 14131076:
    case 5131074:
    case 5231074:
    case 15131074:
    case 5131044:
    case 5231044:
    case 15131044:
    case 5131075:
    case 5231075:
    case 15131075:
    case 7101106:
    case 7101107:
    case 7101108:
        return true;
    default:
        return false;
    }
}

bool IsNoBulletSkill(int id) {
    // MapleRoot Activeskill.h remove_bullets
    switch (id) {
    case 14111002:
    case 4111005:
    case 5221016:
    case 5221017:
    case 3121015:
    case 3221009:
    case 3221010:
    case 3131003:
    case 3231003:
    case 13131003:
    case 3131056:
    case 3231056:
    case 13131056:
    case 3131012:
    case 3231012:
    case 13131012:
    case 4131042:
    case 4231042:
    case 14131042:
    case 5131074:
    case 5231074:
    case 15131074:
        return true;
    default:
        return false;
    }
}

// MapleRoot Activeskill.h ltrb @ 0x00766722 — shoot-attack rect flag (≠ is_attack_area).
// Keep list aligned with MR (ranged only); do NOT reuse warrior/mage IsShootArea expansions.
bool IsLtrbSkill(int id) {
    switch (id) {
    case 13121001:
    case 13121002:
    case 13121052:
    case 13121054:
    case 14121007:
    case 3221010:
    case 3221009:
    case 3201005:
    case 3121015:
    case 5221017:
    case 3131003:
    case 3231003:
    case 13131003:
    case 3131056:
    case 3231056:
    case 13131056:
    case 3131012:
    case 3231012:
    case 13131012:
    case 4131042:
    case 4231042:
    case 14131042:
    case 5131074:
    case 5231074:
    case 15131074:
        return true;
    default:
        return false;
    }
}

void RestoreDoActiveCave() {
    Memory::WriteByteArray(kDoActiveCave, const_cast<unsigned char*>(kOrigCaveBytes), 6);
}

void InstallDoActiveCave(void* cave) {
    Memory::CodeCave(cave, kDoActiveCave, 1);
}

void __declspec(naked) HyperDoActiveSkills() {
    __asm {
		push esi
		push esi
		call HyperBookLoader_PromoteForSkill
		add esp, 4
		pop esi
		mov eax, 1050
		cmp esi, eax
		je jumpmove_lbl
		mov eax, 10001050
		cmp esi, eax
		je jumpmove_lbl
		mov eax, 20001050
		cmp esi, eax
		je jumpmove_lbl
		mov eax, 1121012
		cmp esi, eax
		je melee_lbl
		mov eax, 5121011
		cmp esi, eax
		je melee_lbl
		mov eax, 5121012
		cmp esi, eax
		je melee_lbl
		mov eax, 5111013
		cmp esi, eax
		je melee_lbl
		mov eax, 5121013
		cmp esi, eax
		je melee_lbl
		mov eax, 1321011
		cmp esi, eax
		je melee_lbl
		mov eax, 3221009
		cmp esi, eax
		je shoot_lbl
		mov eax, 4221009
		cmp esi, eax
		je melee_lbl
		mov eax, 7001002
		cmp esi, eax
		je melee_lbl
		mov eax, 7001003
		cmp esi, eax
		je melee_lbl
		mov eax, 7001001
		cmp esi, eax
		je melee_lbl
		mov eax, 7001000
		cmp esi, eax
		je melee_lbl
		mov eax, 7001004
		cmp esi, eax
		je melee_lbl
		mov eax, 7001006
		cmp esi, eax
		je buff_lbl
		mov eax, 1051
		cmp esi, eax
		je melee_lbl
		mov eax, 1052
		cmp esi, eax
		je melee_lbl
		mov eax, 2111004
		cmp esi, eax
		je buff_lbl
		mov eax, 2211004
		cmp esi, eax
		je buff_lbl
		mov eax, 2311005
		cmp esi, eax
		je buff_lbl
		mov eax, 11121004
		cmp esi, eax
		je melee_lbl
		mov eax, 11121014
		cmp esi, eax
		je melee_lbl
		mov eax, 11121101
		cmp esi, eax
		je melee_lbl
		mov eax, 11121102
		cmp esi, eax
		je melee_lbl
		mov eax, 11121203
		cmp esi, eax
		je melee_lbl
		mov eax, 12111002
		cmp esi, eax
		je buff_lbl
		mov eax, 12121012
		cmp esi, eax
		je magic_lbl
		mov eax, 12121002
		cmp esi, eax
		je magic_lbl
		mov eax, 12121054
		cmp esi, eax
		je magic_lbl
		mov eax, 12121055
		cmp esi, eax
		je magic_lbl
		mov eax, 13121001
		cmp esi, eax
		je shoot_lbl
		mov eax, 13121002
		cmp esi, eax
		je shoot_lbl
		mov eax, 13121052
		cmp esi, eax
		je melee_lbl
		mov eax, 13121008
		cmp esi, eax
		je buff_lbl
		mov eax, 13121054
		cmp esi, eax
		je melee_lbl
		mov eax, 14111002
		cmp esi, eax
		je melee_lbl
		mov eax, 14121007
		cmp esi, eax
		je melee_lbl
		mov eax, 14121006
		cmp esi,eax
		je melee_lbl
		mov eax, 4121010
		cmp esi, eax
		je melee_lbl
		mov eax, 14121001
		cmp esi, eax
		je shoot_lbl
		mov eax, 14121003
		cmp esi, eax
		je melee_lbl
		mov eax, 15121003
		cmp esi, eax
		je melee_lbl
		mov eax, 15121001
		cmp esi, eax
		je melee_lbl
		mov eax, 15121002
		cmp esi, eax
		je melee_lbl
		mov eax, 15121052
		cmp esi, eax
		je melee_lbl
		mov eax, 2121006
		cmp esi, eax
		je magic_lbl
		mov eax, 2221009
		cmp esi, eax
		je magic_lbl
		mov eax, 2121054
		cmp esi, eax
		je magic_lbl
		mov eax, 2121052
		cmp esi, eax
		je magic_lbl
		mov eax, 3221010
		cmp esi, eax
		je shoot_lbl
		mov eax, 4221010
		cmp esi, eax
		je melee_lbl
		mov eax, 1221017
		cmp esi, eax
		je melee_lbl
		mov eax, 1321016
		cmp esi, eax
		je melee_lbl
		mov eax, 1321012
		cmp esi, eax
		je melee_lbl
		mov eax, 1221014
		cmp esi, eax
		je melee_lbl
		mov eax, 1321013
		cmp esi, eax
		je melee_lbl
		mov eax, 5221014
		cmp esi, eax
		je buff_lbl
		mov eax, 5221018
		cmp esi, eax
		je buff_lbl
		mov eax, 5221017
		cmp esi, eax
		je shoot_lbl
		mov eax, 5221016
		cmp esi, eax
		je shoot_lbl
		mov eax, 2221011
		cmp esi, eax
		je magic_lbl
		mov eax, 3121015
		cmp esi, eax
		je shoot_lbl
		mov eax, 1221014
		cmp esi, eax
		je melee_lbl
		mov eax, 11121204
		cmp esi, eax
		je buff_lbl
		mov eax, 1054
		cmp esi, eax
		je jumpmove_lbl
		mov eax, 10001054
		cmp esi, eax
		je jumpmove_lbl
		mov eax, 20001054
		cmp esi, eax
		je jumpmove_lbl

		//5TH JOB
		//WARRIORS ATTACKS
		mov eax, 1131105
		cmp esi, eax
		je melee_lbl
		mov eax, 1231105
		cmp esi, eax
		je melee_lbl
		mov eax, 1331105
		cmp esi, eax
		je melee_lbl
		mov eax, 11131105
		cmp esi, eax
		je melee_lbl
		mov eax, 21131105
		cmp esi, eax
		je melee_lbl
		mov eax, 1131075
		cmp esi, eax
		je melee_lbl
		mov eax, 1231075
		cmp esi, eax
		je melee_lbl
		mov eax, 1331075
		cmp esi, eax
		je melee_lbl
		mov eax, 11131075
		cmp esi, eax
		je melee_lbl
		mov eax, 21131075
		cmp esi, eax
		je melee_lbl
		mov eax, 1131074
		cmp esi, eax
		je melee_lbl
		mov eax, 1231074
		cmp esi, eax
		je melee_lbl
		mov eax, 1331074
		cmp esi, eax
		je melee_lbl
		mov eax, 11131074
		cmp esi, eax
		je melee_lbl
		mov eax, 21131074
		cmp esi, eax
		je melee_lbl
		//WARRIORS BUFFS
		mov eax, 1131109
		cmp esi, eax
		je buff_lbl
		mov eax, 1231109
		cmp esi, eax
		je buff_lbl
		mov eax, 1331109
		cmp esi, eax
		je buff_lbl
		mov eax, 11131109
		cmp esi, eax
		je buff_lbl
		mov eax, 21131109
		cmp esi, eax
		je buff_lbl
		//Mages Attacks
		mov eax, 2131067
		cmp esi, eax
		je magic_lbl
		mov eax, 2231067
		cmp esi, eax
		je magic_lbl
		mov eax, 2331067
		cmp esi, eax
		je magic_lbl
		mov eax, 12131067
		cmp esi, eax
		je magic_lbl
		mov eax, 2131072
		cmp esi, eax
		je magic_lbl
		mov eax, 2231072
		cmp esi, eax
		je magic_lbl
		mov eax, 2331072
		cmp esi, eax
		je magic_lbl
		mov eax, 12131072
		cmp esi, eax
		je magic_lbl
		mov eax, 2131079
		cmp esi, eax
		je magic_lbl
		mov eax, 2231079
		cmp esi, eax
		je magic_lbl
		mov eax, 2331079
		cmp esi, eax
		je magic_lbl
		mov eax, 12131079
		cmp esi, eax
		je magic_lbl
		//Mages Buffs
		mov eax, 2131006
		cmp esi, eax
		je buff_lbl
		mov eax, 2231006
		cmp esi, eax
		je buff_lbl
		mov eax, 2331006
		cmp esi, eax
		je buff_lbl
		mov eax, 12131006
		cmp esi, eax
		je buff_lbl
		//Archers Attacks
		mov eax, 3131003
		cmp esi, eax
		je shoot_lbl
		mov eax, 3231003
		cmp esi, eax
		je shoot_lbl
		mov eax, 13131003
		cmp esi, eax
		je shoot_lbl
		mov eax, 3131056
		cmp esi, eax
		je shoot_lbl
		mov eax, 3231056
		cmp esi, eax
		je shoot_lbl
		mov eax, 13131056
		cmp esi, eax
		je shoot_lbl
		mov eax, 3131012
		cmp esi, eax
		je shoot_lbl
		mov eax, 3231012
		cmp esi, eax
		je shoot_lbl
		mov eax, 13131012
		cmp esi, eax
		je shoot_lbl
		//Archers Buffs
		mov eax, 3131007
		cmp esi, eax
		je buff_lbl
		mov eax, 3231007
		cmp esi, eax
		je buff_lbl
		mov eax, 13131007
		cmp esi, eax
		je buff_lbl
			//Thieves Attacks
			mov eax, 4131042
			cmp esi, eax
			je shoot_lbl
			mov eax, 4231042
			cmp esi, eax
			je shoot_lbl
			mov eax, 14131042
			cmp esi, eax
			je shoot_lbl
			mov eax, 4131073
			cmp esi, eax
			je melee_lbl
			mov eax, 4231073
			cmp esi, eax
			je melee_lbl
			mov eax, 14131073
			cmp esi, eax
			je melee_lbl
			mov eax, 4131076
			cmp esi, eax
			je melee_lbl
			mov eax, 4231076
			cmp esi, eax
			je melee_lbl
			mov eax, 14131076
			cmp esi, eax
			je melee_lbl
			//Thieves Buffs
			mov eax, 4131031
			cmp esi, eax
			je buff_lbl
			mov eax, 4231031
			cmp esi, eax
			je buff_lbl
			mov eax, 14131031
			cmp esi, eax
			je buff_lbl
			//Pirate Attacks
			mov eax, 5131074
			cmp esi, eax
			je shoot_lbl
			mov eax, 5231074
			cmp esi, eax
			je shoot_lbl
			mov eax, 15131074
			cmp esi, eax
			je shoot_lbl
			mov eax, 5131044
			cmp esi, eax
			je melee_lbl
			mov eax, 5231044
			cmp esi, eax
			je melee_lbl
			mov eax, 15131044
			cmp esi, eax
			je melee_lbl
			mov eax, 5131075
			cmp esi, eax
			je melee_lbl
			mov eax, 5231075
			cmp esi, eax
			je melee_lbl
			mov eax, 15131075
			cmp esi, eax
			je melee_lbl
			//Pirate Buffs
			mov eax, 5131033
			cmp esi, eax
			je buff_lbl
			mov eax, 5231033
			cmp esi, eax
			je buff_lbl
			mov eax, 15131033
			cmp esi, eax
			je buff_lbl
			//Bard buffs
			mov eax, 7101101
			cmp esi, eax
			je buff_lbl
			mov eax, 7101102
			cmp esi, eax
			je buff_lbl
			mov eax, 7101103
			cmp esi, eax
			je buff_lbl
			mov eax, 7101104
			cmp esi, eax
			je buff_lbl
			mov eax, 7101105
			cmp esi, eax
			je buff_lbl
			//Bard Attacks
			mov eax, 7101106
			cmp esi, eax
			je melee_lbl
			mov eax, 7101107
			cmp esi, eax
			je melee_lbl
			mov eax, 7101108
			cmp esi, eax
			je melee_lbl



		mov eax, 2301005 // need this to go back to our original skills from where we codecave
		jmp dword ptr [g_doActiveJmpBack]

		melee_lbl: jmp dword ptr [g_meleeAttack]
		summons_lbl: jmp dword ptr [g_summonAttack]
		prepare_lbl: jmp dword ptr [g_prepareAttack]
		magic_lbl: jmp dword ptr [g_magicAttack]
		buff_lbl: jmp dword ptr [g_statChange]
		combat_lbl: jmp dword ptr [g_combatStep]
		recover_lbl: jmp dword ptr [g_dorecovery]
		shoot_lbl: jmp dword ptr [g_shootAttack]
		jumpmove_lbl: jmp dword ptr [g_doBoundJump]
    }
}

using IsAttackArea_t = int(__cdecl*)(int nSkillID);
IsAttackArea_t g_IsAttackArea = reinterpret_cast<IsAttackArea_t>(0x007666CB);

int __cdecl IsAttackArea_Hook(int nSkillID) {
    if (IsShootAreaSkill(nSkillID)) {
        return 1;
    }
    return g_IsAttackArea(nSkillID);
}

using RemoveBullet_t = int(__cdecl*)(int nSkillID);
RemoveBullet_t g_RemoveBullet = reinterpret_cast<RemoveBullet_t>(0x007667EE);

int __cdecl RemoveBullet_Hook(int nSkillID) {
    if (IsNoBulletSkill(nSkillID)) {
        return 1;
    }
    return g_RemoveBullet(nSkillID);
}

using IsLtrb_t = int(__cdecl*)(int nSkillID);
IsLtrb_t g_IsLtrb = reinterpret_cast<IsLtrb_t>(0x00766722);

int __cdecl IsLtrb_Hook(int nSkillID) {
    if (IsLtrbSkill(nSkillID)) {
        return 1;
    }
    return g_IsLtrb(nSkillID);
}

using GetJobName_t = const char*(__cdecl*)(int nJob);
GetJobName_t g_GetJobName = reinterpret_cast<GetJobName_t>(0x004A77EF);

static const char kJob700[] = "\xb3\xac\xbc\xb6\xb3\xf5\xd0\xc4\xd5\xdf";
static const char kJob113[] = "\xcf\xc8\xf7\xb9";
static const char kJob123[] = "\xca\xed\xd7\xef";
static const char kJob133[] = "\xba\xda\xb0\xb5\xd4\xa4\xd5\xd7";
static const char kJob1113[] = "\xcf\xa6\xd5\xd5";
static const char kJob2113[] = "\xc9\xf1\xb6\xdc";
static const char kJob213[] = "\xc1\xd2\xd1\xe6\xb4\xf3\xca\xa6";
static const char kJob223[] = "\xba\xae\xb1\xf9\xb4\xf3\xca\xa6";
static const char kJob233[] = "\xc9\xf1\xca\xa5\xb4\xf3\xca\xa6";
static const char kJob1213[] = "\xb3\xe0\xd1\xe6";
static const char kJob313[] = "\xC8\xF1\xD1\xDB";
static const char kJob323[] = "\xD6\xE4\xCA\xB8";
static const char kJob1313[] = "\xbb\xc3\xb7\xe7";
static const char kJob413[] = "\xd0\xe9\xbf\xd5";
static const char kJob423[] = "\xc4\xe6\xd3\xb0";
static const char kJob1413[] = "\xd2\xb9\xd3\xb0";
static const char kJob513[] = "\xb4\xac\xb3\xa4";
static const char kJob523[] = "\xc9\xf1\xc9\xe4";
static const char kJob1513[] = "\xcb\xe9\xb9\xc7";
static const char kJob710[] = "\xba\xcf\xb3\xc9\xb4\xf3\xca\xa6";

const char* __cdecl GetJobName_Hook(int nJob) {
    switch (nJob) {
    case 700: return kJob700;
    case 113: return kJob113;
    case 123: return kJob123;
    case 133: return kJob133;
    case 1113: return kJob1113;
    case 2113: return kJob2113;
    case 213: return kJob213;
    case 223: return kJob223;
    case 233: return kJob233;
    case 1213: return kJob1213;
    case 313: return kJob313;
    case 323: return kJob323;
    case 1313: return kJob1313;
    case 413: return kJob413;
    case 423: return kJob423;
    case 1413: return kJob1413;
    case 513: return kJob513;
    case 523: return kJob523;
    case 1513: return kJob1513;
    case 710: return kJob710;
    default: return g_GetJobName(nJob);
    }
}

// IDA: CUISkill draw uses SkillEntryCmp (sub_A08E05). Return 0 => iconDisabled (gray).
// Hyper slim Skill/*.img can leave a non-null req list at skill+0x50; vanilla then
// gray-locks even when SET_FIELD already applied skillLevel>=1 (Cosmic has no masterLevel).
// IMPORTANT: only unlock *known hyper IDs* — blanket job%10==3 forced -1 on every row
// while K-open loads fat books → long UI stalls / null Btn_Array paths (2026-08-24).
using SkillEntryCmp_t = int(__thiscall*)(void* this_, unsigned int* skillEntry);
SkillEntryCmp_t g_SkillEntryCmp = reinterpret_cast<SkillEntryCmp_t>(0x00A08E05);

int __fastcall HyperSkillEntryCmp_Hook(void* this_, void* /*edx*/, unsigned int* skillEntry) {
    if (skillEntry) {
        const int id = static_cast<int>(*skillEntry);
        if (IsHyperMatchedSkill(id)) {
            return -1; // unlocked for CUISkill icon path (same as maxed)
        }
    }
    return g_SkillEntryCmp(this_, skillEntry);
}

static void HyperSkillLog(const char* msg) {
    std::cout << msg << std::endl;
    FILE* f = nullptr;
    // Client cwd → BeiDou-Client\beidou-hyperskill.log (and legacy hyperskill_attach.log).
    if (fopen_s(&f, "beidou-hyperskill.log", "a") == 0 && f) {
        fputs(msg, f);
        fputc('\n', f);
        fclose(f);
    }
    if (fopen_s(&f, "hyperskill_attach.log", "a") == 0 && f) {
        fputs(msg, f);
        fputc('\n', f);
        fclose(f);
    }
}

static bool PatchByteChecked(DWORD addr, unsigned char want, const char* label) {
    Memory::WriteByte(addr, want);
    const unsigned char got = *reinterpret_cast<volatile unsigned char*>(addr);
    char buf[160];
    sprintf_s(buf, "[hyperskill] %s @ %08X => wrote %02X read %02X %s",
              label, addr, want, got, got == want ? "OK" : "FAIL");
    HyperSkillLog(buf);
    return got == want;
}

static bool PatchIntChecked(DWORD addr, unsigned int want, const char* label) {
    Memory::WriteInt(addr, want);
    const unsigned int got = *reinterpret_cast<volatile unsigned int*>(addr);
    char buf[160];
    sprintf_s(buf, "[hyperskill] %s @ %08X => wrote %08X read %08X %s",
              label, addr, want, got, got == want ? "OK" : "FAIL");
    HyperSkillLog(buf);
    return got == want;
}

// Shared CRT _memcpy @0xA60C00 used by skill tooltip string format.
// Missing String/Skill.img fields leave ESI==0 → AV at mov al,[esi].
// Do NOT patch entry A60C05 — shared memcpy; entry cave caused StringPool AV @41672C
// (boot, 2026-08-24).
// Do NOT patch A60C33 (rep movsd): also shared memcpy; edx jumptable resume is tip-only.
// Guarding it (2026-08-26) made boot jump to garbage EIP=0x696B7372 before Gr2D load.
// Guard only the tip-format byte-copy sites C72/9E/CC2.
static char g_EmptySkillTipStr[1] = {'\0'};
constexpr DWORD kTipResumeC72 = 0x00A60C79;
constexpr DWORD kTipResume9E = 0x00A60CA5;
constexpr DWORD kTipResumeCC2 = 0x00A60CC7;

__declspec(naked) void SkillTipStrCopy_GuardC72() {
    __asm {
        test esi, esi
        jnz L_ok
        lea esi, g_EmptySkillTipStr
    L_ok:
        mov al, byte ptr [esi]
        mov byte ptr [edi], al
        mov al, byte ptr [esi + 1]
        jmp kTipResumeC72
    }
}

__declspec(naked) void SkillTipStrCopy_Guard9E() {
    __asm {
        test esi, esi
        jnz L_ok
        lea esi, g_EmptySkillTipStr
    L_ok:
        mov al, byte ptr [esi]
        mov byte ptr [edi], al
        mov al, byte ptr [esi + 1]
        jmp kTipResume9E
    }
}

__declspec(naked) void SkillTipStrCopy_GuardCC2() {
    __asm {
        test esi, esi
        jnz L_ok
        lea esi, g_EmptySkillTipStr
    L_ok:
        mov al, byte ptr [esi]
        mov byte ptr [edi], al
        inc esi
        jmp kTipResumeCC2
    }
}

// CUISkill row button dispatch @0x8AD918 (SetButton slot invoke):
//   8AD920 lea esi,[ecx+eax*8+5C4h]   ; 7 bytes
//   8AD927 mov eax,[esi+4]           ← CodeCave entry (stock 8B 46 04)
//   8AD92A lea ecx,[eax+4]
//   8AD92D mov eax,[ecx]
//   8AD92F call [eax+24h]            → AV / EIP=0 when sparse book leaves bad slot
// Do NOT use 8AD928: that is mid-imm of mov (ExpectBytes always mismatches → SKIP).
// L_skip must drop the pushed arg before pop esi, else +/- / double-click appear dead.
__declspec(naked) void BtnArrayInvoke_Guard() {
    __asm {
        // esi must be a heap Btn slot — code/null ptrs (e.g. 0x008ADxxx) read garbage [esi+4] -> EIP=0.
        cmp esi, 0x01000000
        jb L_skip
        cmp dword ptr [esi + 4], 0
        je L_skip
        mov eax, dword ptr [esi + 4]
        cmp eax, 0x01000000
        jb L_skip
        lea ecx, [eax + 4]
        mov eax, dword ptr [ecx]
        test eax, eax
        jz L_skip
        // First callee is stdcall (pops the arg pushed at 8AD91C). Do NOT add esp after it.
        call dword ptr [eax + 0x24]
        mov esi, dword ptr [esi + 4]
        push dword ptr [esp + 0x10]
        mov eax, dword ptr [esi + 4]
        test eax, eax
        jz L_drop_push
        lea ecx, [esi + 4]
        mov eax, dword ptr [ecx]
        test eax, eax
        jz L_drop_push
        // Second callee is also stdcall — it pops the push above. Stock has no add esp.
        call dword ptr [eax + 0x1C]
        pop esi
        ret 0x0C
    L_drop_push:
        // Pushed arg_8 but skipped second call — drop it, then restore esi.
        add esp, 4
        pop esi
        ret 0x0C
    L_skip:
        // 8AD91C push never consumed (no first call) — drop it.
        add esp, 4
        pop esi
        ret 0x0C
    }
}

static bool ExpectBytes(DWORD va, const unsigned char* expected, size_t n) {
    __try {
        const auto* live = reinterpret_cast<const unsigned char*>(va);
        for (size_t i = 0; i < n; ++i) {
            if (live[i] != expected[i]) {
                return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void ApplySkillTipCrashGuards() {
    // Tip-format byte-copy null guards only. Never touch A60C00 entry or A60C33 rep movsd
    // (shared memcpy — boot AV 2026-08-26 when A60C33 was guarded).
    Memory::CodeCave(reinterpret_cast<void*>(&SkillTipStrCopy_GuardC72), 0x00A60C72, 0);
    Memory::CodeCave(reinterpret_cast<void*>(&SkillTipStrCopy_Guard9E), 0x00A60C9E, 0);
    Memory::CodeCave(reinterpret_cast<void*>(&SkillTipStrCopy_GuardCC2), 0x00A60CC2, 0);
    HyperSkillLog("[hyperskill] SkillTipStrCopy guards @ A60C72/9E/CC2 installed (A60C33 left vanilla)");
    // Stock @8AD927: 8B 46 04 8D 48 (mov eax,[esi+4]; lea ecx,[eax+4] first 2)
    static const unsigned char kBtnInvokeHead[] = {0x8B, 0x46, 0x04, 0x8D, 0x48};
    constexpr DWORD kBtnInvokeSite = 0x008AD927;
    if (ExpectBytes(kBtnInvokeSite, kBtnInvokeHead, sizeof(kBtnInvokeHead))) {
        Memory::CodeCave(reinterpret_cast<void*>(&BtnArrayInvoke_Guard), kBtnInvokeSite, 0);
        const unsigned char op = *reinterpret_cast<volatile unsigned char*>(kBtnInvokeSite);
        char buf[128];
        sprintf_s(buf, "[hyperskill] BtnArrayInvoke_Guard @ %08X => %02X (expect E9) %s",
                  kBtnInvokeSite, op, op == 0xE9 ? "OK" : "FAIL");
        HyperSkillLog(buf);
    } else {
        HyperSkillLog("[hyperskill] BtnArrayInvoke_Guard SKIP @ 8AD927 (stock bytes mismatch)");
    }
}

// MapleRoot YourStuff.h skillexpansion — redirect CUISkill row ZRefs out of +0x5C4 object
// storage into plugin Btn_Array[48] before raising SetButton loop to 6 (IDA 2026-08-26).
static void ApplyBtnArraySkillexpansion() {
    static const unsigned char kOnCreateLea[] = {0x8D, 0x83, 0xC4, 0x05, 0x00, 0x00};
    static const unsigned char kSetBtnLea[] = {0x8D, 0xB4, 0xC1, 0xC4, 0x05, 0x00, 0x00};
    constexpr DWORD kOnCreateSite = 0x008AAD3C;
    constexpr DWORD kSetBtnLeaSite = 0x008AD920;
    const DWORD btnBase = reinterpret_cast<DWORD>(&Btn_Array) + 12;

    PatchByteChecked(0x008AADAC + 3, 0x67, "row Y loop end 117->167");
    PatchByteChecked(0x008AD903 + 2, 0x06, "SetButton loop cmp 4->6");
    PatchByteChecked(0x008AD7F8 + 2, 0x06, "SetButton init cmp 4->6");

    if (ExpectBytes(kOnCreateSite, kOnCreateLea, sizeof(kOnCreateLea))) {
        Memory::WriteByte(kOnCreateSite + 1, 0x05); // lea eax, [imm32]
        Memory::WriteInt(kOnCreateSite + 2, btnBase);
        char buf[160];
        sprintf_s(buf, "[hyperskill] Btn_Array OnCreate lea @ %08X => %08X %s",
                  kOnCreateSite, btnBase,
                  *reinterpret_cast<volatile DWORD*>(kOnCreateSite + 2) == btnBase ? "OK" : "FAIL");
        HyperSkillLog(buf);
    } else {
        HyperSkillLog("[hyperskill] Btn_Array OnCreate SKIP @ 8AAD3C (stock bytes mismatch)");
    }

    if (ExpectBytes(kSetBtnLeaSite, kSetBtnLea, sizeof(kSetBtnLea))) {
        Memory::WriteByte(kSetBtnLeaSite + 1, 0x34); // lea esi, [eax*8+disp32]
        Memory::WriteByte(kSetBtnLeaSite + 2, 0xC5);
        Memory::WriteInt(kSetBtnLeaSite + 3, btnBase);
        char buf[160];
        sprintf_s(buf, "[hyperskill] Btn_Array SetButton lea @ %08X => %08X %s",
                  kSetBtnLeaSite, btnBase,
                  *reinterpret_cast<volatile DWORD*>(kSetBtnLeaSite + 3) == btnBase ? "OK" : "FAIL");
        HyperSkillLog(buf);
    } else {
        HyperSkillLog("[hyperskill] Btn_Array SetButton SKIP @ 8AD920 (stock bytes mismatch)");
    }
}

static void ApplyHyperSkillUiPatches() {
    // ★ CUISkill book-tab enable ceiling (IDA: sub_8AD2D1 cmp edi, dword_BE2C68).
    // Vanilla .data dd=5 → hyper root index 5 stays disabled (visible disabled art, no click).
    // NOT s_aptKeyPos (0xBE27E0). Only xref is CUISkill tab AddItem enable flag.
    PatchIntChecked(0x00BE2C68, 6, "CUISkill tabEnableMax BE2C68");

    // MapleRoot YourStuff.h skillexpansion — taller skill window for modified WZ.
    // Live EXE (file VA): push imm32 opcode is AT the cited VA; low imm byte is VA+1.
    //   8AA86F: 68 21 01 00 00 = push 0x121 → patch +1 21→73 ⇒ push 0x173
    //   8AAE23: 68 09 01 00 00 = push 0x109 → patch +1 09→59 ⇒ push 0x159
    //   8AACD5: 68 9B 00 00 00 = push 0x9B  → patch +1 9B→F0 ⇒ push 0xF0
    // Do NOT write the new imm onto the opcode VA (68→73 destroys the push).
    // Audit 2026-08-25: offline EXE byte check; IDA MCP unavailable — confirm in IDA when up.
    PatchByteChecked(0x008AA86F + 1, 0x73, "CreateWnd H low");
    PatchByteChecked(0x008AACD5 + 1, 0xF0, "scrollbar H");
    PatchByteChecked(0x008AAE23 + 1, 0x59, "macro Y");

    // CCtrlTab (id 2000): IDA 0x8AAC72 push 0AAh; 0x8AAC97 mov [eax+38h],22h.
    // 5*34=170 fits 4th-job; hyper book is 6th tab → widen + shrink spacing.
    PatchByteChecked(0x008AAC72 + 1, 0xAE, "CCtrlTab W");
    PatchByteChecked(0x008AAC97 + 3, 0x1C, "CCtrlTab spacing");

    // Skill-row buttons (MapleRoot skillexpansion; IDs 2010+), not job-book tabs.
    PatchByteChecked(0x008AD9F2 + 2, 0x4F, "tooltip range");
    PatchByteChecked(0x008ACE76 + 3, 0x66, "icon draw");
    PatchByteChecked(0x008AD7B4 + 2, 0xFB, "scrollbar fix");
    PatchByteChecked(0x008AC4DF + 1, 0x5B, "SP Y");
    PatchByteChecked(0x008AB929 + 2, 0xE0, "row click hi");
    ApplyBtnArraySkillexpansion();
    ApplySkillTipCrashGuards();
    Memory::WriteByte(0x008AD01A, 0xE9);
    Memory::WriteInt(0x008AD01A + 1, 0x008AD227 - (0x008AD01A + 5));

    Memory::PatchNop(0x008C5AFC, 6);
    // Cosmetic case-25 orange (MapleRoot customcolor) intentionally NOT applied:
    // vanilla get_basic_font @98B70C = push 0xFF000000; CUIStat/CUISkill push font case 1.
    Memory::PatchNop(0x00967707, 12);
    Memory::WriteByte(0x004F2D9B + 2, 0x07);

    // CUIStat / non-CUISkill UIs that call get_job_level (job%10==3 → level 5).
    PatchByteChecked(0x004E8F90 + 2, 0x05, "get_job_level cmp");
    {
        const unsigned char jl = *reinterpret_cast<volatile unsigned char*>(0x004E8F92);
        char buf[96];
        sprintf_s(buf, "[hyperskill] readback 4E8F92=%02X (expect 05)", jl);
        HyperSkillLog(buf);
    }
    {
        const unsigned int tabMax = *reinterpret_cast<volatile unsigned int*>(0x00BE2C68);
        char buf[96];
        sprintf_s(buf, "[hyperskill] readback BE2C68=%u (expect 6)", tabMax);
        HyperSkillLog(buf);
    }
}

} // namespace

void AttachHyperSkillMod() {
    HyperSkillLog("[hyperskill] AttachHyperSkillMod begin");
    ApplyHyperSkillUiPatches();
    // Permanent cave: HyperDoActiveSkills falls through to vanilla @g_doActiveJmpBack for unknown ids.
    // Avoids per-cast CodeCave + disk logging that caused post-cast stutter (2026-08-24).
    InstallDoActiveCave(reinterpret_cast<void*>(&HyperDoActiveSkills));
    HyperSkillLog("[hyperskill] DoActiveSkill cave installed (no per-cast hook)");
    // get_job_name first: sub_4A8C4F builds skill-root tabs only if the name lookup succeeds.
    bool ok = true;
    if (!Memory::SetHook(true, reinterpret_cast<void**>(&g_GetJobName), reinterpret_cast<void*>(&GetJobName_Hook))) {
        HyperSkillLog("[hyperskill] get_job_name hook FAILED");
        ok = false;
    } else {
        HyperSkillLog("[hyperskill] get_job_name hook OK");
    }
    if (!Memory::SetHook(true, reinterpret_cast<void**>(&g_IsAttackArea), reinterpret_cast<void*>(&IsAttackArea_Hook))) {
        HyperSkillLog("[hyperskill] is_attack_area hook FAILED");
        ok = false;
    }
    if (!Memory::SetHook(true, reinterpret_cast<void**>(&g_IsLtrb), reinterpret_cast<void*>(&IsLtrb_Hook))) {
        HyperSkillLog("[hyperskill] is_ltrb (766722) hook FAILED");
        ok = false;
    }
    if (!Memory::SetHook(true, reinterpret_cast<void**>(&g_RemoveBullet), reinterpret_cast<void*>(&RemoveBullet_Hook))) {
        HyperSkillLog("[hyperskill] remove_bullet hook FAILED");
        ok = false;
    }
    if (!Memory::SetHook(true, reinterpret_cast<void**>(&g_SkillEntryCmp), reinterpret_cast<void*>(&HyperSkillEntryCmp_Hook))) {
        HyperSkillLog("[hyperskill] SkillEntryCmp (A08E05) hook FAILED");
        ok = false;
    }
    HyperSkillLog(ok ? "[hyperskill] AttachHyperSkillMod OK (BE2C68=6+Btn_Array+CCtrlTab6+get_job_name+rows+UI)"
                     : "[hyperskill] AttachHyperSkillMod PARTIAL (UI applied; some hooks failed)");
    AttachMrSkillSatellites();
}
