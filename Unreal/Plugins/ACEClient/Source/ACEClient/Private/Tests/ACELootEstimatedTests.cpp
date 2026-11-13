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
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELootEstimatedTest,"ACE.LootQuality.EstimatedItems",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELootEstimatedTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
    ON_SCOPE_EXIT{Dat->Deinitialize();};
    FACEWorldObject Item;Item.Guid=169;Item.WeenieClassId=3884;Item.Name=TEXT("Tinkered Fire Long Sword");Item.IconId=0x06001661;
    Item.ItemType=ACEItemType::MeleeWeapon;Item.ValidLocations=ACEEquipMask::MeleeWeapon;Item.Value=12000;
    FACEAppraisalInfo A;A.ObjectGuid=Item.Guid;A.bSuccess=A.bHasWeaponProfile=true;A.WeaponSkill=44;
    A.Damage=65;A.DamageVariance=.47;A.WeaponOffense=1.08;A.DamageType=16;
    A.IntProperties={{158,2},{159,44},{160,400},{171,0},{105,7},{353,2},{19,12000}};A.FloatProperties={{29,1.08}};
    FACEPlayerVitals C;C.bValid=true;
    auto Review=[&]{return ACELootQuality::Evaluate(Item,A,C,{},Dat);};
    const auto Exact=Review();TestTrue(TEXT("Fresh original is exact"),Exact.bRated&&!Exact.bEstimated&&Exact.bOriginalKnown);
    A.WeaponSkill=11;TestTrue(TEXT("Legacy sword profile still uses the modern heavy wield bracket"),Review().bRated);A.WeaponSkill=44;
    A.IntProperties[171]=3;A.Damage=67;A.DamageVariance=.376;A.SpellBookEntries={6089};
    auto R=Review();TestTrue(TEXT("Unknown three-tinker history is rated and marked estimated"),R.bRated&&R.bEstimated&&!R.bOriginalKnown);
    const auto* Damage=R.Stats.FindByPredicate([](const auto& S){return S.Key==TEXT("Damage");});
    TestTrue(TEXT("Two iron and one granite is a coherent legal estimate"),Damage&&Damage->bOriginalEstimated&&FMath::IsNearlyEqual(Damage->Original,65.)&&FMath::IsNearlyEqual(Damage->Permanent,67.));
    TestTrue(TEXT("Cantrip remains separate from inferred tinkering"),Damage&&FMath::IsNearlyEqual(Damage->WithCantrip,77.));
    FACEQualityOriginal Saved;TestFalse(TEXT("Estimated originals cannot be captured as observed history"),ACELootQuality::CaptureOriginal(Item,A,{},Dat,Saved));
    TestTrue(TEXT("Chat shares the rating and per-hit interval"),ACELootQuality::ChatSummary(Item,R).Contains(TEXT("[" )+ACELootQuality::Name(R.Grade).ToUpper()+TEXT("]"))&&ACELootQuality::ChatSummary(Item,R).Contains(TEXT("Fire"))&&ACELootQuality::ChatSummary(Item,R).Contains(TEXT("estimated")));
    if(FApp::CanEverRender()&&FSlateApplication::IsInitialized())
    {
        auto* Resources=NewObject<UACEUIResourceResolver>(GI);Resources->Initialize(Dat);auto* Owner=NewObject<UBorder>(GI);
        ACELootPresentation::Tooltip(Owner,Item.Name,Item,R,Resources);
        auto W=CastChecked<UACEHoverTooltipWidget>(Owner->GetToolTip())->TakeWidget();W->SlatePrepass();
        const FIntPoint Size(FMath::CeilToInt(W->GetDesiredSize().X),FMath::CeilToInt(W->GetDesiredSize().Y));
        FWidgetRenderer Renderer(false,true);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Nearest,false);
        ++GFrameCounter;Renderer.DrawWidget(Target,W,FVector2D(Size),.016f);FlushRenderingCommands();TArray<FColor> Pixels;
        FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        for(auto& Pixel:Pixels)Pixel=Pixel.ReinterpretAsLinear().ToFColor(true);TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("LootQuality-estimated.png")));Target->ReleaseResource();
    }
    Item.WeenieClassId=999999;R=Review();TestTrue(TEXT("Uncatalogued loot template uses explicit comparable class estimate"),R.bRated&&R.bEstimated);
    Item.WeenieClassId=3884;A.Damage=100;R=Review();TestTrue(TEXT("Unreconstructable tinkered weapon still has a current-stat estimate"),R.bRated&&R.bEstimated&&!R.bOriginalKnown);
    A.IntProperties[171]=0;TestFalse(TEXT("Impossible untouched roll remains outside verified bounds"),Review().bRated);
    Item.WeenieClassId=2366;Item.ItemType=ACEItemType::Caster;A=FACEAppraisalInfo();A.bSuccess=true;A.IntProperties={{171,0}};A.FloatProperties={{29,1.15},{144,.10}};
    R=Review();TestTrue(TEXT("Perfect no-wield utility caster has a full-scale rating"),R.bRated&&!R.bEstimated&&FMath::IsNearlyEqual(R.Points,100.));
    A.FloatProperties={{29,1.0},{144,0}};R=Review();TestTrue(TEXT("Minimum utility caster is Poor"),R.bRated&&R.Points==0);
    Item.WeenieClassId=29259;A.IntProperties={{158,2},{159,34},{160,375},{171,2}};A.FloatProperties={{152,1.18},{29,1.2},{144,.09}};
    R=Review();TestTrue(TEXT("Tinkered elemental caster gets an estimated original"),R.bRated&&R.bEstimated&&!R.bOriginalKnown);
    Item.ItemType=ACEItemType::Jewelry;Item.ValidLocations=0x100;A=FACEAppraisalInfo();A.bSuccess=true;
    R=Review();TestTrue(TEXT("Empty jewelry package is Poor instead of Unrated"),R.bRated&&R.Grade==ACELootScoring::Grade::Poor&&R.Points==0);
    Item.ItemType=ACEItemType::Clothing;Item.ValidLocations=ACEEquipMask::ChestWear;Item.ClothingPriority=ACECoverageMask::UnderwearChest;
    R=Review();TestTrue(TEXT("Plain underclothes still get a package rating"),R.bRated&&R.Grade==ACELootScoring::Grade::Poor);
    return !HasAnyErrors();
}
#endif
