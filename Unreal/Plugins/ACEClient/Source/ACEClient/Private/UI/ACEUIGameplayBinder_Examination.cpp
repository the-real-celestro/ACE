#include "ACEHoverTooltipWidget.h"
#include "ACERuntimeOptions.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIResourceResolver.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEAppraisalFormatting.h"
#include "ACEAppraisalKnowledge.h"
#include "Dat/ACEDatTextLayout.h"
#include "ACEDatSubsystem.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/NativeWidgetHost.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"

void UACEUIGameplayBinder::RefreshExaminationWikiLink(bool bShow)
{
    auto* Host=Client&&Client->GetGameInstance()?Client->GetGameInstance()->GetSubsystem<UACEPluginSubsystem>():nullptr;
    FACEWorldObject Subject;
    const bool HaveSubject=Client&&Client->GetWorldObject(LastAppraisal.ObjectGuid,Subject);
    const bool Player=HaveSubject&&Subject.bIsPlayer;
    bool Enabled=false;
    if(Host)for(const auto& P:Host->Plugins)if(P->Id==TEXT("wiki"))Enabled=P->Enabled;
    if(!bShow||!Enabled||Player||LastAppraisal.Name.IsEmpty()||!Canvas->WidgetTree)
    {if(ExamWikiLink)ExamWikiLink->SetVisibility(ESlateVisibility::Collapsed);return;}
    Host->CacheWikiExamination(LastAppraisal,HaveSubject?&Subject:nullptr);
    if(!ExamWikiLink)ExamWikiLink=Canvas->WidgetTree->ConstructWidget<UNativeWidgetHost>();
    if(ExamWikiName!=LastAppraisal.Name||bExamWikiIdentified!=LastAppraisal.bSuccess)
    {
        ExamWikiName=LastAppraisal.Name;bExamWikiIdentified=LastAppraisal.bSuccess;
        ExamWikiLink->SetContent(Host->MakeWikiLink(ExamWikiName,bExamWikiIdentified,
            [Weak=TWeakObjectPtr<UACEUIGameplayBinder>(this),Name=ExamWikiName]() -> UTextureRenderTarget2D*
            {
                auto* Binder=Weak.Get();
                return Binder&&Binder->LastAppraisal.Name==Name&&Binder->LastAppraisal.bIsCreature
                    &&Binder->ExamPaperDollAppliedGuid==Binder->LastAppraisal.ObjectGuid
                    ?Binder->ExamPaperDollRenderTarget.Get():nullptr;
            }));
    }
    if(!ExamWikiRegion)ExamWikiRegion=MakeShared<FACEUIElement>();
    const auto Description=Manager->FindElementUnder(TEXT("RootGameplay_FloatyExamination_Field"),
        LastAppraisal.bIsCreature?TEXT("BasicCreatureExam_ExtraInfo"):TEXT("ItemDisplayText"));
    if(!Description){ExamWikiLink->SetVisibility(ESlateVisibility::Collapsed);return;}
    ExamWikiRegion->Parent=Description;
    ExamWikiRegion->X=10;ExamWikiRegion->Y=FMath::Max(0,Description->Height-22);
    ExamWikiRegion->Width=FMath::Max(1,Description->Width-20);ExamWikiRegion->Height=18;
    ExamWikiLink->SetVisibility(ESlateVisibility::Visible);
    Canvas->PlaceWidgetAtElement(ExamWikiLink,ExamWikiRegion,100010);
}

