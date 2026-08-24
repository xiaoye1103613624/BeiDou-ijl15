#pragma once

// NO-OP WEATHER SHIM. You do not need to edit this file, and in most installs you will
// never look at it again.
//
// Hair sway reads the sky in exactly one place, to stiffen its motion under a storm or a
// blizzard. That is the entire coupling: one call and two enum values, on four lines of
// hairsway.cpp. Requiring a whole weather system for it would be absurd, so the include
// in hairsway.cpp is guarded:
//
//     #if __has_include("weather.h")
//     #  include "weather.h"
//     #  define HAIRSWAY_HAS_WEATHER 1
//     #else
//     #  include "hairsway_weather.h"
//     #endif
//
// With no weather system present, IsRoughWeather() compiles to `return false` and the
// hair runs at its baseline strength forever. If you install a weather system later that
// provides a `weather.h` with `Weather::CurrentSky()` and `Weather::SKY_STORM` /
// `Weather::SKY_BLIZZARD`, the guard picks it up on the next build and rough weather
// switches itself on. Nothing here changes and nothing in hairsway.cpp changes.
//
// WHY THIS IS NOT CALLED weather.h. A weather system installs its own header by that
// name into this same directory. A shim wearing the same name would shadow the real one
// for anyone who has both, silently, and the failure would look like "the weather system
// stopped working" rather than "there is a stray stub header". The guard above is written
// to prefer the real header precisely so that this one can never win.
//
// This header deliberately declares NOTHING. The `#else` branch of IsRoughWeather() never
// names the Weather namespace, so there is nothing to declare, and an empty shim cannot
// drift out of step with a real weather header's enum values.
