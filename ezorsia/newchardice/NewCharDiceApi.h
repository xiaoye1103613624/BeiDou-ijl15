#pragma once

// 10\u7ea7\u524d\u81ea\u7531\u52a0\u70b9 + \u5192\u9669\u5bb6\u521b\u5efa\u89d2\u8272\u6295\u9ab0\u5b50\uff08\u539f\u751f NewChar UI\uff09
// BeiDou.exe @ imagebase 0x400000 \u2014 IDA \u6838\u5bf9 2026-08-28

void AttachNewCharDiceMod();

namespace NewCharDice {
void ApplyPatches();
void OnClientTick();
bool IsEnabled();
}
