// Link with Release_frozen_NOHOOK\shoulders.obj (pre-CD64 API).
// Full shoulders.cpp already exports Shoulder_UseNativeCd64Slots — do NOT
// compile this stub together with a rebuilt shoulders.obj.
bool Shoulder_UseNativeCd64Slots() {
    return false;
}
