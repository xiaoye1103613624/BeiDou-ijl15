#include "stdafx.h"
#include "HigherShopListApi.h"

namespace {
bool g_patchesApplied = false;
} // namespace

namespace HigherShopList {
void ApplyPatches() {
    if (g_patchesApplied) {
        return;
    }
    g_patchesApplied = true;
    AttachHigherShopListMod();
}
} // namespace
