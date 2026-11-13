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
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SRichTextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEHoverSpellContextTest,"ACE.LootQuality.SpellContext",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEHoverSpellContextTest::RunTest(const FString&)
{
    using ACELootScoring::Grade;
    auto* GI=NewObject<UGameInstance>();auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if(!Dat->LoadDatDirectory(ACETestDatDirectory()))return false;
    ON_SCOPE_EXIT{Dat->Deinitialize();};
    auto* Owner=NewObject<UBorder>(GI);auto* Resources=NewObject<UACEUIResourceResolver>(GI);Resources->Initialize(Dat);
    FACEWorldObject Item;Item.Guid=2112;Item.WeenieClassId=29259;Item.Name=TEXT("War Caster");Item.ItemType=ACEItemType::Caster;Item.IconId=0x06001578;
    FACEAppraisalInfo A;A.bSuccess=true;A.ObjectGuid=Item.Guid;A.DamageType=16;
    A.IntProperties={{158,2},{159,34},{160,385},{171,0},{105,7},{19,19388},{109,350}};
    A.FloatProperties={{152,1.18},{29,1.20},{144,.09}};A.SpellBookEntries={6075,6091,3964,2597,6045,6098};
    FACEPlayerVitals C;C.bValid=true;C.Level=275;
    for(int32 Id:{34,14,15,6,39}){auto& S=C.Skills.AddDefaulted_GetRef();S.SkillId=Id;S.Base=500;S.Current=Id==14?378:500;S.AdvancementClass=3;}
    auto Review=[&](){return ACELootQuality::Evaluate(Item,A,C,{},Dat);};
    auto SpellName=[&](int32 Id){FString Name;uint32 Icon=0;Dat->TryGetSpellInfo(Id,Name,Icon);return Name;};
    auto GradeFor=[&](const FACEQualityReview& R,int32 Id){const auto* G=R.SpellGrades.Find(SpellName(Id));return G?int32(*G):-1;};
    auto Render=[&](const FACEQualityReview& R,const TCHAR* Filename)
    {
        ACELootPresentation::Tooltip(Owner,Item.Name,Item,R,Resources);
        auto Widget=CastChecked<UACEHoverTooltipWidget>(Owner->GetToolTip())->TakeWidget();Widget->SlatePrepass();
        if(!FApp::CanEverRender())return;
        const FVector2D Size=Widget->GetDesiredSize();FWidgetRenderer Renderer(false,true);
        auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Nearest,false);
        ++GFrameCounter;Renderer.DrawWidget(Target,Widget,Size,.016f);FlushRenderingCommands();
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        for(auto& Pixel:Pixels)Pixel=Pixel.ReinterpretAsLinear().ToFColor(true);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(FMath::CeilToInt(Size.X),FMath::CeilToInt(Size.Y),Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/Filename));Target->ReleaseResource();
        int32 Inline=0;TArray<FSlateRect> Values;FString Plain;
        TFunction<void(TSharedRef<SWidget>)> Visit=[&](TSharedRef<SWidget> Node)
        {
            if(Node->GetType()==TEXT("SRichTextBlock"))
            {
                ++Inline;const auto Rich=StaticCastSharedRef<SRichTextBlock>(Node);const FString Markup=Rich->GetText().ToString();
                TestFalse(TEXT("Spell wrapping has no forced per-spell newline"),Markup.Contains(TEXT("\n")));
                TestTrue(TEXT("Every named spell is represented in the same text flow"),Markup.Contains(SpellName(A.SpellBookEntries[0])));
            }
            if(Node->GetType()==TEXT("STextBlock"))
            {
                const auto Text=StaticCastSharedRef<STextBlock>(Node);Plain+=Text->GetText().ToString()+TEXT(" ");
                if(Node->GetTag()==FName(TEXT("ACELoot.Name")))TestTrue(TEXT("Item name uses neutral ivory"),Text->GetColorAndOpacity().GetSpecifiedColor().Equals(FLinearColor(.95,.92,.85)));
                if(Node->GetTag().ToString().StartsWith(TEXT("ACELoot.Value.")))Values.Add(Text->GetCachedGeometry().GetLayoutBoundingRect());
            }
            auto* Children=Node->GetChildren();for(int32 I=0;I<Children->Num();++I)Visit(Children->GetChildAt(I));
        };Visit(Widget);
        TestEqual(TEXT("The spell list is one rich text block"),Inline,R.SpellNames.IsEmpty()?0:1);
        for(int32 I=1;I<Values.Num();++I)TestTrue(TEXT("All stat values end at the same right edge"),FMath::IsNearlyEqual(Values[0].Right,Values[I].Right,.1f));
        TestTrue(TEXT("Type, Value and Workmanship remain visible"),Plain.Contains(TEXT("Type:"))&&Plain.Contains(TEXT("Value:"))&&Plain.Contains(TEXT("Workmanship:")));
        TestTrue(TEXT("Requirements have explicit required and player values"),Plain.Contains(TEXT("Requirements"))&&Plain.Contains(TEXT("Arcane Lore 350"))&&Plain.Contains(TEXT("You: ")));
    };
    auto R=Review();TestTrue(TEXT("The real caster fixture has a verified quality"),R.bRated);
    TestEqual(TEXT("Legendary War Magic is Legendary on its matching wand"),GradeFor(R,6075),int32(Grade::Legendary));
    TestEqual(TEXT("Defender remains very useful without implying a perfect item"),GradeFor(R,6091),int32(Grade::Epic));
    TestEqual(TEXT("Legendary Spirit Thirst has full usefulness on an elemental caster"),GradeFor(R,6098),int32(Grade::Legendary));
    TestEqual(TEXT("Epic Focus receives Epic color for this mage"),GradeFor(R,3964),int32(Grade::Epic));
    TestEqual(TEXT("Minor Acid Bane is Poor on this caster"),GradeFor(R,2597),int32(Grade::Poor));
    TestEqual(TEXT("Cooking with a supporting Epic attribute is useful without being Legendary"),GradeFor(R,6045),int32(Grade::Rare));
    TestTrue(TEXT("Workmanship and actual item value are transmitted into the hover"),R.CurrentStats.ContainsByPredicate([](const auto& S){return S.Key==TEXT("Work")&&S.Value==TEXT("7");})&&R.CurrentStats.ContainsByPredicate([](const auto& S){return S.Key==TEXT("Value")&&S.Value==FText::AsNumber(19388).ToString();}));
    Render(R,TEXT("LootQuality-hybrid-inline.png"));
    for(auto& S:C.Skills)if(S.SkillId==14)S.Current=300;
    R=Review();TestTrue(TEXT("An unmet Lore row does not mark the satisfied wield row as failed"),R.Checks.ContainsByPredicate([](const auto& Q){return Q.Label==TEXT("Arcane Lore 350")&&!Q.bMet;})&&R.Checks.ContainsByPredicate([](const auto& Q){return Q.Label.Contains(TEXT("War Magic 385"))&&Q.bMet;}));
    Render(R,TEXT("LootQuality-hybrid-lore-blocked.png"));
    for(auto& S:C.Skills)if(S.SkillId==14)S.Current=378;
    Item.WeenieClassId=999999;Item.Name=TEXT("Armor with unverified original rolls");Item.ItemType=ACEItemType::Armor;Item.ValidLocations=ACEEquipMask::ChestArmor;Item.IconId=0x06000FCD;
    A.IntProperties.Add(28,300);A.FloatProperties.Reset();A.SpellBookEntries={6045};R=Review();
    TestTrue(TEXT("Useful crafting receives a disclosed package rating without inventing armor rolls"),R.bRated);
    Render(R,TEXT("LootQuality-hybrid-unrated-armor.png"));
    TArray<uint32> Attributes;TestTrue(TEXT("Cooking synergy uses the actual DAT formula"),Dat->TryGetSkillAttributes(39,Attributes)&&Attributes.Contains(4)&&Attributes.Contains(5));
    A.SpellBookEntries={6045,6103,6105};R=Review();const auto Craft=R;
    TestEqual(TEXT("Trained Cooking plus both Legendary supporting attributes is a valuable crafting combination"),GradeFor(R,6045),int32(Grade::Legendary));
    TestEqual(TEXT("Coordination gets crafting relevance even on a mage's wearable"),GradeFor(R,6103),int32(Grade::Legendary));
    Render(R,TEXT("LootQuality-hybrid-crafting.png"));
    const auto Panel=UACEHoverTooltipWidget::GetWidgetContentTooltip(Owner);const auto Content=Panel->GetChildren()->GetChildAt(0);
    for(auto& S:C.Skills)if(S.SkillId==39)S.AdvancementClass=1;
    R=Review();TestTrue(TEXT("Untrained crafting does not receive the same Legendary usefulness"),GradeFor(R,6045)<int32(Grade::Legendary));
    TestTrue(TEXT("A trained coherent crafting package earns more quality credit"),R.Points<Craft.Points);
    ACELootPresentation::Tooltip(Owner,Item.Name,Item,R,Resources);
    TestTrue(TEXT("A color-only context change updates content without replacing the active popup"),UACEHoverTooltipWidget::GetWidgetContentTooltip(Owner)==Panel&&Panel->GetChildren()->GetChildAt(0)!=Content);
    return !HasAnyErrors();
}
#endif
