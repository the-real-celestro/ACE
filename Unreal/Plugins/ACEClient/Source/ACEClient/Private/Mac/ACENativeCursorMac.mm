#include "ACENativeCursor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Mac/MacSystemIncludes.h"

class FACEMacNativeCursor final : public FACENativeCursor
{
    friend class FACEMacNativeCursorTest;
    struct FShape
    {
        TWeakObjectPtr<UTexture2D> Texture;
        int32 HotX=0,HotY=0;
        NSCursor* Handle=nil;
    };
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TSharedPtr<ICursor> PlatformCursor;
    TMap<EMouseCursor::Type,FShape> Shapes;
    float Scale=1.f;
    bool SoftwareWasEnabled=true;
public:
    FACEMacNativeCursor(UGameViewportClient* InViewport,TSharedPtr<ICursor> InCursor)
        : Viewport(InViewport),PlatformCursor(InCursor),SoftwareWasEnabled(InViewport->GetIsUsingSoftwareCursorWidgets()) {}
    virtual ~FACEMacNativeCursor() override
    {
        for(auto& Pair:Shapes)
        {
            PlatformCursor->SetTypeShape(Pair.Key,nullptr);
            [Pair.Value.Handle release];
        }
        if(auto* V=Viewport.Get())V->SetUseSoftwareCursorWidgets(SoftwareWasEnabled);
    }
    virtual bool SetTexture(EMouseCursor::Type Type,UTexture2D* Texture,int32 HotX,int32 HotY) override
    {
        if(!Texture||!Texture->GetPlatformData())return false;
        auto* Data=Texture->GetPlatformData();
        if(Data->PixelFormat!=PF_B8G8R8A8||Data->Mips.IsEmpty())return false;
        auto* Existing=Shapes.Find(Type);
        if(Existing&&Existing->Handle&&Existing->Texture==Texture&&Existing->HotX==HotX&&Existing->HotY==HotY)return true;
        const int32 W=FMath::Max(1,FMath::RoundToInt(Data->SizeX*Scale)),H=FMath::Max(1,FMath::RoundToInt(Data->SizeY*Scale));
        @autoreleasepool
        {
            NSBitmapImageRep* Rep=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil pixelsWide:W pixelsHigh:H
                bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace
                bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:W*4 bitsPerPixel:32];
            if(!Rep)return false;
            const auto* Source=static_cast<const FColor*>(Data->Mips[0].BulkData.LockReadOnly());
            if(!Source){Data->Mips[0].BulkData.Unlock();[Rep release];return false;}
            auto* Dest=[Rep bitmapData];
            for(int32 Y=0;Y<H;++Y)for(int32 X=0;X<W;++X)
            {
                const auto& P=Source[FMath::Min(Data->SizeY-1,Y*Data->SizeY/H)*Data->SizeX+FMath::Min(Data->SizeX-1,X*Data->SizeX/W)];
                const int32 I=(Y*W+X)*4;Dest[I]=P.R;Dest[I+1]=P.G;Dest[I+2]=P.B;Dest[I+3]=P.A;
            }
            Data->Mips[0].BulkData.Unlock();
            NSImage* Image=[[NSImage alloc] initWithSize:NSMakeSize(W,H)];[Image addRepresentation:Rep];
            NSCursor* Handle=[[NSCursor alloc] initWithImage:Image hotSpot:NSMakePoint(HotX*Scale,HotY*Scale)];
            [Image release];[Rep release];
            if(!Handle)return false;
            auto& Shape=Shapes.FindOrAdd(Type);
            PlatformCursor->SetTypeShape(Type,Handle);
            [Shape.Handle release];Shape={Texture,HotX,HotY,Handle};
            // Disable Slate's hidden hardware cursor only after native art exists.
            if(auto* V=Viewport.Get())V->SetUseSoftwareCursorWidgets(false);
            return true;
        }
    }
    virtual void SetScale(float Value) override
    {
        Value=FMath::Clamp(Value,.5f,4.f);if(FMath::IsNearlyEqual(Value,Scale))return;
        Scale=Value;
        const auto Previous=Shapes;
        for(const auto& Pair:Previous)
        {
            auto& Shape=Shapes[Pair.Key];Shape.Texture.Reset();
            SetTexture(Pair.Key,Pair.Value.Texture.Get(),Pair.Value.HotX,Pair.Value.HotY);
        }
    }
};

TSharedPtr<FACENativeCursor> FACENativeCursor::Create(UGameViewportClient* Viewport)
{
    if(!Viewport||!FSlateApplication::IsInitialized())return nullptr;
    auto Cursor=FSlateApplication::Get().GetPlatformCursor();
    if(Cursor)return MakeShared<FACEMacNativeCursor>(Viewport,Cursor);
    return nullptr;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Dat/ACEDatTextureResolver.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEMacNativeCursorTest,"ACE.RetailParity.MacNativeCursor",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEMacNativeCursorTest::RunTest(const FString&)
{
    auto* Viewport=NewObject<UGameViewportClient>(GEngine);Viewport->SetUseSoftwareCursorWidgets(true);
    auto Base=FACENativeCursor::Create(Viewport);
    if(!TestTrue(TEXT("Mac native cursor backend exists"),Base.IsValid()))return false;
    auto Cursor=StaticCastSharedPtr<FACEMacNativeCursor>(Base);
    const TArray<FColor> Pixels={FColor(255,0,0,255),FColor(0,255,0,255),FColor(0,0,255,255),FColor::Transparent};
    auto* Texture=FACEDatTextureResolver::CreateTransientRgbaUiIcon(2,2,Pixels);
    TestTrue(TEXT("Retail RGBA texture installs as a native cursor"),Cursor->SetTexture(EMouseCursor::Default,Texture,1,1));
    TestFalse(TEXT("Native cursor stops Slate from hiding the OS cursor"),Viewport->GetIsUsingSoftwareCursorWidgets());
    auto* First=Cursor->Shapes[EMouseCursor::Default].Handle;
    TestTrue(TEXT("Native hotspot matches the visible click point"),NSEqualPoints([First hotSpot],NSMakePoint(1,1)));
    NSBitmapImageRep* Image=(NSBitmapImageRep*)[[[First image] representations] firstObject];
    const auto* Actual=[Image bitmapData];
    TestTrue(TEXT("Native image retains red, green, blue, and alpha channels"),Actual[0]==255&&Actual[1]==0&&Actual[2]==0&&Actual[3]==255&&Actual[4]==0&&Actual[5]==255&&Actual[10]==255&&Actual[15]==0);
    Cursor->SetTexture(EMouseCursor::Default,Texture,1,1);
    TestTrue(TEXT("Stationary mouse reuses native cursor without allocation"),Cursor->Shapes[EMouseCursor::Default].Handle==First);
    Cursor->SetScale(2.f);
    auto* Scaled=Cursor->Shapes[EMouseCursor::Default].Handle;
    TestTrue(TEXT("Native cursor scale changes art and hotspot together"),NSEqualSizes([[Scaled image] size],NSMakeSize(4,4))&&NSEqualPoints([Scaled hotSpot],NSMakePoint(2,2)));
    TestTrue(TEXT("Native window resize art is supported"),Cursor->SetTexture(EMouseCursor::ResizeUpDown,Texture,0,0));
    TestFalse(TEXT("Missing art cannot replace a working cursor"),Cursor->SetTexture(EMouseCursor::Default,nullptr,0,0));
    Cursor.Reset();Base.Reset();
    TestTrue(TEXT("Controller shutdown restores the previous cursor mapping policy"),Viewport->GetIsUsingSoftwareCursorWidgets());
    return !HasAnyErrors();
}
#endif
