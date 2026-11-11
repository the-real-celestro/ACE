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
        const int32 Inside=Size==32?2:5;
        const auto Left=Pixels[(Size/2)*Size+Inside],Top=Pixels[Inside*Size+Size/2],Center=Pixels[(Size/2)*Size+Size/2];
        TestTrue(TEXT("Thick quality color is visible across the icon edge at both scales"),Left.A>240&&Top.A>240&&Left.R>80&&Left.B>100&&Top.R>80&&Top.B>100);
        TestEqual(TEXT("Quality frame leaves the center artwork unobscured"),Center.A,uint8(0));
        Target->ReleaseResource();
    }
    return !HasAnyErrors();
}
#endif
