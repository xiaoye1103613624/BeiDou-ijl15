#pragma once

// Level ushort expansion (byte 255 -> short 300) + EXP Decode8/FakeTear64.
// Level: Decode1->Decode2 + LevelFakeTear + Fuse_byte hook + FixMovsx.
// EXP: codecave Decode4x2 + store int64 with CS=0 (shares Fuse_long CS==0 with maxhpmp).
// Raises GetNextLevelExp clamps 200 -> 300 (imm32 sites).
// EquipCalc: also widen mov cl,bl after Fuse so IsAbleToWear sees full level (not 300&0xFF).
void AttachLevel300Mod();
