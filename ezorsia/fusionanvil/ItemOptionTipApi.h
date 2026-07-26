#pragma once
#include <string>

// Format ItemOption tip line(s) as GBK (same table as equip tooltip companion).
std::string ItemOptionTip_FormatGbk(int optionId, int potLevel);

// Infer cube grade 1..5 from option id bands (1xxxx rare .. 4xxxx legendary).
unsigned char ItemOptionTip_InferGrade(int pot1, int pot2, int pot3);

// potLevel from equip reqLevel: (req+9)/10 clamped 1..20.
int ItemOptionTip_PotLevelFromReq(int reqLevel);
