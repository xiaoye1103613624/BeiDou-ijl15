#pragma once

/**
 * Quick login + optional cash-trade client patches (GMS v083 / BeiDou).
 *
 * quicklogin: after world select, auto-focus channel window (Enter -> ch1);
 *             CLogin ctor clears sub-step garbage + brings login window to foreground.
 * allowCashTrade: NOP cash-item trade checks at 0x004F3FB8 / 0x004F3FC4.
 */
void AttachQuickLoginMod();
void AttachAllowCashTradeMod();
