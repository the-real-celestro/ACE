#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACELootQuality.h"
#include "ACEDatSubsystem.h"
#include "ACETestDatDirectory.h"
#include "UI/ACELootPresentation.h"
#include "UI/ACEAppraisalFormatting.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEHoverTooltipWidget.h"
#include "Components/Border.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Text/STextBlock.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELootSpecialItemTest,"ACE.LootQuality.SpecialItems",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACELootSpecialItemTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
    // Public world template 51989 plus the reflected totals in the reported
    // appraisal. Fixed quest stats are not a random-loot generation cohort.
    FACEWorldObject Item;Item.Guid=159;Item.WeenieClassId=51989;Item.Name=TEXT("Rynthid Tentacle Wand");
    Item.ItemType=ACEItemType::Caster;Item.IconId=100693234;
    FACEAppraisalInfo A;A.ObjectGuid=Item.Guid;A.bSuccess=true;A.ItemType=Item.ItemType;
    A.IntProperties={{33,1},{45,16},{106,475},{107,2762},{108,3000},{114,1},{158,2},{159,34},{160,375},{166,19}};
    A.FloatProperties={{29,1.4},{138,1.6},{144,.36},{147,.30},{152,1.24}};
    A.SpellBookEntries={3964,4227,4400,4414,6075,int32(0x80000000u|4400),int32(0x80000000u|4414)};
    A.WeaponEnchantments=2;A.ResistanceEnchantments=0x2000;
    FACEPlayerVitals C;C.bValid=true;auto& War=C.Skills.AddDefaulted_GetRef();War.SkillId=34;War.Base=War.Current=391;War.AdvancementClass=3;
    auto R=ACELootQuality::Evaluate(Item,A,C,{},Dat);
    TestTrue(TEXT("Quest caster is identified equipment with actual stats"),R.bEquipment&&R.bIdentified&&!R.CurrentStats.IsEmpty());
    TestFalse(TEXT("Fixed quest weapon is not assigned a fabricated loot-roll rating"),R.bRated);
    TestEqual(TEXT("Quest caster identifies its actual skill"),R.Class,FString(TEXT("War Magic")));
    auto Stat=[&](const TCHAR* Key){const auto* S=R.CurrentStats.FindByPredicate([&](const FACEItemStatLine& L){return L.Key==Key;});return S?S->Value:FString();};
    TestEqual(TEXT("Defense matches the reflected Examine total"),Stat(TEXT("Defense")),FString(TEXT("+40%")));
    TestEqual(TEXT("Mana conversion is displayed for uncatalogued weapons"),Stat(TEXT("ManaConversion")),FString(TEXT("+36%")));
    TestEqual(TEXT("Spell damage against monsters matches Examine"),Stat(TEXT("Element")),FString(TEXT("+24%")));
    TestEqual(TEXT("Spell damage against players matches Examine"),Stat(TEXT("ElementPvP")),FString(TEXT("+12%")));
    TestTrue(TEXT("Special-item modification stages do not invent roll bounds"),!R.Stats.IsEmpty()&&!R.Stats.ContainsByPredicate([](const FACEQualityStat& S){return S.Bounds.Variable();}));
    TestEqual(TEXT("Critical chance is not dropped by rating eligibility"),Stat(TEXT("Critical")),FString(TEXT("30%")));
    TestEqual(TEXT("Slayer modifier is displayed"),Stat(TEXT("Slayer")),FString(TEXT("×1.6")));
    TestTrue(TEXT("Shared special properties include attunement and slayer"),R.Properties.Contains(TEXT("Bonded"))&&R.Properties.Contains(TEXT("Attuned"))&&R.Properties.Contains(TEXT("Virindi slayer")));
    TestEqual(TEXT("Regular auras and cantrips are retained without active-marker duplicates"),R.SpellNames.Num(),5);
    const FString Examine=ACEAppraisalFormatting::ItemDetails(A,Dat);
    TestTrue(TEXT("Examine reports matching defense and spell totals"),Examine.Contains(TEXT("+40.0%"))&&Examine.Contains(TEXT("+24.0%"))&&Examine.Contains(TEXT("+12.0%")));
    if(FApp::CanEverRender()&&FSlateApplication::IsInitialized())
    {
        auto* Resources=NewObject<UACEUIResourceResolver>(GI);Resources->Initialize(Dat);auto* Owner=NewObject<UBorder>(GI);
        ACELootPresentation::Tooltip(Owner,Item.Name,Item,R,Resources);
        auto Widget=CastChecked<UACEHoverTooltipWidget>(Owner->GetToolTip())->TakeWidget();Widget->SlatePrepass();
        FString Copy;bool Slider=false;
        TFunction<void(TSharedRef<SWidget>)> Read=[&](TSharedRef<SWidget> W)
        {
            if(W->GetType()==TEXT("STextBlock"))Copy+=StaticCastSharedRef<STextBlock>(W)->GetText().ToString()+TEXT(" ");
            Slider|=W->GetType()==TEXT("SACELootBar");
            auto* Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)Read(Children->GetChildAt(I));
        };Read(Widget);
        TestTrue(TEXT("Quest hover contains actual stats and all intrinsic spell names"),Copy.Contains(TEXT("+40%"))&&Copy.Contains(TEXT("+36%"))&&Copy.Contains(TEXT("30%"))&&Copy.Contains(TEXT("Defender"))&&Copy.Contains(TEXT("Spirit Drinker")));
        TestFalse(TEXT("Quest hover has no sliders or uncatalogued placeholder"),Slider||Copy.Contains(TEXT("Uncatalogued"))||Copy.Contains(TEXT("Unrated")));
        const FIntPoint Size(FMath::CeilToInt(Widget->GetDesiredSize().X),FMath::CeilToInt(Widget->GetDesiredSize().Y));
        AddInfo(FString::Printf(TEXT("Quest hover: %d × %d"),Size.X,Size.Y));
        TestTrue(TEXT("Quest panel stays compact"),Size.X<=380&&Size.Y<=540);
        FWidgetRenderer Renderer(false,true);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Nearest,false);
        ++GFrameCounter;Renderer.DrawWidget(Target,Widget,FVector2D(Size),.016f);FlushRenderingCommands();
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        for(auto& Pixel:Pixels)Pixel=Pixel.ReinterpretAsLinear().ToFColor(true);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("LootQuality-quest-hover.png")));Target->ReleaseResource();
    }
    // Missing and non-finite values never become invented zero-valued stats.
    A.FloatProperties.Remove(144);A.FloatProperties.Add(149,std::numeric_limits<double>::quiet_NaN());
    R=ACELootQuality::Evaluate(Item,A,C,{},Dat);
    TestFalse(TEXT("Absent mana conversion is omitted"),R.CurrentStats.ContainsByPredicate([](const FACEItemStatLine& L){return L.Key==TEXT("ManaConversion");}));
    TestFalse(TEXT("Non-finite defense is omitted"),R.CurrentStats.ContainsByPredicate([](const FACEItemStatLine& L){return L.Key==TEXT("MissileDefense");}));
    return !HasAnyErrors();
}
#endif
