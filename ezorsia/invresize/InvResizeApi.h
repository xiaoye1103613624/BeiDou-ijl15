#pragma once

#include <windows.h>

void AttachInvResizeMod();
void InvResize_Tick();
bool InvResize_HandleMouseMessage(UINT& msg, WPARAM wParam, LPARAM lParam, LRESULT* plResult);
/** Re-apply composited narrow-tab background on the live inventory window. */
void InvResize_ReinstallLiveBackground();

namespace InvResize {
void EnsureHooks();
void OnTick();
}
