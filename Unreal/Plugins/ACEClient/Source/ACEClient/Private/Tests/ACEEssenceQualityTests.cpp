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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEEssenceQualityTest,"ACE.LootQuality.Essences",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEEssenceQualityTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(!Dat->LoadDatDirectory(ACETestDatDirectory()))return false;
    ON_SCOPE_EXIT{Dat->Deinitialize();};
    // Public world identity and the exact 9/9/8/5 combination in Dustin's image.
    FACEWorldObject Item;Item.Guid=1948;Item.WeenieClassId=49330;Item.Name=TEXT("Incendiary Wisp Essence");
    Item.IconId=100693035;Item.ItemType=ACEItemType::Misc;Item.Structure=Item.MaxStructure=50;
    FACEAppraisalInfo A;A.ObjectGuid=Item.Guid;A.bSuccess=true;
    A.IntProperties={{266,49205},{366,54},{367,570},{368,54},{369,185},{370,9},{371,9},{374,8},{373,5},{105,9}};
    FACEPlayerVitals C;C.bValid=true;C.Level=275;C.StatQualityInts.Add(362,1);
    auto& Skill=C.Skills.AddDefaulted_GetRef();Skill.SkillId=54;Skill.Base=500;Skill.Current=600;Skill.AdvancementClass=3;
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
        TestTrue(TEXT("The six-roll essence panel remains compact"),Size.Y<400);
    };
    auto R=Review();const double Score=R.Points;
    TestTrue(TEXT("Unwieldable Misc essence is recognized before appraisal for background identification"),ACELootQuality::IsSummoningEssence(Item));
    TestTrue(TEXT("All six ratings contribute to a 31/120 combination"),R.bEquipment&&R.bRated&&FMath::IsNearlyEqual(Score,100.*31/120,.001));
    TestTrue(TEXT("The screenshot's low combination is Common"),R.Grade==ACELootScoring::Grade::Common);
    TestEqual(TEXT("Each roll has a verified comparison"),R.Stats.Num(),6);
    TestTrue(TEXT("Absent critical chance and critical damage resistance are visible zero rolls"),R.CurrentStats.ContainsByPredicate([](const auto& L){return L.Key==TEXT("Essence372")&&L.Value==TEXT("0");})
        &&R.CurrentStats.ContainsByPredicate([](const auto& L){return L.Key==TEXT("Essence375")&&L.Value==TEXT("0");}));
    TestTrue(TEXT("Summoning uses current skill and the separate specialized gate"),!R.bCannotWield&&!R.bActivationBlocked&&R.Requirements.Contains(TEXT("600 / 570")));
    TestEqual(TEXT("World pet profile supplies Fire damage"),R.DamageType,FString(TEXT("Fire")));
    Render(R,TEXT("LootQuality-essence-current.png"));
    A.IntProperties[105]=1;Item.Structure=1;
    TestTrue(TEXT("Workmanship and remaining charges cannot inflate roll quality"),FMath::IsNearlyEqual(Review().Points,Score,.001));
    Item.Structure=0;R=Review();TestTrue(TEXT("An empty essence retains quality but is unavailable"),R.bActivationBlocked&&FMath::IsNearlyEqual(R.Points,Score,.001));
    Item.Structure=50;C.Skills[0].Current=569;R=Review();TestTrue(TEXT("Summoning shortfall retains quality"),R.bCannotWield&&R.bRated);
    C.Skills[0].Current=600;C.Skills[0].AdvancementClass=2;
    TestTrue(TEXT("Level-200 essence requires specialized Summoning"),Review().bCannotWield);
    C.Skills[0].AdvancementClass=3;C.Level=184;
    TestTrue(TEXT("User level is distinct from the pet's own level"),Review().bCannotWield);
    C.Level=275;C.StatQualityInts[362]=2;
    TestTrue(TEXT("Wrong summoning mastery is unavailable"),Review().bActivationBlocked);
    C.StatQualityInts[362]=1;A.IntProperties.Add(109,100);
    TestTrue(TEXT("Custom Arcane Lore requirements are respected"),Review().bActivationBlocked);A.IntProperties.Remove(109);
    for(int32 I=0;I<6;++I)A.IntProperties.Add(370+I,20);
    R=Review();TestTrue(TEXT("Simultaneously maximum rolls reach Legendary 100"),R.bRated&&R.Points==100&&R.Grade==ACELootScoring::Grade::Legendary);
    Render(R,TEXT("LootQuality-essence-perfect.png"));
    for(int32 I=0;I<6;++I)A.IntProperties[370+I]=18;
    R=Review();TestTrue(TEXT("A 108/120 elite combination reaches Epic"),R.bRated&&R.Points==90&&R.Grade==ACELootScoring::Grade::Epic);
    for(int32 I=0;I<6;++I)A.IntProperties.Remove(370+I);
    TestTrue(TEXT("A fully identified essence with no ratings is the legal zero endpoint"),Review().bRated&&Review().Points==0);
    A.IntProperties.Add(370,21);TestFalse(TEXT("Unsupported server-specific ratings are not silently clamped"),Review().bRated);
    A.IntProperties[370]=-1;TestFalse(TEXT("Negative generated ratings are not valid"),Review().bRated);A.IntProperties.Remove(370);
    A.bSuccess=false;R=Review();TestTrue(TEXT("Unidentified essences are never fabricated as zero-quality rolls"),R.bEquipment&&!R.bRated&&!R.bIdentified);
    A.bSuccess=true;A.IntProperties.Remove(266);A.IntProperties.Remove(366);A.IntProperties.Remove(367);A.IntProperties.Remove(368);A.IntProperties.Remove(369);
    // One actual world template for every pet level. Requirement properties can
    // be absent in sparse packets; the verified template supplies these gates.
    const int32 WCIDs[]={49324,49325,49326,49327,49328,49329,49330};
    const int32 PetLevels[]={50,80,100,125,150,180,200},Skills[]={310,370,400,430,475,530,570};
    for(int32 I=0;I<7;++I)
    {
        Item.WeenieClassId=WCIDs[I];R=Review();
        TestTrue(TEXT("Verified lower brackets compare their full 0..120 range"),R.bRated&&R.Points==0&&R.Class.Contains(FString::FromInt(PetLevels[I]))&&R.Requirements.Contains(FString::FromInt(Skills[I])));
    }
    Item.WeenieClassId=1;TestFalse(TEXT("Arbitrary Misc objects do not acquire essence grades"),Review().bEquipment);
    return !HasAnyErrors();
}
#endif
