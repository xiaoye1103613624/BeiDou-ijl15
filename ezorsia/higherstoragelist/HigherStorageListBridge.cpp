#include "stdafx.h"
#include "HigherStorageListApi.h"

namespace {
bool g_patchesApplied = false;
} // namespace

namespace HigherStorageList {
void ApplyPatches() {
    if (g_patchesApplied) {
        return;
    }
    g_patchesApplied = true;
    AttachHigherStorageListMod();
}
} // namespace
