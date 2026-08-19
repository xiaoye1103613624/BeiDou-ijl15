#pragma once

// Meso uncap: Decode4→Decode8 + UI format globals + Encode8 for storage/trade.
// Requires matching PacketCreator writeLong / StorageProcessor·Trade readLong.
void AttachMesoUncapMod();
