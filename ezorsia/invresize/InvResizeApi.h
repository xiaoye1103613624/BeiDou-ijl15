#pragma once

#include <windows.h>

void AttachInvResizeMod();
void InvResize_Tick();
bool InvResize_HandleMouseMessage(UINT& msg, WPARAM wParam, LPARAM lParam, LRESULT* plResult);

namespace InvResize {
void EnsureHooks();
void OnTick();
}
