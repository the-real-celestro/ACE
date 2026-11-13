#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACELootQuality.h"
#include "ACEDatSubsystem.h"
#include "ACETestDatDirectory.h"
#include "Engine/GameInstance.h"
#include "UI/ACELootPresentation.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEHoverTooltipWidget.h"
#include "Components/Border.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELootUtilityRatingsTest,"ACE.LootQuality.UtilityRatings",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELootUtilityRatingsTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(!TestTrue(TEXT("DAT opens"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
    ON_SCOPE_EXIT{Dat->Deinitialize();};
    FACEWorldObject I;I.Guid=170;I.Name=TEXT("Silver Heavy Necklace");I.ItemType=ACEItemType::Jewelry;I.ValidLocations=0x100;I.IconId=0x0600150a;I.WeenieClassId=623;I.Value=15457;I.Burden=90;
    FACEAppraisalInfo A;A.ObjectGuid=I.Guid;A.bSuccess=true;A.IntProperties={{105,7},{19,15457},{158,7},{160,180},{109,288},{106,275},{107,1908},{108,2100}};
    A.SpellBookEntries={1094,6041};FACEPlayerVitals C;C.bValid=true;C.Level=253;
    for(int32 Id:{44,14,6,15,16,21,31,32,33,48,49,50,51,52}){auto& S=C.Skills.AddDefaulted_GetRef();S.SkillId=Id;S.Base=S.Current=Id==44?500:383;S.AdvancementClass=2;}
    auto Evaluate=[&]{return ACELootQuality::Evaluate(I,A,C,{},Dat);};
    auto R=Evaluate();TestTrue(TEXT("Reported Legendary Arcane amulet is Rare without a perfect score"),R.bRated&&R.Grade==ACELootScoring::Grade::Rare&&FMath::IsNearlyEqual(R.Points,75.65));
    FString Arcane;uint32 Icon=0;Dat->TryGetSpellInfo(6041,Arcane,Icon);
    TestTrue(TEXT("Spell color agrees with practical Arcane value"),R.SpellGrades.FindRef(Arcane)==ACELootScoring::Grade::Rare);
    if(FApp::CanEverRender()&&FSlateApplication::IsInitialized())
    {
        auto* Resources=NewObject<UACEUIResourceResolver>(GI);Resources->Initialize(Dat);auto* Owner=NewObject<UBorder>(GI);
        auto Render=[&](const FACEQualityReview& Review,const TCHAR* File)
        {
            ACELootPresentation::Tooltip(Owner,I.Name,I,Review,Resources);auto W=CastChecked<UACEHoverTooltipWidget>(Owner->GetToolTip())->TakeWidget();W->SlatePrepass();
            TSharedPtr<STextBlock> Badge;TSharedPtr<SBorder> Fill;
            TFunction<void(TSharedRef<SWidget>)> Find=[&](auto Node){if(Node->GetTag()==TEXT("ACELoot.QualityBadgeText"))Badge=StaticCastSharedRef<STextBlock>(Node);if(Node->GetTag()==TEXT("ACELoot.QualityBadge"))Fill=StaticCastSharedRef<SBorder>(Node);auto* Children=Node->GetChildren();for(int32 K=0;K<Children->Num();++K)Find(Children->GetChildAt(K));};Find(W);
            if(TestTrue(TEXT("Larger badge has distinct text and background"),Badge.IsValid()&&Fill.IsValid()))
            {
                const double L1=Badge->GetColorAndOpacity().GetSpecifiedColor().GetLuminance(),L2=Fill->GetBorderBackgroundColor().GetSpecifiedColor().GetLuminance();
                TestTrue(TEXT("Every quality badge exceeds 4.5:1 text contrast"),(FMath::Max(L1,L2)+.05)/(FMath::Min(L1,L2)+.05)>=4.5);
                TestTrue(TEXT("Rating is bold and at least 12 points"),Badge->GetFont().Size>=12&&Badge->GetFont().TypefaceFontName==TEXT("Bold"));
            }
            if(!File)return;const FVector2D Size=W->GetDesiredSize();FWidgetRenderer Renderer(false,true);auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Nearest,false);
            ++GFrameCounter;Renderer.DrawWidget(Target,W,Size,.016f);FlushRenderingCommands();TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
            for(auto& P:Pixels)P=P.ReinterpretAsLinear().ToFColor(true);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(FMath::CeilToInt(Size.X),FMath::CeilToInt(Size.Y),Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/File));Target->ReleaseResource();
        };
        Render(R,TEXT("LootQuality-arcane-rare.png"));
        for(int32 Grade=0;Grade<6;++Grade){auto Example=R;Example.Grade=static_cast<ACELootScoring::Grade>(Grade);Render(Example,nullptr);}
    }
    // The exact legacy IDs in retail must earn the same ratio as modern names.
    I.WeenieClassId=3884;I.ItemType=ACEItemType::MeleeWeapon;I.ValidLocations=ACEEquipMask::MeleeWeapon;
    A.IntProperties={{158,2},{159,44},{160,400},{171,0}};A.FloatProperties={{29,1.08}};A.bHasWeaponProfile=true;A.WeaponSkill=44;A.WeaponOffense=1.08;A.Damage=65;A.DamageVariance=.47;
    for(int32 SpellId:{2598,2586,4661,6089})
    {
        A.SpellBookEntries={SpellId};R=Evaluate();
        TestTrue(TEXT("Every Blood Thirst rank earns damage credit"),R.CantripPoints>0);
    }
    return !HasAnyErrors();
}
#endif
