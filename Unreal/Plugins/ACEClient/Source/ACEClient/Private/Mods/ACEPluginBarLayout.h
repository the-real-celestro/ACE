#pragma once

#include "CoreMinimal.h"

/** Geometry shared by layout and resizing. Sizes include the revealable 18px header. */
namespace ACEPluginBarLayout
{
    constexpr double Padding=4., Header=18.;
    inline FVector2D Tile(bool Large,bool Labels,bool Horizontal,float TextScale=1.f)
    {
        return Labels?(Horizontal?FVector2D(96,Large?78:66):FVector2D(208,Large?56:40))*FMath::Max(1.f,TextScale)
            :FVector2D(Large?52:36,Large?52:36);
    }
    inline FVector2D Pitch(FVector2D Tile,bool Large){return Tile+FVector2D(Large?6:4);}
    struct FGrid { int32 Columns=1,Rows=1;FVector2D Size=FVector2D::ZeroVector; };
    inline FGrid Fit(int32 Count,int32 RequestedColumns,FVector2D Pitch,FVector2D View=FVector2D::ZeroVector)
    {
        Count=FMath::Max(1,Count);
        int32 MinColumns=1,MaxColumns=Count;
        if(View.X>0&&View.Y>0)
        {
            MaxColumns=FMath::Clamp(FMath::FloorToInt((View.X-2*Padding)/Pitch.X),1,Count);
            const int32 MaxRows=FMath::Max(1,FMath::FloorToInt((View.Y-2*Padding-Header)/Pitch.Y));
            MinColumns=FMath::Min(MaxColumns,FMath::DivideAndRoundUp(Count,MaxRows));
        }
        FGrid Grid;Grid.Columns=FMath::Clamp(RequestedColumns,MinColumns,MaxColumns);
        Grid.Rows=FMath::DivideAndRoundUp(Count,Grid.Columns);
        Grid.Size=FVector2D(2*Padding+Grid.Columns*Pitch.X,2*Padding+Header+Grid.Rows*Pitch.Y);
        return Grid;
    }
    inline int32 ResizeColumns(int32 Count,FVector2D Extent,FVector2D Pitch,bool ByRows)
    {
        if(ByRows)
        {
            const int32 Rows=FMath::Clamp(FMath::RoundToInt((Extent.Y-2*Padding-Header)/Pitch.Y),1,FMath::Max(1,Count));
            return FMath::DivideAndRoundUp(FMath::Max(1,Count),Rows);
        }
        return FMath::Clamp(FMath::RoundToInt((Extent.X-2*Padding)/Pitch.X),1,FMath::Max(1,Count));
    }
}
