#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACETestDatDirectory.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACELootQualitySubsystem.h"
#include "ACEHoverTooltipWidget.h"
#include "ACEPlayerController.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACELootIconOverlay.h"
#include "Components/Border.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Slate/WidgetRenderer.h"
#include "Input/HittestGrid.h"
#include "RenderingThread.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEItemHoverRoutingTest,"ACE.LootQuality.HoverRouting",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEItemHoverRoutingTest::RunTest(const FString&)
{
    if(!FApp::CanEverRender()||!FSlateApplication::IsInitialized())return true;
    auto* PC=GEngine&&GEngine->GameViewport&&GEngine->GameViewport->GetWorld()
        ?Cast<AACEPlayerController>(GEngine->GameViewport->GetWorld()->GetFirstPlayerController()):nullptr;
    if(!TestNotNull(TEXT("Native client controller is available for viewport hover routing"),PC))return false;
    auto* GI=NewObject<UGameInstance>();GI->Init();ON_SCOPE_EXIT{GI->Shutdown();};
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
    auto* Resources=NewObject<UACEUIResourceResolver>(GI);Resources->Initialize(Dat);
    auto* Canvas=NewObject<UACEUICanvasWidget>(GI);Canvas->Initialize();Canvas->SetResourceResolver(Resources);
    auto* Binder=NewObject<UACEUIGameplayBinder>(GI);Binder->Canvas=Canvas;Binder->Client=GI->GetSubsystem<UACEClientSubsystem>();
    auto* Loot=GI->GetSubsystem<UACELootQualitySubsystem>();
    FACEWorldObject Item;Item.Guid=101;Item.WeenieClassId=3884;Item.ItemType=ACEItemType::MeleeWeapon;Item.Name=TEXT("Hover fixture sword");Item.IconId=0x06001661;
    FACEAppraisalInfo A;A.ObjectGuid=101;A.bSuccess=true;A.bHasWeaponProfile=true;A.WeaponSkill=44;
    A.Damage=65;A.DamageVariance=.47;A.WeaponOffense=1.08;A.FloatProperties.Add(29,1.08);A.IntProperties={{158,2},{159,44},{160,400},{171,0}};
    Loot->Review(Item);Loot->ObserveAppraisal(A);
    auto Layout=SNew(SConstraintCanvas);TArray<UBorder*> Icons;
    for(int32 I=0;I<7;++I)
    {
        auto* Icon=NewObject<UBorder>(Canvas);Icon->SetVisibility(I==1?ESlateVisibility::Visible:ESlateVisibility::HitTestInvisible);
        Binder->SetItemTooltip(Icon,Item);Icons.Add(Icon);
        Layout->AddSlot().Offset(FMargin(12+I*60,12,32,32)).Alignment(FVector2D::ZeroVector)[Icon->TakeWidget()];
    }
    Binder->PaperDollIcons.Add(TEXT("Equipped"),Icons[0]);Binder->InventorySlots.Add(Icons[1]);Binder->ShortcutIcons.Add(Icons[2]);
    Binder->ExtItemSlots.Add(Icons[3]);Binder->VendorItemSlots.Add(Icons[4]);Binder->TradeSelfSlots.Add(Icons[5]);Binder->TradeOtherSlots.Add(Icons[6]);
    const FVector2D Size(460,64);auto Window=SNew(SVirtualWindow).Size(Size);Window->SetContent(Layout);
    auto& Slate=FSlateApplication::Get();Slate.RegisterVirtualWindow(Window);
    FWidgetRenderer Renderer(false,true);auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Nearest,false);FHittestGrid Grid;
    auto PreviousBinder=PC->DatGameplayBinder;
    ON_SCOPE_EXIT{PC->ClearItemHover();PC->DatGameplayBinder=PreviousBinder;Slate.UnregisterVirtualWindow(Window);Target->ReleaseResource();};
    PC->DatGameplayBinder=Binder;
    auto Frame=[&](){++GFrameCounter;Renderer.DrawWindow(Target,Grid,Window,1.f,Size,.016f);FlushRenderingCommands();};Frame();Frame();
    auto Center=[](UBorder* Icon){const auto& G=Icon->GetCachedGeometry();return G.LocalToAbsolute(G.GetLocalSize()*.5);};
    for(int32 I=0;I<Icons.Num();++I)
    {
        auto* Tip=Cast<UACEHoverTooltipWidget>(Icons[I]->GetToolTip());
        TestTrue(TEXT("Every item surface resolves the full attribute panel"),Tip&&Binder->GetItemTooltipAt(Center(Icons[I]))==Tip);
        TestFalse(TEXT("No duplicate native popup is registered"),Icons[I]->GetCachedWidget()->GetToolTip().IsValid());
        TestTrue(TEXT("Item hover is displayed by the controller"),PC->UpdateItemHoverAt(Center(Icons[I]),{100,100}));
        TestTrue(TEXT("Equipped, backpack, loot, vendor and trade panels enter the viewport"),PC->ItemHoverTooltip==Tip&&Tip->IsInViewport());
        FString HoverName;FVector2D HoverPoint;
        TestTrue(TEXT("Hover accessibility retains the item name and cursor position"),PC->GetHoverTooltip(HoverName,HoverPoint)&&HoverName==Item.Name&&HoverPoint.Equals({100,100}));
        TestEqual(TEXT("The hover cannot intercept item clicks"),Tip->GetVisibility(),ESlateVisibility::HitTestInvisible);
    }
    const auto Point=Center(Icons[0]);PC->UpdateItemHoverAt(Point,{100,100});auto* Stable=PC->ItemHoverTooltip.Get();
    const auto Root=Stable->GetCachedWidget();
    for(int32 I=0;I<100;++I)
    {
        A.FloatProperties[29]=I%2?1.08:1.2;Loot->ObserveAppraisal(A);Binder->SetItemTooltip(Icons[0],Item);Frame();
        TestTrue(TEXT("Stationary equipped hover survives appraisal and inventory refresh"),PC->UpdateItemHoverAt(Point,{100,100})&&PC->ItemHoverTooltip==Stable&&Stable->IsInViewport());
        TestTrue(TEXT("Viewport panel root stays attached throughout refresh"),Stable->GetCachedWidget()==Root);
        TestFalse(TEXT("Refresh never recreates an OS tooltip"),Icons[0]->GetCachedWidget()->GetToolTip().IsValid());
    }
    Icons[0]->SetVisibility(ESlateVisibility::Collapsed);
    TestFalse(TEXT("Hidden equipment cannot keep a stale hover"),PC->UpdateItemHoverAt(Point,{100,100}));
    TestFalse(TEXT("Leaving the item removes its viewport panel"),Stable->IsInViewport());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEQualityBorderTest,"ACE.LootQuality.IconBorders",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEQualityBorderTest::RunTest(const FString&)
{
    if(!FApp::CanEverRender()||!FSlateApplication::IsInitialized())return true;
    auto* Overlay=NewObject<UACELootIconOverlay>();FACEQualityReview R;R.bRated=true;R.Grade=ACELootScoring::Grade::Epic;
    Overlay->SetReview(R,nullptr);FWidgetRenderer Renderer(false,true);
    for(int32 Size:{32,64})
    {
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size,Size),TF_Nearest,false);
        ++GFrameCounter;Renderer.DrawWidget(Target,Overlay->TakeWidget(),FVector2D(Size,Size),.016f);FlushRenderingCommands();
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        const int32 Inset=Size==32?2:4;
        const auto Bottom=Pixels[(Size-1-Inset)*Size+Size/2],Left=Pixels[(Size/2)*Size+Inset],Top=Pixels[Inset*Size+Size/2],Center=Pixels[(Size/2)*Size+Size/2];
        TestTrue(TEXT("Quality-colored frame is visible on all four sides at both icon sizes"),Bottom.A>100&&Left.A>100&&Top.A>100&&Left.B>Left.G&&Top.B>Top.G&&Bottom.B>Bottom.G);
        const auto UpperBevel=Pixels[(Size==32?1:2)*Size+Size/2],LowerBevel=Pixels[(Size-1-(Size==32?1:2))*Size+Size/2];
        TestTrue(TEXT("Beveled frame has a brighter upper face and shaded lower face"),int32(UpperBevel.R)+UpperBevel.G+UpperBevel.B>int32(LowerBevel.R)+LowerBevel.G+LowerBevel.B);
        TestEqual(TEXT("Frame leaves interior artwork and imbue background untouched"),Center.A,uint8(0));
        Target->ReleaseResource();
    }
    // Capture the actual DAT artwork with each native quality indicator for
    // visual review. These six grades are deliberate test examples.
    auto* GI=NewObject<UGameInstance>();auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(TestTrue(TEXT("Retail artwork is available for the quality capture"),Dat->LoadDatDirectory(ACETestDatDirectory())))
    {
        auto* Resources=NewObject<UACEUIResourceResolver>(GI);Resources->Initialize(Dat);
        FSlateBrush Background,Foreground;Background.DrawAs=Foreground.DrawAs=ESlateBrushDrawType::Image;
        Background.SetResourceObject(Resources->ResolveItemBackground(ACEItemType::MeleeWeapon,0));
        auto* Art=Resources->ResolveItemForeground(0x06001661,0,0,false,0,0);Foreground.SetResourceObject(Art);
        auto Row=SNew(SHorizontalBox);TArray<UACELootIconOverlay*> Samples;
        for(int32 I=0;I<6;++I)
        {
            auto* Sample=NewObject<UACELootIconOverlay>(GI);auto Review=R;Review.Grade=static_cast<ACELootScoring::Grade>(I);
            Sample->SetReview(Review,Art);Samples.Add(Sample);
            Row->AddSlot().AutoWidth().Padding(4)[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(FText::FromString(ACELootQuality::Name(Review.Grade))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),8)).ColorAndOpacity(ACELootQuality::Color(Review.Grade))]
                +SVerticalBox::Slot().AutoHeight()[SNew(SBox).WidthOverride(64).HeightOverride(64)[SNew(SOverlay)
                    +SOverlay::Slot()[SNew(SImage).Image(&Background)]
                    +SOverlay::Slot()[SNew(SImage).Image(&Foreground)]
                    +SOverlay::Slot()[Sample->TakeWidget()]]]];
        }
        Row->SlatePrepass();const FIntPoint Size(FMath::CeilToInt(Row->GetDesiredSize().X),FMath::CeilToInt(Row->GetDesiredSize().Y));
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Nearest,false);
        ++GFrameCounter;Renderer.DrawWidget(Target,Row,FVector2D(Size),.016f);FlushRenderingCommands();
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        for(auto& Pixel:Pixels)Pixel=Pixel.ReinterpretAsLinear().ToFColor(true);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("LootQuality-frames.png")));Target->ReleaseResource();
    }
    return !HasAnyErrors();
}
#endif
