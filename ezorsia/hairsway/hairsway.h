#pragma once

struct IWzGr2DLayer;
struct IWzCanvas;

// Runtime hair sway for the local player's normal hairstyle range.
bool HairSway_Applies(void* pAvatar, int hairId);
void HairSway_BeginLayer(void* pAvatar, int hairId);
void HairSway_EndLayer();
bool HairSway_CaptureLayer(IWzGr2DLayer* layer, IWzCanvas* insertedCanvas);
void HairSway_Tick();
