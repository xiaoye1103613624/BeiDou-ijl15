#pragma once

// Expand teleport rock / VIP teleport rock saved-map slots (vanilla 5 / 10 → 999 / 999).
// Must stay in sync with server PacketCreator.addTeleportInfo / trockRefreshMapList.
void AttachMapTransferExpandMod();
