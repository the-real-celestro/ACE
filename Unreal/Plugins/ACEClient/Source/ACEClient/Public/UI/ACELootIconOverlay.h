#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "ACELootQuality.h"
#include "ACELootIconOverlay.generated.h"

/** One-pixel quality edge; class marks are permitted only over clear artwork. */
UCLASS()
class ACECLIENT_API UACELootIconOverlay : public UWidget
{
    GENERATED_BODY()
public:
    void SetReview(const FACEQualityReview& Review,class UTexture2D* Foreground,bool PreserveBackground=false);
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool ReleaseChildren) override;
private:
    TSharedPtr<class SACEQualityIcon> Painted;
};