bool UACEUIGameplayBinder::InspectSpellAt(FVector2D Point)
{
    if (!Canvas || !Client || !Manager) return false;
    const FVector2D Absolute = Canvas->GetCachedGeometry().LocalToAbsolute(Point);
    int32 Spell = 0;
    HitTestSpellbookRow(Absolute, Spell);
    for (int32 I=0; !Spell && I<SpellBarIcons.Num() && I<SpellBarSpellIds.Num(); ++I)
        if (Canvas->IsWidgetExposedAt(SpellBarIcons[I], Absolute)) Spell = SpellBarSpellIds[I];
    if (!Spell && BuiltInSpellIconBorders.Num() && Canvas->IsWidgetExposedAt(BuiltInSpellIconBorders[0], Absolute)) Spell = BuiltInSpellId;
    auto* Dat = Canvas->GetResourceResolver() ? Canvas->GetResourceResolver()->GetDatSubsystem() : nullptr;
    FString Name; uint32 Icon=0;
    if (!Spell || !Dat || !Dat->TryGetSpellInfo(Spell, Name, Icon)) return false;
    // Retail ExamineSpell ends the outstanding object appraisal cursor wait.
    if (auto Session=Client->GetSession()) Session->CancelCursorAppraisal();
    ExaminedSpellId = Spell; bExaminationDismissed = false;
    if (ExamScroll) ExamScroll->SetScrollOffset(0.f);
    ShowExamination(true); RefreshExaminationOverlay();
    return true;
}

void UACEUIGameplayBinder::RefreshSpellExamination()
{
    if (!Canvas || !Canvas->WidgetTree || !Manager || !Canvas->GetResourceResolver()) return;
    auto* Dat=Canvas->GetResourceResolver()->GetDatSubsystem();
    FString Name, Details; uint32 Icon=0;
    if (!Dat || !Dat->TryGetSpellInfo(ExaminedSpellId, Name, Icon)) return;
    Dat->TryGetSpellExamination(ExaminedSpellId, Details);
    while (ExamSpellLabels.Num()<2) ExamSpellLabels.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
    PlaceTextOnElement(ExamSpellLabels[0], TEXT("DisplayedNameText"), Name, 10, FLinearColor::White, 100001);
    FString School, RemainingDetails;
    Details.Split(TEXT("\n"), &School, &RemainingDetails);
    PlaceTextOnElement(ExamSpellLabels[1], TEXT("MagicSchoolText"), School, 10, FLinearColor::White, 100001);
    if (!ExamIconBorder) ExamIconBorder=Canvas->WidgetTree->ConstructWidget<UBorder>();
    SetIconDid(ExamIconBorder, Icon);
    ExamIconBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
    Canvas->PlaceWidgetAtElement(ExamIconBorder, Manager->FindElementUnder(TEXT("SpellExamineUI"), TEXT("SpellIcon")), 100001);
    // Details and ingredients share a responsive scroll area. Retire the
    // original fixed-height subdivisions instead of drawing them through text.
    for (const TCHAR* Field : {TEXT("SpellManaText"), TEXT("SpellDurationText"), TEXT("SpellRangeText"), TEXT("SpellFormulaText"), TEXT("SpellFormulaIconList"),
        TEXT("SpellExamBackground_Divider_Middle"),TEXT("SpellExamBackground_Divider_Lower")})
        if (auto Element=Manager->FindElementUnder(TEXT("SpellExamineUI"),Field)) Element->bVisible=false;
    const auto Body=Manager->FindElementUnder(TEXT("SpellExamineUI"), TEXT("SpellDisplayText"));
    if (!Body) return;
    const auto Panel=Manager->FindElementByName(TEXT("SpellExamineUI"));
    const auto Divider=Manager->FindElementUnder(TEXT("SpellExamineUI"),TEXT("SpellExamBackground_Divider"));
    if(Panel && Divider)
    {
        Body->Y=Divider->Y+Divider->Height+6;
        Body->Height=FMath::Max(32,Panel->Height-Body->Y-4);
        if(auto Background=Manager->FindElementUnder(TEXT("SpellExamineUI"),TEXT("SpellExamBackground")))Background->Height=Panel->Height;
        if(auto Scrollbar=Manager->FindElementUnder(TEXT("SpellExamineUI"),TEXT("SpellDisplayTextScrollbar")))
        {Scrollbar->Y=Body->Y;Scrollbar->Height=Body->Height;}
    }
    if (!ExamBody) ExamBody=Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
    ExamBody->SetText(FText::FromString(RemainingDetails));
    ExamBody->SetColorAndOpacity(FLinearColor::White); ExamBody->SetAutoWrapText(true);
    ExamBody->SetVisibility(ESlateVisibility::HitTestInvisible);
    if (auto* Retail=Cast<UACERetailTextBlock>(ExamBody))
	{
        Retail->SetRetailElement(Canvas->GetResourceResolver(), Body, Canvas->GetLastScale2D(), Body->Width, false,true);
		Retail->SetTextColors({});
	}
    if (!ExamScroll) ExamScroll=Canvas->WidgetTree->ConstructWidget<UScrollBox>();
    ExamScroll->SetClipping(EWidgetClipping::ClipToBounds);
    ExamScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
    ExamScroll->SetAnimateWheelScrolling(false);
    if (ExamBody->GetParent()!=ExamScroll) ExamScroll->AddChild(ExamBody);
    ExamScroll->SetVisibility(ESlateVisibility::Visible);
    Canvas->PlaceWidgetAtElement(ExamScroll, Body, 100001);
    const float MaxOffset = ExamScroll->GetScrollOffsetOfEnd();
    const auto Bar = Manager->FindElementUnder(TEXT("SpellExamineUI"),TEXT("SpellDisplayTextScrollbar"));
    if (Bar) Bar->bVisible = MaxOffset > .5f;
    SyncDatScrollbar(Bar, MaxOffset > 0.f ? ExamScroll->GetScrollOffset()/MaxOffset : 0.f,
        Body->Height/FMath::Max(1.f, MaxOffset+Body->Height));
}

