#pragma once
// Kaentake pch.h stand-in for Coloring Prism / WeaponTint (BeiDou-ijl15).
#include "../stdafx.h"
#include "compat/hook.h"
#include "compat/wvs/Packet.h"

// Upstream decode surface; BeiDou PacketDispatcher uses CompatInPacket.
// Alias lives in weapontint.h as well; keep pch consistent for non-weapontint TUs.
#include "compat/wvs/Packet.h"
using CInPacket = CompatInPacket;

// COutPacket only (do not include packet_legacy.h — it redefines CInPacket).
#include "outpacket_shim.h"
