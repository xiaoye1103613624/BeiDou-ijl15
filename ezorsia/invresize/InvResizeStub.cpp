// Tip-clean stub — no inventory-resize UI/hooks (enter-red when real invresize linked).
#include "stdafx.h"
#include "InvResizeApi.h"

void AttachInvResizeMod() {}
void InvResize_Tick() {}
bool InvResize_HandleMouseMessage(UINT&, WPARAM, LPARAM, LRESULT*) { return false; }

namespace InvResize {
void EnsureHooks() {}
void OnTick() {}
}
