#pragma once

IWzCanvasPtr GetMobIcon(int id);

IWzCanvasPtr GetNpcIcon(int id);

IWzCanvasPtr GetMapIcon(int mapId);

// Quest "!" bulb shown next to NPCs that have an actionable quest.
// state: 1 = available (startable), 2 = completable (ready to turn in).
IWzCanvasPtr GetQuestMarkerIcon(int state);
