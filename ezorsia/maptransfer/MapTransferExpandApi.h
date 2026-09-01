#pragma once

// Expand teleport rock / VIP teleport rock saved-map slots (vanilla 5 / 10 → 20 / 999).
// Must stay in sync with server GameConstants + PacketCreator.addTeleportInfo / trockRefreshMapList.
void AttachMapTransferExpandMod();
