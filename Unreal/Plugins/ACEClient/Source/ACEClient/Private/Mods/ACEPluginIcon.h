#pragma once
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

struct FACEPluginIconArtwork;

/** Painted AC-style inventory emblems, with vector fallbacks if the atlas is unavailable. */
class SACEPluginIcon : public SLeafWidget
{
    friend class FACEPluginIconArtworkTest;
public:
    SLATE_BEGIN_ARGS(SACEPluginIcon) {} SLATE_ARGUMENT(FString, Id) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(26,26); }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool) const override;
    static FSlateLayoutTransform Fit(const FVector2D& Size);
    /** Stable color identity, shared by the emblem and launcher tile. */
    static FLinearColor Accent(const FString& Id);
    /** Row-major cell in the shared 5x4 painted atlas; the drag grip has no painted cell. */
    static int32 ArtworkSlot(const FString& Id);
private:
    TSharedPtr<FACEPluginIconArtwork> Artwork;
    int32 ArtworkIndex=INDEX_NONE;
    const FSlateBrush* PaintedBrush() const;
    struct FPath { TArray<FVector2D> Points; FLinearColor Color; bool Filled=false; };
    TArray<FPath> Paths;
};
