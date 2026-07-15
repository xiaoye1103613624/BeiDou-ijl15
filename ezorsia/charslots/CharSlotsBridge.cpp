#include "stdafx.h"
#include "CharSlotsApi.h"

namespace {
bool g_patchesApplied = false;
} // namespace

namespace CharSlots {
void ApplyPatches() {
    if (g_patchesApplied) {
        return;
    }
    g_patchesApplied = true;
    AttachCharSlotsMod();
}
} // namespace
