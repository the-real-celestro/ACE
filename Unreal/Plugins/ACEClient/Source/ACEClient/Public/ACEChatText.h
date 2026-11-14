#pragma once
#include "CoreMinimal.h"

/** A color span in plain chat text; offsets stay stable through wrapping and copy. */
struct FACEChatAccent
{
    FIntPoint Range=FIntPoint(INDEX_NONE,INDEX_NONE);
    FLinearColor Color=FLinearColor::White;
    bool IsValid(int32 Length) const {return Range.X>=0&&Range.Y>Range.X&&Range.Y<=Length;}
};
