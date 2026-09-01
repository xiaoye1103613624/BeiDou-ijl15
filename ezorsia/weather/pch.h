#pragma once
// Local precompiled stand-in for Kaentake pch.h — BeiDou uses stdafx.h.
#include "../stdafx.h"
#include "compat/wvs/Packet.h"

// Weather handlers historically take CInPacket*; BeiDou packet surface is CompatInPacket.
using CInPacket = CompatInPacket;
