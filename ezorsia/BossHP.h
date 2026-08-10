#pragma once

class BossHP
{
public:
	static void Hook();
	// Field lifecycle without DetourAttach on CField::Init (LazyCompat owns 0x528DBC).
	static void OnFieldEnter();
	static void OnFieldDispose();
	static void OnClientTick();
};
