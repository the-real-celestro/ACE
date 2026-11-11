#pragma once
#include "ACELootQuality.h"
class UWidget;
class UACEUIResourceResolver;
namespace ACELootPresentation
{
    // Authored underlays and imbue/effect palettes must remain recognizable.
    inline bool PreserveBackground(const FACEWorldObject& Item)
    { return Item.IconUnderlayId!=0 || (Item.UiEffects&~uint32(1))!=0; }
    void Tooltip(UWidget* Owner,const FString& Name,const FACEWorldObject& Item,
        const FACEQualityReview& Review,UACEUIResourceResolver* Resources);
}
