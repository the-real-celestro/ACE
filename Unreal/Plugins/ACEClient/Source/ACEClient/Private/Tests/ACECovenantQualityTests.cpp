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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECovenantQualityTest,"ACE.LootQuality.Covenant",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACECovenantQualityTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(!Dat->LoadDatDirectory(ACETestDatDirectory()))return false;
    ON_SCOPE_EXIT{Dat->Deinitialize();};
    FACEWorldObject Item;Item.Guid=1931;Item.WeenieClassId=21152;Item.Name=TEXT("Covenant Breastplate");
    Item.IconId=100667354;Item.ItemType=ACEItemType::Armor;Item.ValidLocations=ACEEquipMask::ChestArmor;
    FACEAppraisalInfo A;A.bSuccess=true;A.ObjectGuid=Item.Guid;A.ItemType=Item.ItemType;
    A.IntProperties={{28,529},{158,2},{159,6},{160,410},{270,7},{271,1},{272,180},{171,0},{36,9999}};
    A.ArmorResistances={1.6f,1.6f,1.6f,1.6f,1.6f,1.6f,1,1.6f};
    FACEPlayerVitals C;C.bValid=true;C.Level=275;
    for(int32 Id:{6,34,14}){auto& S=C.Skills.AddDefaulted_GetRef();S.SkillId=Id;S.Base=S.Current=500;S.AdvancementClass=3;}
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
    const auto Naked=Review();TestTrue(TEXT("All seven native Covenant rolls use the Covenant model"),Naked.bRated&&Naked.Class==TEXT("Covenant Armor"));
    const TArray<int32> SelfSpells={4407,4391,4393,4397,4401,4403,4409,4412};
    A.SpellBookEntries=SelfSpells;auto R=Review();const auto Ready=R;
    TestTrue(TEXT("Maximum AL and complete ordinary self protection reaches Rare without cantrips"),R.bRated&&FMath::IsNearlyEqual(R.Points,80.,.001)&&R.Grade==ACELootScoring::Grade::Rare);
    TestTrue(TEXT("Ordinary built-in spells receive substantial Covenant protection credit"),R.Points>Naked.Points+30);
    A.SpellBookEntries={};A.ArmorResistances={1.3f,1.3f,1.3f,.6f,.6f,.6f,1,.6f};
    const auto Weak=Review();A.SpellBookEntries={4660};const auto EpicBane=Review();A.SpellBookEntries={1498};const auto NormalBane=Review();
    TestTrue(TEXT("A normal self Acid Bane VI can be worth much more than an Epic cantrip"),NormalBane.Points-Weak.Points>5*(EpicBane.Points-Weak.Points));
    A.SpellBookEntries={4407};const auto Impen=Review();A.SpellBookEntries={4407,1486};
    TestTrue(TEXT("Two ranks of ordinary Impenetrability do not stack"),FMath::IsNearlyEqual(Review().Points,Impen.Points,.001));
    A.ArmorResistances={2,2,2,2,2,2,1,2};A.IntProperties[171]=10;A.SpellBookEntries={4407};
    const auto Capped=Review();A.SpellBookEntries={4407,4660};R=Review();
    TestTrue(TEXT("A bane gets no extra credit once earned protection reaches the cap"),R.bRated&&FMath::IsNearlyEqual(R.Points,Capped.Points,.001));
    A.IntProperties[171]=0;A.ArmorResistances={1.6f,1.6f,1.6f,1.6f,1.6f,1.6f,1,1.6f};
    A.SpellBookEntries=SelfSpells;A.SpellBookEntries.Add(6095);R=Review();const double LegendaryImpenScore=R.Points;
    TestTrue(TEXT("Stacking item Impenetrability can exceed the ordinary protection benchmark"),R.Points>Ready.Points);
    TestTrue(TEXT("Inactive Impenetrability has a distinct blue projection"),R.Stats.ContainsByPredicate([](const auto& S){return S.Key==TEXT("Armor")&&S.CantripPreview.Contains(TEXT("+80 cantrip"));}));
    A.IntProperties.Add(265,14);A.SpellBookEntries.Add(4911);R=Review();Render(R,TEXT("LootQuality-covenant-potential.png"));
    const double FullScore=R.Points;
    A.IntProperties[28]=849;A.ArmorResistances={2,2,2,2,2,2,1,2};A.ArmorEnchantments=0x00ff00ff;
    for(int32 Id:SelfSpells)A.SpellBookEntries.Add(int32(0x80000000u|uint32(Id)));
    A.SpellBookEntries.Add(int32(0x80000000u|6095));R=Review();
    TestTrue(TEXT("Equipping the self-buffing item does not count its spells twice"),R.bRated&&FMath::IsNearlyEqual(R.Points,FullScore,.001));
    TestTrue(TEXT("A capped active bane does not invent an unbuffed zero resistance"),R.Stats.ContainsByPredicate([](const auto& S){return S.Key==TEXT("Armor18")&&!S.bOriginalKnown&&!S.bPermanentKnown;}));
    TestFalse(TEXT("Already-active protection cantrips have no additional blue projection"),R.Stats.ContainsByPredicate([](const auto& S){return !S.CantripPreview.IsEmpty();}));
    Render(R,TEXT("LootQuality-covenant-active.png"));
    A.IntProperties.Add(109,999);C.Skills[0].Base=200;C.Skills[0].Current=600;R=Review();
    TestTrue(TEXT("Unmet wield and Lore requirements retain Covenant quality"),R.bCannotWield&&R.bActivationBlocked&&FMath::IsNearlyEqual(R.Points,FullScore,.001));
    A.IntProperties[28]=769;A.SpellBookEntries.Reset();for(int32 Id:SelfSpells)A.SpellBookEntries.Add(int32(0x80000000u|uint32(Id)));
    TestFalse(TEXT("Unowned capped effects cannot masquerade as available self protection"),Review().bRated);
    C.Skills[0].Base=500;A.ArmorEnchantments=0;A.IntProperties.Remove(109);A.IntProperties.Remove(265);
    A.IntProperties[28]=419;A.IntProperties[160]=325;A.SpellBookEntries=SelfSpells;
    A.ArmorResistances={1.6f,1.6f,1.6f,1.6f,1.6f,1.6f,1,1.6f};Item.WeenieClassId=21158;Item.ValidLocations=ACEEquipMask::Shield;
    R=Review();TestTrue(TEXT("Covenant shields use their separate wield/AL bracket"),R.bRated&&R.Class==TEXT("Covenant Shield")&&FMath::IsNearlyEqual(R.Points,80.,.001));
    A.IntProperties[28]=529;TestFalse(TEXT("Armor-level maxima cannot be applied to Covenant shields"),Review().bRated);
    return !HasAnyErrors();
}
#endif
