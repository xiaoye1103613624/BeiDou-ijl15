#pragma once

#include "targetver.h"

#if defined(_M_X64) || defined(_M_AMD64)
#error "BeiDou-ijl15 must be built as Win32 (x86). Switch the VS platform from x64 to x86, or use: MSBuild ezorsia.vcxproj /p:Platform=Win32 /p:Configuration=Release"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#endif

// Include before <windows.h> to avoid winsock.h conflicts.
// Needed for hostname->IPv4 resolution (getaddrinfo/InetPton/InetNtop).
#include <winsock2.h>
#include <ws2tcpip.h>

// Windows Header Files
#include <windows.h>

// reference additional headers your program requires here

#include <iostream>
#include "Client.h"
#include "Memory.h"

