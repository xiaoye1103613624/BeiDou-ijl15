#include "stdafx.h"
#include "FusionAnvilApi.h"
#include "Client.h"

namespace FusionAnvil {
void EnsurePacketHooks() {
    AttachFusionAnvilPacketHooks();
}

void EnsureHooks() {
    AttachFusionAnvilUiHooks();
}

void EnsureTooltipHooks() {
    if (!Client::enableFusionAnvilTooltipHooks) {
        return;
    }
    AttachFusionAnvilTooltipHooks();
}

void EnsureItemIconHooks() {
    AttachFusionAnvilItemIconHooks();
}
} // namespace
