#pragma once

void AttachCharSlotsMod();

namespace CharSlots {
// Byte patches must run at DllMain (before character select UI).
void ApplyPatches();
}
