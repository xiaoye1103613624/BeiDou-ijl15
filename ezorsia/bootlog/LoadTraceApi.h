#pragma once

// Install hooks that write stage markers into client_boot.log during
// CWvsApp::Init / InitializeResMan / InitializeGameData / WZ mount.
void AttachLoadTrace();
