#pragma once

#include <cstdint>
#include <vector>

struct DyeHsl {
    float hue = 0.f;
    float sat = 0.f;
    float light = 0.f;
    bool nearZero() const {
        return (hue > -0.01f && hue < 0.01f)
            && (sat > -0.001f && sat < 0.001f)
            && (light > -0.001f && light < 0.001f);
    }
};

struct DyeEntry {
    int itemId = 0;
    DyeHsl hsl;
};

namespace EquipDye {

void Hook();

void SyncItemHslFromList(const std::vector<DyeEntry>& entries);
void MergeItemHslFromList(int charId, const std::vector<DyeEntry>& entries);

DyeHsl GetOwnedHsl(int itemId);
void SetOwnedHsl(int itemId, const DyeHsl& hsl);
void ClearOwnedHsl(int itemId);

void SoftRefreshPreview(int itemId, const DyeHsl& hsl);
void RestoreSharedWzForItem(int itemId);

void ApplyOwnedItemDyeOnly();
void ScheduleDeferredOwnedApply(int delayMs);
void PollDeferredOwnedApply();

void OnCanvasResolved(void* punk);

void RequestForeignAvatarRefresh(int charId);
bool TryConsumeForeignAvatarRefresh(int* outCharId);
void ApplyForeignVisibleDye(int charId);

bool IsDyeableCategory(int itemId);
bool IsCashItemId(int itemId);

void SetLoadingItemId(int itemId);
int GetLoadingItemId();
void SetAllowFieldLoadDye(bool allow);

} // namespace EquipDye
