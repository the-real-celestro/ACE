#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACELootQuality.h"
#include "ACELootQualitySubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "ACETestDatDirectory.h"
#include "UI/ACELootPresentation.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEHoverTooltipWidget.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Widgets/IToolTip.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Components/Border.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELootQualityTest,"ACE.LootQuality.Scoring",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELootQualityTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
    ON_SCOPE_EXIT{Dat->Deinitialize();};
    TArray<FString> RangeErrors;const int32 Checked=ACELootQuality::ValidateWeaponBrackets(Dat,RangeErrors);
    for(const auto& Error:RangeErrors)AddError(Error);
    TestTrue(TEXT("All supported weapon brackets have matching zero/best endpoints"),RangeErrors.IsEmpty()&&Checked>=600);
    AddInfo(FString::Printf(TEXT("Validated %d class/wield endpoint fixtures"),Checked));
    FACEWorldObject Item;Item.Guid=1234;Item.Name=TEXT("Frost Long Sword");Item.WeenieClassId=3884;Item.IconId=0x06001661;
    Item.ItemType=ACEItemType::MeleeWeapon;Item.ValidLocations=ACEEquipMask::MeleeWeapon;
    FACEAppraisalInfo A;A.ObjectGuid=Item.Guid;A.bSuccess=true;A.bHasWeaponProfile=true;A.WeaponSkill=44;
    A.Damage=65;A.DamageVariance=.47;A.WeaponOffense=1.08;A.DamageType=8;
    A.IntProperties={{158,2},{159,44},{160,400},{171,0}};A.FloatProperties.Add(29,1.08);
    FACEPlayerVitals C;C.bValid=true;C.Level=275;
    for(int32 Id:{44,14,6,15}){auto& S=C.Skills.AddDefaulted_GetRef();S.SkillId=Id;S.Base=500;S.Current=500;S.AdvancementClass=3;}
    TArray<FACEActiveEnchantment> Active;
    auto Review=[&](const FACEQualityOriginal* Original=nullptr){return ACELootQuality::Evaluate(Item,A,C,Active,Dat,Original);};
    auto R=Review();TestTrue(TEXT("Catalogued heavy sword is rated"),R.bRated);
    TestEqual(TEXT("Best damage with lowest bonuses is Rare"),int32(R.Grade),int32(ACELootScoring::Grade::Rare));
    TestTrue(TEXT("Perfect damage earns 85"),FMath::IsNearlyEqual(R.Points,85.,1.e-4));
    A.SpellBookEntries={6089,6072};R=Review();
    TestTrue(TEXT("Blood Thirst and matching aptitude reach Legendary"),FMath::IsNearlyEqual(R.Points,98.,1.e-4));
    A.SpellBookEntries={6089,2598};R=Review();
    TestTrue(TEXT("Repeated Blood Thirst ranks are not added"),FMath::IsNearlyEqual(R.CantripPoints,8.,1.e-4));
    A.SpellBookEntries={6045};R=Review();TestTrue(TEXT("Legendary Cooking grants no combat points"),FMath::IsNearlyZero(R.CantripPoints));
    A.Damage=63;A.DamageVariance=.5;A.WeaponOffense=1.16;A.FloatProperties[29]=1.2;
    A.SpellBookEntries={6091,6055,6063};R=Review();
    TestTrue(TEXT("Defensive package raises a good sword to Epic"),R.Points>90&&R.Points<97);
    TestTrue(TEXT("Defender and both defense skills receive separate credit"),FMath::IsNearlyEqual(R.CantripPoints,16.,1.e-4));
    A.Damage=75;A.DamageVariance=.47;A.WeaponOffense=1.17;A.FloatProperties[29]=1.17;
    A.SpellBookEntries={6089,6094,6091,int32(0x80000000u|6089),int32(0x80000000u|6094),int32(0x80000000u|6091)};
    R=Review();
    TestTrue(TEXT("Reflected cantrips are removed from original rolls before scoring"),R.bRated&&FMath::IsNearlyEqual(R.OriginalPoints,85.,1.e-3));
    FACEQualityOriginal Original;TestTrue(TEXT("Untinkered original snapshot is captured"),ACELootQuality::CaptureOriginal(Item,A,Active,Dat,Original));
    TestTrue(TEXT("Saved original damage excludes Blood Thirst"),FMath::IsNearlyEqual(Original.Stats.FindRef(TEXT("Damage")),65.,1.e-4));
    A.IntProperties[171]=3;A.Damage=67;A.DamageVariance=.376;A.WeaponOffense=1.08;A.FloatProperties[29]=1.08;A.SpellBookEntries={6089};
    R=Review(&Original);TestTrue(TEXT("Observed original survives tinkering"),R.bRated&&FMath::IsNearlyEqual(R.Stats[0].Original,65.,1.e-4));
    TestTrue(TEXT("Tinkering and cantrip stages stay distinct"),FMath::IsNearlyEqual(R.Stats[0].Permanent,67.,1.e-4)&&FMath::IsNearlyEqual(R.Stats[0].WithCantrip,77.,1.e-4));
    if(FApp::CanEverRender()&&FSlateApplication::IsInitialized())
    {
        auto* Resources=NewObject<UACEUIResourceResolver>(GI);Resources->Initialize(Dat);
        auto* Owner=NewObject<UBorder>(GI);
        ACELootPresentation::Tooltip(Owner,Item.Name,Item,R,Resources);
        auto SlateOwner=Owner->TakeWidget();
        const auto InitialTooltip=SlateOwner->GetToolTip();
        const auto InitialContent=InitialTooltip->AsWidget();
        for(int32 Refresh=0;Refresh<100;++Refresh)
            ACELootPresentation::Tooltip(Owner,Item.Name,Item,R,Resources);
        TestTrue(TEXT("Stationary hover retains Slate popup identity across 100 inventory refreshes"),SlateOwner->GetToolTip()==InitialTooltip);
        TestTrue(TEXT("Stationary hover retains content root"),InitialTooltip->AsWidget()==InitialContent);
        auto Widget=CastChecked<UACEHoverTooltipWidget>(Owner->GetToolTip())->TakeWidget();Widget->SlatePrepass();
        TArray<FString> Labels;bool HasSlider=false;
        TFunction<void(TSharedRef<SWidget>)> Collect=[&](TSharedRef<SWidget> Node)
        {
            HasSlider|=Node->GetType()==FName(TEXT("SACELootBar"));
            if(Node->GetType()==TEXT("STextBlock"))Labels.Add(StaticCastSharedRef<STextBlock>(Node)->GetText().ToString());
            auto* Children=Node->GetChildren();for(int32 I=0;I<Children->Num();++I)Collect(Children->GetChildAt(I));
        };
        Collect(Widget);
        const FString Copy=FString::Join(Labels,TEXT(" "));
        TestTrue(TEXT("Examine-style lines retain current values and concise provenance"),Copy.Contains(TEXT("Damage per hit:"))&&Copy.Contains(TEXT("Base 65"))&&Copy.Contains(TEXT("Tinkering +2"))&&Copy.Contains(TEXT("Cantrip +10")));
        TestFalse(TEXT("No slider widget remains in the hover"),HasSlider);
        for(const TCHAR* Extra:{TEXT("quality points"),TEXT("Original per hit"),TEXT("Wield requirements met"),TEXT("Ivory:"),TEXT("compressed extension"),TEXT("ACE baseline"),TEXT("No tinkering")})
            TestFalse(FString(TEXT("No repetitive explanation: "))+Extra,Copy.Contains(Extra));
        TestTrue(TEXT("Compact panel keeps current, original and cantrip contributions"),Copy.Contains(TEXT("65"))&&Copy.Contains(TEXT("67"))&&Copy.Contains(TEXT("Cantrip +10")));
        TestTrue(TEXT("Natural roll limits use concise inline text"),Copy.Contains(TEXT("base max +20%"))&&Copy.Contains(TEXT("base best 47%")));
        const FIntPoint Size(FMath::CeilToInt(Widget->GetDesiredSize().X),FMath::CeilToInt(Widget->GetDesiredSize().Y));
        TestTrue(TEXT("Single-cantrip hover is compact"),Size.X<=380&&Size.Y<=380);
        FWidgetRenderer Renderer(false,true);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Nearest,false);
        ++GFrameCounter;Renderer.DrawWidget(Target,Widget,FVector2D(Size),.016f);FlushRenderingCommands();
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        for(auto& Pixel:Pixels)Pixel=Pixel.ReinterpretAsLinear().ToFColor(true);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("LootQuality-hover.png")));Target->ReleaseResource();
        A.SpellBookEntries={6089,6091,6055,6063};const auto Four=Review(&Original);A.SpellBookEntries={6089};
        ACELootPresentation::Tooltip(Owner,Item.Name,Item,Four,Resources);
        TestTrue(TEXT("New appraisal updates an open tooltip without closing it"),SlateOwner->GetToolTip()==InitialTooltip);
        TestFalse(TEXT("Loot popup cannot steal mouse hover hit tests"),Owner->GetToolTip()->GetVisibility()==ESlateVisibility::Visible);
        auto FourWidget=CastChecked<UACEHoverTooltipWidget>(Owner->GetToolTip())->TakeWidget();FourWidget->SlatePrepass();
        AddInfo(FString::Printf(TEXT("Four-cantrip hover size: %.0f x %.0f"),FourWidget->GetDesiredSize().X,FourWidget->GetDesiredSize().Y));
        TestTrue(TEXT("Four-cantrip hover remains compact"),FourWidget->GetDesiredSize().Y<=440);
        UACEHoverTooltipWidget::SetWidgetContentTooltip(Owner,FText::FromString(TEXT("Renamed item")),SNew(STextBlock).Text(FText::FromString(TEXT("Changed"))));
        TestTrue(TEXT("Renaming an item retains its active popup"),SlateOwner->GetToolTip()==InitialTooltip);
        UACEHoverTooltipWidget::SetWidgetTooltip(Owner,FText::FromString(TEXT("Ordinary item")));
        TestTrue(TEXT("Reused equipment slot can switch to ordinary tooltip"),SlateOwner->GetToolTip()!=InitialTooltip);
    }
    TestFalse(TEXT("Tinker count alone cannot reconstruct an original"),Review().bRated);
    A.IntProperties[171]=0;A.Damage=65;A.DamageVariance=.47;A.IntProperties.Add(109,1000);R=Review();
    TestTrue(TEXT("Lore gates activation separately from wielding"),R.bActivationBlocked&&!R.bCannotWield&&R.bRated);
    for(auto& S:C.Skills)if(S.SkillId==44){S.Base=200;S.Current=600;}
    R=Review();TestTrue(TEXT("Current buffs cannot satisfy a raw-skill wield requirement"),R.bCannotWield);
    TestTrue(TEXT("Unwieldable future items retain quality"),R.bRated);
    A.Damage=99;TestFalse(TEXT("Custom out-of-range rolls do not receive a fabricated perfect score"),Review().bRated);
    Item.WeenieClassId=29259;Item.ItemType=ACEItemType::Caster;Item.Name=TEXT("War caster");
    A.bHasWeaponProfile=false;A.WeaponSkill=0;A.IntProperties[159]=34;A.IntProperties[160]=385;
    A.FloatProperties.Add(152,1.18);A.FloatProperties[29]=1.2;A.SpellBookEntries={6091};R=Review();
    TestTrue(TEXT("Max spell damage + max defense + Legendary Defender is exceptional Legendary"),R.bRated&&R.Points>=111.99);
    Item.ItemType=ACEItemType::Jewelry;Item.ValidLocations=0x100;A.SpellBookEntries={6107};R=Review();
    TestTrue(TEXT("Relevant Legendary attribute is highly valued on wearable gear"),R.bRated&&R.Points>=97);
    Item.WeenieClassId=25638;Item.Name=TEXT("Leather Breastplate");Item.ItemType=ACEItemType::Armor;
    Item.ValidLocations=ACEEquipMask::ChestArmor;
    A.IntProperties={{158,7},{159,1},{160,150},{171,0},{28,304}};A.FloatProperties.Empty();
    A.ArmorResistances={1,1,1,.5,.5,.5,0,.6};A.SpellBookEntries={6107};R=Review();
    TestTrue(TEXT("Armor has its own verified protection and spell model"),R.bRated&&R.Class==TEXT("Armor")&&R.Points>=80);
    const double KnownArmorScore=R.Points;
    A.ArmorResistances[7]=.1;R=Review();
    TestTrue(TEXT("An invalid resistance cannot be dropped to inflate armor quality"),R.Class.Contains(TEXT("spell package"))&&R.OriginalPoints==0&&R.Points<=KnownArmorScore);
    A.ArmorResistances[7]=.6;
    A.IntProperties.Remove(28);R=Review();
    TestTrue(TEXT("Missing AL cannot reclassify armor into a higher-scored clothing package"),R.Class.Contains(TEXT("Armor"))&&R.Points<=KnownArmorScore);
    A.IntProperties.Add(28,304);
    A.IntProperties[160]=180;A.IntProperties[28]=235;R=Review();
    TestTrue(TEXT("Legendary-promoted armor retains lower tier-eight AL support"),R.bRated);
    A.IntProperties[171]=2;R=Review();
    TestTrue(TEXT("Unknown original armor can still disclose spell-package quality"),R.bRated&&R.Class.Contains(TEXT("spell package")));
    TestTrue(TEXT("Losing original armor evidence cannot increase its quality score"),R.Points<=KnownArmorScore);
    TestFalse(TEXT("An original armor level is not fabricated after tinkering"),R.bOriginalKnown);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELootIconPreservationTest,"ACE.LootQuality.ImbuedBackground",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELootIconPreservationTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();GI->Init();ON_SCOPE_EXIT{GI->Shutdown();};
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(!TestTrue(TEXT("Retail DAT opens"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
    auto* Resources=NewObject<UACEUIResourceResolver>(GI);Resources->Initialize(Dat);
    auto* Canvas=NewObject<UACEUICanvasWidget>(GI);Canvas->Initialize();Canvas->SetResourceResolver(Resources);
    auto* Binder=NewObject<UACEUIGameplayBinder>(GI);Binder->Client=GI->GetSubsystem<UACEClientSubsystem>();Binder->Canvas=Canvas;
    auto* Loot=GI->GetSubsystem<UACELootQualitySubsystem>();
    FACEWorldObject Item;Item.Guid=101;Item.WeenieClassId=3884;Item.ItemType=ACEItemType::MeleeWeapon;Item.Name=TEXT("Future Heavy Sword");
    FACEAppraisalInfo A;A.ObjectGuid=101;A.bSuccess=true;A.bHasWeaponProfile=true;A.WeaponSkill=44;A.Damage=65;A.DamageVariance=.47;A.WeaponOffense=1.08;A.FloatProperties.Add(29,1.08);
    A.IntProperties={{158,2},{159,44},{160,400},{171,0}};Loot->Review(Item);Loot->ObserveAppraisal(A);
    if(!TestTrue(TEXT("Fixture requires a skill this character lacks"),Loot->Review(Item).bCannotWield))return false;
    auto* Border=NewObject<UBorder>(GI);
    Binder->SetItemSlotBackground(Border,&Item);
    TestFalse(TEXT("Ordinary unusable items keep the distinct background tint"),Border->GetBrushColor().Equals(FLinearColor::White));
    for(uint32 Effect:{0x20u,0x40u,0x80u,0x100u,0x200u,0x400u,0x800u,0x1000u})
    {
        Item.UiEffects=Effect;Binder->SetItemSlotBackground(Border,&Item);
        TestTrue(TEXT("Every imbue palette retains its original background color"),Border->GetBrushColor().Equals(FLinearColor::White));
        TestTrue(TEXT("Imbued background uses the original DAT texture"),Border->Background.GetResourceObject()==Resources->ResolveItemBackground(Item.ItemType,Item.IconUnderlayId));
    }
    Item.UiEffects=0;
    // RecipeManager.IconUnderlay: cold/electric/acid/armor rend, crippling,
    // critical strike, fire/bludgeon/pierce/slash rend.
    for(uint32 Underlay=0x06003353;Underlay<=0x0600335c;++Underlay)
    {
        Item.IconUnderlayId=Underlay;Binder->SetItemSlotBackground(Border,&Item);
        TestTrue(TEXT("Every authored imbue underlay is preserved without recoloring"),Border->GetBrushColor().Equals(FLinearColor::White));
        TestTrue(TEXT("Imbue underlay remains composed from its original DAT texture"),Border->Background.GetResourceObject()==Resources->ResolveItemBackground(Item.ItemType,Underlay));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELootQualityLifecycleTest,"ACE.LootQuality.InventoryRefresh",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELootQualityLifecycleTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();GI->Init();bool Shutdown=false;ON_SCOPE_EXIT{if(!Shutdown)GI->Shutdown();};
    auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto* Loot=GI->GetSubsystem<UACELootQualitySubsystem>();
    FACEWorldObject Item;Item.Guid=101;Item.Name=TEXT("Observed Heavy Sword");Item.WeenieClassId=3884;Item.ItemType=ACEItemType::MeleeWeapon;
    TestFalse(TEXT("Unidentified inventory is not assigned a fabricated grade"),Loot->Review(Item).bRated);
    FACEAppraisalInfo A;A.ObjectGuid=Item.Guid;A.bSuccess=true;A.bHasWeaponProfile=true;A.WeaponSkill=44;
    A.Damage=65;A.DamageVariance=.47;A.WeaponOffense=1.08;A.FloatProperties.Add(29,1.08);
    A.IntProperties={{158,2},{159,44},{160,400},{171,0}};
    Loot->ObserveAppraisal(A);TestTrue(TEXT("Appraisal immediately gives inventory a quality rating"),Loot->Review(Item).bRated);
    const uint64 Before=Loot->Revision;
    Client->OnItemRefreshed.Broadcast(Item.Guid);
    TestTrue(TEXT("Server item refresh invalidates the inventory presentation"),Loot->Revision>Before);
    TestFalse(TEXT("Old appraisal cannot survive a crafting refresh"),Loot->Review(Item).bRated);
    A.IntProperties[171]=3;A.Damage=67;A.DamageVariance=.376;Loot->ObserveAppraisal(A);
    const auto& R=Loot->Review(Item);
    TestTrue(TEXT("A post-tinker appraisal retains its real observed original"),R.bRated&&R.bOriginalKnown&&FMath::IsNearlyEqual(R.Stats[0].Original,65.,.001));
    TestTrue(TEXT("The permanent value updates after tinkering"),FMath::IsNearlyEqual(R.Stats[0].Permanent,67.,.001));
    Item.Name=TEXT("Different item using the same GUID");
    TestFalse(TEXT("Changed item identity cannot inherit an old original or quality"),Loot->Review(Item).bRated);
    GI->Shutdown();Shutdown=true;
    TestFalse(TEXT("Rating service stops ticking when its game instance shuts down"),Loot->IsTickable());
    return !HasAnyErrors();
}
#endif
