#pragma once

#include <cstring>

// Minimal StringPool helper for Found In world-map text swap.
inline void SetStringPoolString(unsigned index, const char* text) {
    if (!text) {
        return;
    }
    // ms_aString pool; entries are XOR-encoded — swapping is done via the client's own
    // StringPool::SetString when available. For BeiDou this is a best-effort no-op stub
    // unless a future hook wires the real setter.
    (void)index;
    (void)text;
}
