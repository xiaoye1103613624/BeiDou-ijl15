#pragma once

// cashshopwnd.h - standalone cash shop window entry points

class CInPacket;

// Match server RecvOpcode.CASHSHOP_WINDOW_ACTION / SendOpcode.CASHSHOP_WINDOW_SYNC
static constexpr unsigned short kCashShopActionOpcode = 0x3730;   // client -> server
static constexpr unsigned short kCashShopSyncOpcode   = 0x3731;   // server -> client

void CashShopWnd_HandleSync(CInPacket* pPacket);
void CashShopWnd_Tick();
void CashShopWnd_RequestOpen();
void CashShopWnd_Close();
bool CashShopWnd_IsOpen();
