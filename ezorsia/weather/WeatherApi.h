#pragma once

// Day/night + weather + street lamps (from weather-time-systems bundle).
void AttachWeatherMod();
void AttachWeatherWindMod();

void Weather_Tick();
void WeatherPuddle_Frame();
void WeatherAccum_Frame();
void WeatherSplash_Frame();
void WeatherMove_Frame();
void WeatherMove_Restore();
void WeatherSway_Frame();

bool Weather_IsFieldActive();
bool Weather_HasFallingSky();

void Weather_RegisterPacketHandlers();
