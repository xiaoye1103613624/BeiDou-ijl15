#pragma once

// Level ushort (300) + EXP Decode8. Fuse sentinel = 'LVL3' (never CS==0).
// Requires matching PacketCreator writeShort(level) + writeLong(exp).
void AttachLevel300Mod();