void UACEUIGameplayBinder::RefreshCreatureExamination()
{
    if (!Manager || !Canvas || !Canvas->WidgetTree) return;
    const auto Root = Manager->FindElementUnder(TEXT("RootGameplay_FloatyExamination_Field"), TEXT("BasicCreatureExamineUI"));
    if (!Root) return;
    const bool bCharacter = ACEAppraisalFormatting::UsesCharacterExamination(LastAppraisal);
    auto Find = [&](const TCHAR* Name) { return Manager->FindElementUnder(TEXT("BasicCreatureExamineUI"), Name); };
    if (auto El = Find(TEXT("CreatureExam_Attributes"))) El->bVisible = !bCharacter;
    if (auto El = Find(TEXT("CharacterExam_Attributes"))) El->bVisible = bCharacter;
    while (ExamCreatureHeadings.Num() < 7)
        ExamCreatureHeadings.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
    for (UTextBlock* Text : ExamCreatureHeadings) Text->SetVisibility(ESlateVisibility::Collapsed);
    // 23000001/0990A1E2 and 07100DAC supply these English labels in the DAT.
    PlaceTextOnElement(ExamCreatureHeadings[0], Find(TEXT("LevelInfo_Character")), TEXT("Character"), 8, FLinearColor::White, 100001);
    PlaceTextOnElement(ExamCreatureHeadings[1], Find(TEXT("LevelInfo_Level")), TEXT("Level"), 8, FLinearColor::White, 100001);
    FACEWorldObject Subject;
    const bool bHaveSubject = Client && Client->GetWorldObject(LastAppraisal.ObjectGuid, Subject);
    const auto Headings = ACEAppraisalFormatting::CreatureHeadings(LastAppraisal, Canvas->GetResourceResolver(), bHaveSubject ? &Subject : nullptr);
    if (bCharacter)
    {
        PlaceTextOnElement(ExamCreatureHeadings[3], Find(TEXT("HeritageText")), Headings.Heritage, 9, FLinearColor::White, 100001);
        PlaceTextOnElement(ExamCreatureHeadings[4], Find(TEXT("ProfessionText")), Headings.Profession, 9, FLinearColor::White, 100001);
        PlaceTextOnElement(ExamCreatureHeadings[5], Find(TEXT("PlayerKillerText")), Headings.PlayerKiller, 9, FLinearColor::White, 100001);
    }
    else PlaceTextOnElement(ExamCreatureHeadings[2], Find(TEXT("CreatureName")), Headings.Type, 9, FLinearColor::White, 100001);
    PlaceTextOnElement(ExamCreatureHeadings[6], Find(TEXT("AllegianceNameText")), Headings.Allegiance, 9, FLinearColor::White, 100001);
    auto List = Manager->FindElementUnder(TEXT("BasicCreatureExamineUI"), TEXT("BasicCreatureExam_Attributes"));
    if (!List) return;
    List->bVisible = true;
    // BasicCreatureExamineUI constructs six AttributeInfoRegions followed by
    // Health (including percent), Stamina and Mana Attribute2ndInfoRegions.
    // All use the actual 2100006B/10000166 list entry, including label/value
    // alignment, glyph atlas, outline and twenty-pixel row height.
    auto* Dat=Canvas->GetResourceResolver()?Canvas->GetResourceResolver()->GetDatSubsystem():nullptr;
    const auto Insights=ACEAppraisalKnowledge::CompactCreatureInsights(LastAppraisal,bHaveSubject?&Subject:nullptr,Dat);
    const auto Stats = ACEAppraisalFormatting::CreatureStatLines(LastAppraisal);
    while (ExamAttributeRows.Num() < Stats.Num())
    {
        auto Row = UACEUILayoutResolver::LoadTemplate(0x2100006B, 0x10000166);
        if (!Row) return;
        Row->Y = ExamAttributeRows.Num() * Row->Height;
        Row->SetElementName(FString::Printf(TEXT("ExamAttribute_%d"), ExamAttributeRows.Num()));
        List->AddChild(Row);
        ExamAttributeRows.Add(Row);
        ExamAttributeLabels.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
        ExamAttributeValues.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
    }
    for (int32 I = 0; I < Stats.Num(); ++I)
    {
        const auto Row = ExamAttributeRows[I];
        const auto& Stat = Stats[I];
        for (const auto& Child : Row->Children)
        {
            if (Child->ElementName == TEXT("InfoRegion_Label"))
                PlaceTextOnElement(ExamAttributeLabels[I], Child, Stat.Label, 9, FLinearColor::White, 100001);
            else if (Child->ElementName == TEXT("InfoRegion_Value"))
            {
                Child->TextColor = Stat.Color;
                PlaceTextOnElement(ExamAttributeValues[I], Child, Stat.Value, 9, Stat.Color, 100001);
            }
        }
    }
    if (!ExamLevel) ExamLevel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
    PlaceTextOnElement(ExamLevel, Find(TEXT("LevelInfo_Value")), LastAppraisal.Level > 0
        ? FString::FromInt(LastAppraisal.Level) : TEXT("???"), 12, FLinearColor::White, 100001);

    // Use BasicCreatureExam_ExtraInfo and its two-column row from 2100001C.
    // The row atlas, margins, alignment and twenty-pixel cadence are retail data.
    const auto DetailRegion = Find(TEXT("BasicCreatureExam_ExtraInfo"));
    if (!DetailRegion) return;
    auto Lines = ACEAppraisalFormatting::CreatureDetailLines(LastAppraisal);
    Lines.Append(Insights);
    if (!ExamCreatureDetailsScroll)
    {
        ExamCreatureDetailsScroll = Canvas->WidgetTree->ConstructWidget<UScrollBox>();
        ExamCreatureDetailsScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
        ExamCreatureDetailsScroll->SetClipping(EWidgetClipping::ClipToBounds);
        ExamCreatureDetailsScroll->SetAnimateWheelScrolling(false);
        ExamCreatureDetailsSize = Canvas->WidgetTree->ConstructWidget<USizeBox>();
        ExamCreatureDetailsCanvas = Canvas->WidgetTree->ConstructWidget<UCanvasPanel>();
        ExamCreatureDetailsSize->AddChild(ExamCreatureDetailsCanvas);
        ExamCreatureDetailsScroll->AddChild(ExamCreatureDetailsSize);
    }
    while (ExamMiscRows.Num() < Lines.Num())
    {
        auto Row = UACEUILayoutResolver::LoadTemplate(0x2100001C, 0x10000337);
        if (!Row) break;
        ExamMiscRows.Add(Row);
        auto* Label = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
        auto* Value = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
        ExamMiscLabels.Add(Label); ExamMiscValues.Add(Value);
        auto* Icon=Canvas->WidgetTree->ConstructWidget<UImage>();ExamMiscIcons.Add(Icon);
        ExamCreatureDetailsCanvas->AddChild(Icon);
        ExamCreatureDetailsCanvas->AddChild(Label); ExamCreatureDetailsCanvas->AddChild(Value);
    }
    const FVector2D Scale = Canvas->GetLastScale2D();
    constexpr float ContentInset=10.f,IconSize=28.f;
    constexpr float TileHeight=42.f;
    const float ContentWidth=FMath::Max(1.f,DetailRegion->Width-2*ContentInset);
    float RowY=6;int32 Group=0,Column=0;
    for (int32 I = 0; I < ExamMiscRows.Num(); ++I)
    {
        if (!Lines.IsValidIndex(I))
        {
            ExamMiscLabels[I]->SetVisibility(ESlateVisibility::Collapsed);
            ExamMiscValues[I]->SetVisibility(ESlateVisibility::Collapsed);
            ExamMiscIcons[I]->SetVisibility(ESlateVisibility::Collapsed);
            continue;
        }
        auto* Icon=ExamMiscIcons[I].Get();Icon->SetVisibility(ESlateVisibility::Collapsed);
        if(Lines[I].TileGroup)
        {
            if(Group!=Lines[I].TileGroup){if(Column)RowY+=TileHeight;Column=0;Group=Lines[I].TileGroup;}
            const float TileWidth=Lines[I].TileGroup==3?36.f:136.f;
            const int32 Columns=FMath::Max(1,FMath::FloorToInt(ContentWidth/TileWidth));
            const float ActualWidth=FMath::Min(TileWidth,ContentWidth);
            auto* Label=ExamMiscLabels[I].Get();Label->SetVisibility(ESlateVisibility::Collapsed);
            auto* Value=ExamMiscValues[I].Get();
            const auto Cell=ExamMiscRows[I]->Children.Last();
            const bool HasIcon=Lines[I].IconDid&&Canvas->GetResourceResolver()->ResolveTexture(Lines[I].IconDid);
            const float X=ContentInset+Column*ActualWidth,TextX=X+(HasIcon?IconSize+5:0);
            const float TextWidth=FMath::Max(1.f,ActualWidth-(HasIcon?IconSize+7:2));
            Value->SetText(FText::FromString(Lines[I].DisplayValue));Value->SetAutoWrapText(true);Value->SetJustification(ETextJustify::Left);Value->SetMargin(FMargin(0));
            CastChecked<UACERetailTextBlock>(Value)->SetRetailElement(Canvas->GetResourceResolver(),Cell,Scale,TextWidth,false,true);
            CastChecked<UACERetailTextBlock>(Value)->SetSmoothFont(10.f);
            Value->SetColorAndOpacity(Lines[I].Color);Value->SetVisibility(ESlateVisibility::Visible);
            UACEHoverTooltipWidget::SetWidgetTooltip(Value,FText::FromString(Lines[I].Tooltip));
            auto* ValueSlot=CastChecked<UCanvasPanelSlot>(Value->Slot);ValueSlot->SetPosition({TextX*Scale.X,(RowY+2)*Scale.Y});ValueSlot->SetSize({TextWidth*Scale.X,(TileHeight-4)*Scale.Y});
            if(HasIcon)
            {
                Icon->SetBrushFromTexture(Canvas->GetResourceResolver()->ResolveTexture(Lines[I].IconDid));Icon->SetColorAndOpacity(Lines[I].Color);Icon->SetVisibility(ESlateVisibility::Visible);
                UACEHoverTooltipWidget::SetWidgetTooltip(Icon,FText::FromString(Lines[I].Tooltip));
                auto* IconSlot=CastChecked<UCanvasPanelSlot>(Icon->Slot);IconSlot->SetPosition({X*Scale.X,(RowY+2)*Scale.Y});IconSlot->SetSize({IconSize*Scale.X,IconSize*Scale.Y});
            }
            if(++Column>=Columns){Column=0;RowY+=TileHeight;}
            continue;
        }
        if(Column)RowY+=TileHeight;Column=0;Group=0;
        const bool Wide=Lines[I].bFullWidth;
        float RowHeight=20;
        for (const auto& Cell : ExamMiscRows[I]->Children)
        {
            const bool bLabel = Cell->ElementId == 0x1000012A;
            UTextBlock* Text = bLabel ? ExamMiscLabels[I].Get() : ExamMiscValues[I].Get();
            if(Wide&&bLabel){Text->SetVisibility(ESlateVisibility::Collapsed);continue;}
            Text->SetText(FText::FromString(bLabel ? Lines[I].Label : Lines[I].Value));
            Cell->TextColor = Lines[I].Color;
            if (!bLabel) Cell->Width = FMath::Max(1, ContentWidth - Cell->X - 2);
            const float NativeWidth=Wide?ContentWidth:Cell->Width;
            Cell->bTextOneLine=!Wide;
            auto* Retail=Cast<UACERetailTextBlock>(Text);
            Retail->SetRetailElement(Canvas->GetResourceResolver(), Cell, Scale, NativeWidth, false,true);
            const bool Heading=Wide&&Lines[I].Color.Equals(FLinearColor(.88f,.76f,.48f));
            Retail->SetSmoothFont(Heading?12.f:10.f,Heading);
            if(Wide)
            {
                Text->SetWrapTextAt(NativeWidth*Scale.X);
                Text->ForceLayoutPrepass();
                RowHeight=FMath::Max(20.f,Text->GetDesiredSize().Y/Scale.Y+6.f);
            }
            if(Wide&&Retail->GetBitmapFont())
                RowHeight=FMath::Max(20.f,float(ACEDatText::Layout(*Retail->GetBitmapFont(),Lines[I].Value,FMath::Max(1,int32(NativeWidth)),false).Num()*Retail->GetBitmapFont()->MaxCharHeight+6));
            Text->SetColorAndOpacity(Lines[I].Color);
            Text->SetJustification(bLabel || Wide ? ETextJustify::Left : ETextJustify::Right);
            Text->SetMargin(Wide?FMargin(0):FMargin(Cell->TextMargins.Left * Scale.X, Cell->TextMargins.Top * Scale.Y,
                Cell->TextMargins.Right * Scale.X, Cell->TextMargins.Bottom * Scale.Y));
            Text->SetAutoWrapText(Wide);
            UACEHoverTooltipWidget::SetWidgetTooltip(Text, Lines[I].Tooltip.IsEmpty()?Text->GetText():FText::FromString(Lines[I].Tooltip));
            Text->SetVisibility(ESlateVisibility::Visible);
            auto* Slot = CastChecked<UCanvasPanelSlot>(Text->Slot);
            Slot->SetPosition(FVector2D((ContentInset+(Wide?0:Cell->X)) * Scale.X, (RowY + (Wide?3:Cell->Y)) * Scale.Y));
            Slot->SetSize(FVector2D(NativeWidth * Scale.X, (Wide?RowHeight-3:Cell->Height) * Scale.Y));
        }
        RowY+=RowHeight;
    }
    if(Column)RowY+=TileHeight;
    ExamCreatureDetailsSize->SetWidthOverride(DetailRegion->Width * Scale.X);
    ExamCreatureDetailsSize->SetHeightOverride((RowY+6) * Scale.Y);
    if (ExamCreatureScrolledGuid != LastAppraisal.ObjectGuid)
    {
        ExamCreatureDetailsScroll->SetScrollOffset(0.f);
        ExamCreatureScrolledGuid = LastAppraisal.ObjectGuid;
    }
    ExamCreatureDetailsScroll->SetVisibility(ESlateVisibility::Visible);
    Canvas->PlaceWidgetAtElement(ExamCreatureDetailsScroll, DetailRegion, 100001, FMargin(0,0,0,24));
}
