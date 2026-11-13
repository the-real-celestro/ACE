#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/App.h"
#include "ACELootQuality.h"
#include "ACEDatSubsystem.h"
#include "ACETestDatDirectory.h"
#include "ACEHoverTooltipWidget.h"
#include "UI/ACELootPresentation.h"
#include "UI/ACEUIResourceResolver.h"
#include "Components/Border.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/SWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEHealthRatingQualityTest,"ACE.LootQuality.HealthRatings",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEHealthRatingQualityTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(!Dat->LoadDatDirectory(ACETestDatDirectory()))return false;
    ON_SCOPE_EXIT{Dat->Deinitialize();};
    FACEWorldObject Item;Item.Guid=2016;Item.WeenieClassId=297;Item.Name=TEXT("Ring");
    Item.IconId=100668662;Item.ItemType=ACEItemType::Jewelry;Item.ValidLocations=0xc0000;
    FACEAppraisalInfo A;A.bSuccess=true;A.ObjectGuid=Item.Guid;
    A.IntProperties={{158,7},{159,1},{160,180},{171,0}};A.SpellBookEntries={2524}; // Major Magic Resistance
    FACEPlayerVitals C;C.bValid=true;C.Level=275;
    for(int32 Id:{14,15,34}){auto& S=C.Skills.AddDefaulted_GetRef();S.SkillId=Id;S.Base=S.Current=500;S.AdvancementClass=3;}
    auto Review=[&](){return ACELootQuality::Evaluate(Item,A,C,{},Dat);};
    auto Render=[&](const FACEQualityReview& R,const TCHAR* Filename)
    {
        if(!FApp::CanEverRender())return;
        auto* Owner=NewObject<UBorder>(GI);auto* Resources=NewObject<UACEUIResourceResolver>(GI);Resources->Initialize(Dat);
        ACELootPresentation::Tooltip(Owner,Item.Name,Item,R,Resources);
        auto Widget=CastChecked<UACEHoverTooltipWidget>(Owner->GetToolTip())->TakeWidget();Widget->SlatePrepass();
        const FVector2D Size=Widget->GetDesiredSize();FWidgetRenderer Renderer(false,true);
        auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Nearest,false);
        ++GFrameCounter;Renderer.DrawWidget(Target,Widget,Size,.016f);FlushRenderingCommands();
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        for(auto& Pixel:Pixels)Pixel=Pixel.ReinterpretAsLinear().ToFColor(true);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(FMath::CeilToInt(Size.X),FMath::CeilToInt(Size.Y),Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/Filename));Target->ReleaseResource();
    };
    const auto Base=Review();TestTrue(TEXT("The comparison ring has a useful Major defense cantrip"),Base.bRated&&FMath::IsNearlyEqual(Base.Points,46.75,.001));
    for(int32 Key:{376,379})
    {
        const double Credits[]={0,Key==379?12.5:11.25,Key==379?30.:27.,Key==379?50.:45.};
        for(int32 Rating=1;Rating<=3;++Rating)
        {
            A.IntProperties.Add(Key,Rating);const auto R=Review();
            TestTrue(TEXT("Both health rating types receive the full independent survival credit"),R.bRated&&FMath::IsNearlyEqual(R.Points-Base.Points,Credits[Rating],.001));
            TestTrue(TEXT("The score explanation includes its native health contribution exactly once"),R.Spells.ContainsByPredicate([&](const auto& S){return S.Status==TEXT("Permanent gear property")&&FMath::IsNearlyEqual(S.Points,Credits[Rating],.001);}));
        }
        const auto Max=Review();TestTrue(TEXT("A best normal health roll and a useful Major cantrip form an Epic item"),Max.Grade==ACELootScoring::Grade::Epic);
        A.IntProperties[Key]=0;TestTrue(TEXT("Zero native health does not earn points"),FMath::IsNearlyEqual(Review().Points,Base.Points,.001));
        A.IntProperties[Key]=-1;TestTrue(TEXT("Negative native health does not earn a bonus"),FMath::IsNearlyEqual(Review().Points,Base.Points,.001));
        A.IntProperties.Remove(Key);
    }
    A.IntProperties.Add(379,3);Render(Review(),TEXT("LootQuality-health-major.png"));
    A.SpellBookEntries={4704}; // Epic Magic Resistance
    auto R=Review();const auto Epic=R;
    TestTrue(TEXT("Epic combat defense plus maximum health is exceptional Legendary"),R.bRated&&FMath::IsNearlyEqual(R.Points,129.9,.001)&&R.Grade==ACELootScoring::Grade::Legendary);
    TestTrue(TEXT("Health is displayed as a native property, without a fabricated active-spell delta"),R.CurrentStats.ContainsByPredicate([](const auto& L){return L.Label==TEXT("Maximum health rating")&&L.Value==TEXT("+3")&&!L.bSpellModified;}));
    Render(R,TEXT("LootQuality-health-epic.png"));
    A.IntProperties.Remove(379);A.IntProperties.Add(376,3);R=Review();
    TestTrue(TEXT("Healing Boost receives the strong independent quality weight while remaining distinct"),FMath::IsNearlyEqual(R.Points,Epic.Points-5,.001)&&R.CurrentStats.ContainsByPredicate([](const auto& L){return L.Label==TEXT("Healing boost")&&L.Value==TEXT("+3");}));
    Render(R,TEXT("LootQuality-healing-epic.png"));
    A.IntProperties.Add(379,3);R=Review();TestTrue(TEXT("Actual items with both native benefits retain both contributions"),FMath::IsNearlyEqual(R.Points,174.9,.001));
    C.Level=100;A.IntProperties.Add(109,999);R=Review();
    TestTrue(TEXT("Wield and Lore failure do not erase valuable health ratings"),R.bCannotWield&&R.bActivationBlocked&&FMath::IsNearlyEqual(R.Points,174.9,.001));
    C.Level=275;A.IntProperties.Remove(109);A.IntProperties.Remove(376);A.IntProperties.Remove(379);A.IntProperties.Add(370,3);
    TestTrue(TEXT("Unrelated gear ratings keep their existing weight"),FMath::IsNearlyEqual(Review().Points,79.9+45.,.001));
    A.IntProperties.Remove(370);A.IntProperties.Add(379,3);A.SpellBookEntries.Reset();R=Review();
    TestTrue(TEXT("Native health can rate an otherwise spell-free wearable"),R.bRated&&R.Points==50);
    return !HasAnyErrors();
}
#endif
