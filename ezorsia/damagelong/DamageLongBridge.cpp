#include "stdafx.h"
#include "DamageLongApi.h"
#include "damagelong.h"

namespace DamageLong {
void EnsureHooks() {
    static bool attached = false;
    if (attached) {
        return;
    }
    attached = true;
    AttachDamageLongMod();
}
} // namespace DamageLong
