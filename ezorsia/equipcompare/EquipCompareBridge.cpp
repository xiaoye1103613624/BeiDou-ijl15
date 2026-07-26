#include "stdafx.h"
#include "EquipCompareApi.h"

namespace {
bool g_hooksAttached = false;
} // namespace

namespace EquipCompare {
void EnsureHooks() {
    if (g_hooksAttached) {
        return;
    }
    g_hooksAttached = true;
    AttachEquipCompareMod();
}

// RelayoutActiveCompareTip is defined in equipcompare.cpp
} // namespace
