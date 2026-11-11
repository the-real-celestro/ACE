#pragma once
#include "CoreMinimal.h"
#include "GenericPlatform/ICursor.h"

class UGameViewportClient;
class UTexture2D;

// A native Mac cursor remains visible above Notification Center and other OS
// windows. Other desktop platforms continue to use the existing Slate cursor.
class FACENativeCursor
{
public:
    static TSharedPtr<FACENativeCursor> Create(UGameViewportClient* Viewport);
    virtual ~FACENativeCursor() = default;
    virtual bool SetTexture(EMouseCursor::Type Type,UTexture2D* Texture,int32 HotX,int32 HotY)=0;
    virtual void SetScale(float Scale)=0;
};
