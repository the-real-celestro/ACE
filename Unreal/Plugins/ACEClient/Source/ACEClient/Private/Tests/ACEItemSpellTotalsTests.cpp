#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACELootQuality.h"
#include "ACETestDatDirectory.h"
#include "UI/ACEAppraisalFormatting.h"
#include "UI/ACELootPresentation.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEItemSpellTotalsTest,"ACE.RetailParity.ItemSpellTotals",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEItemSpellTotalsTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
    ON_SCOPE_EXIT{Dat->Deinitialize();};
    FACEAppraisalInfo Bow;Bow.bSuccess=Bow.bHasWeaponProfile=true;Bow.ItemType=ACEItemType::MissileWeapon;
    Bow.Damage=18;Bow.WeaponTime=0;Bow.WeaponOffense=1;Bow.WeaponDamageMod=2.1;Bow.WeaponSkill=47;
    Bow.IntProperties={{44,2},{49,40},{50,1}};Bow.FloatProperties={{29,1.21},{63,2.1}};
    Bow.WeaponEnchantments=0x000e000e;
    Bow.SpellBookEntries={int32(0x80000000u|1615),int32(0x80000000u|1626),int32(0x80000000u|4400)};
    auto Rows=ACEAppraisalFormatting::EquipmentStatLines(Bow,Dat);
    auto Find=[&](const TCHAR* Key)->const FACEItemStatLine&
    {const auto* R=Rows.FindByPredicate([&](const FACEItemStatLine& L){return L.Key==Key;});check(R);return *R;};
    TestEqual(TEXT("Reflected bow damage remains the final total"),Find(TEXT("Damage")).Value,FString(TEXT("+18")));
    TestEqual(TEXT("Bow reports only the actual spell addition"),Find(TEXT("Damage")).SpellContribution,FString(TEXT(" (+16 spells)")));
    TestEqual(TEXT("Speed clamping reports the effective change, not the nominal -50"),Find(TEXT("Speed")).SpellContribution,FString(TEXT(" (-40 spells)")));
    TestEqual(TEXT("Ordinary Defender is included"),Find(TEXT("Defense")).SpellContribution,FString(TEXT(" (+20% spells)")));
    for(const TCHAR* Key:{TEXT("Damage"),TEXT("Speed"),TEXT("Defense")})
        TestTrue(TEXT("Active improvements are green"),Find(Key).bSpellModified&&Find(Key).Color==FLinearColor::Green);
    TestFalse(TEXT("Unchanged bow multiplier stays unhighlighted"),Find(TEXT("Multiplier")).bSpellModified);
    const FString Examine=ACEAppraisalFormatting::ItemDetails(Bow,Dat);
    TestTrue(TEXT("Examine puts the spell amount beside final damage"),Examine.Contains(TEXT("Damage Bonus: 18 (+16 spells)")));
    TestTrue(TEXT("Examine uses the same speed and defense contributions"),Examine.Contains(TEXT("Speed: Very Fast (0) (-40 spells)"))
        &&Examine.Contains(TEXT("+21.0% (+20% spells)")));
    const auto Colors=ACEAppraisalFormatting::ItemTextColors(Bow,Examine,Dat);
    for(const TCHAR* Prefix:{TEXT("Damage Bonus:"),TEXT("Speed:"),TEXT("Bonus to Melee Defense:")})
    {
        const int32 Start=Examine.Find(Prefix);
        if(TestTrue(TEXT("Examine stat exists"),Start!=INDEX_NONE))
            for(int32 I=Start;I<Examine.Len()&&Examine[I]!='\n';++I)TestEqual(TEXT("Whole improved line is green"),Colors[I],FLinearColor::Green);
    }
    auto Unknown=Bow;Unknown.IntProperties.Remove(44);Unknown.IntProperties.Remove(49);
    Unknown.SpellBookEntries={int32(0x80000000u|999999)};
    Rows=ACEAppraisalFormatting::EquipmentStatLines(Unknown,Dat);
    TestTrue(TEXT("Unknown effects preserve the total and highlight without guessing additions"),Find(TEXT("Damage")).Value==TEXT("+18")
        &&Find(TEXT("Damage")).bSpellModified&&Find(TEXT("Damage")).SpellContribution.IsEmpty());
    TestTrue(TEXT("A clamped speed without a raw value has no invented unbuffed speed"),Find(TEXT("Speed")).SpellContribution.IsEmpty());
    auto Unbuffed=Bow;Unbuffed.Damage=2;Unbuffed.WeaponTime=40;Unbuffed.FloatProperties[29]=1.01;
    Unbuffed.WeaponEnchantments=0;Unbuffed.SpellBookEntries={1615,1626,4400};
    Rows=ACEAppraisalFormatting::EquipmentStatLines(Unbuffed,Dat);
    TestFalse(TEXT("Innate inactive spells do not turn totals green"),Rows.ContainsByPredicate([](const auto& R){return R.bSpellModified||!R.SpellContribution.IsEmpty();}));
    auto Harm=Unbuffed;Harm.Damage=1;Harm.WeaponEnchantments=8;
    Rows=ACEAppraisalFormatting::EquipmentStatLines(Harm,Dat);
    TestTrue(TEXT("A harmful active effect remains red with its actual loss"),Find(TEXT("Damage")).Color.R>.9f
        &&Find(TEXT("Damage")).Color.G<.3f&&Find(TEXT("Damage")).SpellContribution==TEXT(" (-1 spells)"));
    auto Stacked=Bow;Stacked.SpellBookEntries.Add(int32(0x80000000u|1615));Stacked.SpellBookEntries.Add(int32(0x80000000u|1614));
    Rows=ACEAppraisalFormatting::EquipmentStatLines(Stacked,Dat);
    TestEqual(TEXT("Duplicate and weaker same-category spells are not added twice"),Find(TEXT("Damage")).SpellContribution,FString(TEXT(" (+16 spells)")));

    FACEWorldObject Item;Item.Guid=160;Item.WeenieClassId=3884;Item.Name=TEXT("Frost Long Sword");
    Item.ItemType=ACEItemType::MeleeWeapon;Item.IconId=0x06001661;Item.ValidLocations=ACEEquipMask::MeleeWeapon;
    FACEAppraisalInfo A;A.bSuccess=A.bHasWeaponProfile=true;A.ObjectGuid=Item.Guid;A.ItemType=Item.ItemType;
    A.Damage=91;A.DamageVariance=.376;A.WeaponTime=20;A.WeaponOffense=1.08;A.WeaponSkill=44;A.DamageType=8;
    A.IntProperties={{44,65},{49,70},{158,2},{159,44},{160,400},{171,2}};A.FloatProperties={{22,.376},{62,1.08},{29,1.41}};
    A.WeaponEnchantments=0x000e000e;
    A.SpellBookEntries={6089,6091,int32(0x80000000u|6089),int32(0x80000000u|6091),
        int32(0x80000000u|1615),int32(0x80000000u|1626),int32(0x80000000u|4400)};
    FACEPlayerVitals Character;Character.bValid=true;auto& Skill=Character.Skills.AddDefaulted_GetRef();
    Skill.SkillId=44;Skill.Base=Skill.Current=500;Skill.AdvancementClass=3;
    FACEQualityOriginal Original;Original.WCID=Item.WeenieClassId;Original.Icon=Item.IconId;Original.Name=Item.Name;
    Original.Stats={{TEXT("Damage"),63},{TEXT("Variance"),.47},{TEXT("Attack"),1.08},{TEXT("Defense"),1.12}};
    const auto Review=ACELootQuality::Evaluate(Item,A,Character,{},Dat,&Original);
    Rows=Review.CurrentStats;
    TestEqual(TEXT("Per-hit final damage is not a potential weapon-roll range"),Find(TEXT("Damage")).Value,FString(TEXT("56.8–91")));
    TestEqual(TEXT("Both endpoints show the combined active spell change"),Find(TEXT("Damage")).SpellContribution,FString(TEXT(" (+16.2–26 spells)")));
    TestEqual(TEXT("Ordinary Defender and Legendary Defender stack in the reported total"),Find(TEXT("Defense")).SpellContribution,FString(TEXT(" (+29% spells)")));
    TestTrue(TEXT("Already applied cantrips are not presented as an additional blue preview"),Review.Stats.ContainsByPredicate([](const auto& S){return S.Key==TEXT("Damage")&&!S.bPotential;}));
    TestTrue(TEXT("Tinkering remains separate from active spells"),Review.Stats[0].bOriginalKnown
        &&FMath::IsNearlyEqual(Review.Stats[0].Original,63.)&&FMath::IsNearlyEqual(Review.Stats[0].Permanent,65.));
    FACEAppraisalInfo Caster;Caster.bSuccess=true;Caster.ItemType=ACEItemType::Caster;Caster.FloatProperties={{29,1.4},{152,1.24},{144,.36}};
    Caster.IntProperties={{45,16}};Caster.SpellBookEntries={int32(0x80000000u|4400),int32(0x80000000u|4414),int32(0x80000000u|1479)};
    Rows=ACEAppraisalFormatting::EquipmentStatLines(Caster,Dat);
    TestEqual(TEXT("Caster monster damage shows its active addition"),Find(TEXT("Element")).SpellContribution,FString(TEXT(" (+8% spells)")));
    TestEqual(TEXT("Caster player damage has half the PvE addition"),Find(TEXT("ElementPvP")).SpellContribution,FString(TEXT(" (+4% spells)")));
    TestEqual(TEXT("Multiplicative mana conversion reports the actual percentage-point addition"),Find(TEXT("ManaConversion")).SpellContribution,FString(TEXT(" (+12% spells)")));
    if(FApp::CanEverRender()&&FSlateApplication::IsInitialized())
    {
        auto* Resources=NewObject<UACEUIResourceResolver>(GI);Resources->Initialize(Dat);auto* Owner=NewObject<UBorder>(GI);
        ACELootPresentation::Tooltip(Owner,Item.Name,Item,Review,Resources);
        auto Widget=CastChecked<UACEHoverTooltipWidget>(Owner->GetToolTip())->TakeWidget();Widget->SlatePrepass();
        int32 GreenValues=0;FString Copy;
        TFunction<void(TSharedRef<SWidget>)> Read=[&](TSharedRef<SWidget> W)
        {
            if(W->GetType()==TEXT("STextBlock"))
            {
                const auto Text=StaticCastSharedRef<STextBlock>(W);const FString Value=Text->GetText().ToString();Copy+=Value+TEXT(" ");
                if(Value.Contains(TEXT(" spells)"))&&Text->GetColorAndOpacity().GetSpecifiedColor()==FLinearColor::Green)++GreenValues;
            }
            auto* Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)Read(Children->GetChildAt(I));
        };Read(Widget);
        TestTrue(TEXT("Rendered hover uses green final values and inline additions"),GreenValues>=3&&Copy.Contains(TEXT("56.8–91 (+16.2–26 spells)")));
        TestFalse(TEXT("Active cantrip is not duplicated as a blue bonus"),Copy.Contains(TEXT("  Cantrip +")));
        const FIntPoint Size(FMath::CeilToInt(Widget->GetDesiredSize().X),FMath::CeilToInt(Widget->GetDesiredSize().Y));
        TestTrue(TEXT("Spell totals remain compact"),Size.X<=380&&Size.Y<=540);
        FWidgetRenderer Renderer(false,true);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Nearest,false);
        ++GFrameCounter;Renderer.DrawWidget(Target,Widget,FVector2D(Size),.016f);FlushRenderingCommands();
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        for(auto& Pixel:Pixels)Pixel=Pixel.ReinterpretAsLinear().ToFColor(true);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("LootQuality-active-spells.png")));Target->ReleaseResource();
    }
    return !HasAnyErrors();
}
#endif
