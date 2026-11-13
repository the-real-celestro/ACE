#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACELoginWidget.h"
#include "ACEHoverTooltipWidget.h"
#include "ACELootQuality.h"
#include "UI/ACELootPresentation.h"
#include "Components/Border.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELauncherFirstFrameTest,"ACE.Launcher.FirstFrame",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELauncherFirstFrameTest::RunTest(const FString&)
{
    if(!FApp::CanEverRender()||!FSlateApplication::IsInitialized())return false;
    TStrongObjectPtr<UACELoginWidget> Widget(NewObject<UACELoginWidget>());Widget->Initialize();
    // Render the real root directly: no NativeTick or previous-frame geometry
    // may prepare its initial scale. No account settings or server are opened.
    auto Root=Widget->RebuildWidget();FWidgetRenderer Renderer(false,true);
    for(const auto Size:TArray<FIntPoint>{{1600,1000},{800,600},{2560,1440},{1280,800}})
    for(float Dpi:{1.f,2.f})
    {
        auto Scaled=SNew(SDPIScaler).DPIScale(Dpi)[Root];
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Nearest,false);
        ++GFrameCounter;Renderer.DrawWidget(Target,Scaled,FVector2D(Size),0);FlushRenderingCommands();
        const float Expected=FMath::Clamp(FMath::Min(Size.X/Dpi/1600.f,Size.Y/Dpi/1000.f),.1f,2.5f);
        TestTrue(TEXT("First frame uses the current viewport, including DPI"),FMath::IsNearlyEqual(Widget->ResponsiveScale->GetUserSpecifiedScale(),Expected,.001f));
        const auto Geometry=Widget->DashboardSize->GetCachedGeometry();
        TestTrue(TEXT("Lobby stage fills its intended size on the first frame"),Geometry.GetAbsoluteSize().Equals(FVector2D(1600,1000)*Expected*Dpi,2));
        TestTrue(TEXT("First frame is centered rather than confined to the upper-left quadrant"),Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f).Equals(FVector2D(Size)*.5f,2));
        Target->ReleaseResource();
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELootHoverLayoutTest,"ACE.LootQuality.HoverLayout",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELootHoverLayoutTest::RunTest(const FString&)
{
    if(!FApp::CanEverRender()||!FSlateApplication::IsInitialized())return false;
    auto* Owner=NewObject<UBorder>();FACEWorldObject Item;Item.Name=TEXT("Stable hover sword");Item.ItemType=ACEItemType::MeleeWeapon;
    FACEQualityReview R;R.bEquipment=R.bIdentified=R.bRated=true;R.Class=TEXT("Heavy Weapons");R.Grade=ACELootScoring::Grade::Rare;
    R.CurrentStats={{TEXT("Damage"),TEXT("Damage per hit"),TEXT("42–65")},{TEXT("Defense"),TEXT("Melee defense"),TEXT("+18%")}};
    R.CurrentStats.Add({TEXT("MissileDefense"),TEXT("Missile defense"),TEXT("+0%")});
    R.CurrentStats.Add({TEXT("MagicDefense"),TEXT("Magic defense"),TEXT("+0.0%")});
    R.CurrentStats.Add({TEXT("Speed"),TEXT("Speed"),TEXT("0")});
    ACELootPresentation::Tooltip(Owner,Item.Name,Item,R,nullptr);
    auto* Tip=CastChecked<UACEHoverTooltipWidget>(Owner->GetToolTip());auto Root=Tip->TakeWidget();Root->SlatePrepass();
    auto Panel=UACEHoverTooltipWidget::GetWidgetContentTooltip(Owner);const auto Content=Panel->GetChildren()->GetChildAt(0);
    const auto Height=Root->GetDesiredSize().Y;
    for(int32 I=0;I<100;++I)
    {
        ACELootPresentation::Tooltip(Owner,Item.Name,Item,R,nullptr);
        TestTrue(TEXT("Stationary polling retains the text tree and its measured height"),Panel->GetChildren()->GetChildAt(0)==Content&&Root->GetDesiredSize().Y==Height);
    }
    FWidgetRenderer Renderer(false,true);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(380,400),TF_Nearest,false);
    ON_SCOPE_EXIT{Target->ReleaseResource();};
    for(int32 I=0;I<3;++I)
    {
        Root->SlatePrepass(); // The real appraisal refresh arrives after this.
        R.CurrentStats[0].Value=I%2?TEXT("52–81"):TEXT("42–65");
        R.CurrentStats[0].bSpellModified=I%2!=0;R.CurrentStats[0].Color=I%2?FLinearColor::Green:FLinearColor::White;
        R.CurrentStats[0].SpellContribution=I%2?TEXT(" (+10–16 spells)"):TEXT("");
        R.SpellNames=I%2?TArray<FString>{TEXT("Legendary Blood Thirst"),TEXT("Legendary Defender")}:TArray<FString>{};
        R.SpellGrades.Reset();for(const auto& Name:R.SpellNames)R.SpellGrades.Add(Name,ACELootScoring::Grade::Legendary);
        ACELootPresentation::Tooltip(Owner,Item.Name,Item,R,nullptr);
        TestTrue(TEXT("Changed appraisal retains the same hover panel"),UACEHoverTooltipWidget::GetWidgetContentTooltip(Owner)==Panel);
        TestTrue(TEXT("Changed rows are measured before the next prepass"),Root->GetDesiredSize().Y>80);
        ++GFrameCounter;Renderer.DrawWidget(Target,Root,FVector2D(380,400),.016f);FlushRenderingCommands();
        TArray<FSlateRect> Labels,Values;FString Copy;int32 SpellBlocks=0;
        TFunction<void(TSharedRef<SWidget>)> Read=[&](TSharedRef<SWidget> Node)
        {
            if(Node->GetType()==TEXT("STextBlock"))
            {
                const auto Text=StaticCastSharedRef<STextBlock>(Node);const FString Value=Text->GetText().ToString();Copy+=Value;
                if(Node->GetTag()==FName(TEXT("ACELoot.Name")))TestTrue(TEXT("Item names stay neutral regardless of rarity"),Text->GetColorAndOpacity().GetSpecifiedColor().Equals(FLinearColor(.95,.92,.85)));
                if(Node->GetTag()==FName(TEXT("ACELoot.Value.Damage"))||Node->GetTag()==FName(TEXT("ACELoot.Value.Defense")))Values.Add(Text->GetCachedGeometry().GetLayoutBoundingRect());
                if(Value==TEXT("Damage per hit:")||Value==TEXT("Melee defense:"))
                {
                    const auto& G=Text->GetCachedGeometry();Labels.Add(G.GetLayoutBoundingRect());
                    TestTrue(TEXT("Post-prepass updates retain full text-row height"),G.GetLocalSize().Y>=11);
                }
            }
            if(Node->GetType()==TEXT("SRichTextBlock"))
            {
                ++SpellBlocks;const auto Rich=StaticCastSharedRef<SRichTextBlock>(Node);Copy+=Rich->GetText().ToString();
                TestTrue(TEXT("Relevant spells have individual quality-color runs in one inline text flow"),Rich->GetText().ToString().Contains(TEXT("<q5>Legendary Blood Thirst</> · <q5>Legendary Defender</>")));
            }
            auto* Children=Node->GetChildren();for(int32 J=0;J<Children->Num();++J)Read(Children->GetChildAt(J));
        };Read(Root);
        TestTrue(TEXT("Text rows cannot overlap during refresh"),Labels.Num()==2&&Labels[1].Top>=Labels[0].Bottom);
        TestTrue(TEXT("Stat value columns share the same right edge"),Values.Num()==2&&FMath::IsNearlyEqual(Values[0].Right,Values[1].Right,.1f));
        TestEqual(TEXT("Retained content updates active spell values"),Copy.Contains(TEXT("(+10–16 spells)")),I%2!=0);
        TestEqual(TEXT("Retained content updates spell names"),Copy.Contains(TEXT("Legendary Defender")),I%2!=0);
        TestEqual(TEXT("Spells use one whitespace-wrapping text block"),SpellBlocks,R.SpellNames.IsEmpty()?0:1);
        TestFalse(TEXT("Zero-percent defense bonuses are hidden"),Copy.Contains(TEXT("Missile defense"))||Copy.Contains(TEXT("Magic defense")));
        TestTrue(TEXT("A meaningful zero speed remains visible"),Copy.Contains(TEXT("Speed:")));
    }
    UACEHoverTooltipWidget::SetWidgetTooltip(Owner,FText::FromString(TEXT("Ordinary item")));
    TestFalse(TEXT("Reused ordinary-item slots discard equipment content"),UACEHoverTooltipWidget::GetWidgetContentTooltip(Owner).IsValid());
    return !HasAnyErrors();
}
#endif
