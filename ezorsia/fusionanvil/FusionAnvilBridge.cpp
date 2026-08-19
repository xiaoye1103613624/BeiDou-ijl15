#include "stdafx.h"
#include "FusionAnvilApi.h"

namespace FusionAnvil {
void EnsurePacketHooks() {
    AttachFusionAnvilPacketHooks();
}

void EnsureHooks() {
    AttachFusionAnvilUiHooks();
}

void EnsureTooltipHooks() {
    AttachFusionAnvilTooltipHooks();
}

void EnsureItemIconHooks() {
    AttachFusionAnvilItemIconHooks();
}
} // namespace
