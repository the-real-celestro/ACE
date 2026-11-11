#pragma once
#include "ACELootQuality.h"
class UWidget;
class UACEUIResourceResolver;
namespace ACELootPresentation
{
    void Tooltip(UWidget* Owner,const FString& Name,const FACEWorldObject& Item,
        const FACEQualityReview& Review,UACEUIResourceResolver* Resources);
}
