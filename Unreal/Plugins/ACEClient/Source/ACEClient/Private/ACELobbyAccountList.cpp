#include "ACELobbyAccountList.h"
#include "ACELobbyArt.h"
#include "ACELoginWidget.h"
#include "ACEDatSubsystem.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIFontStyles.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/NativeWidgetHost.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Engine/GameInstance.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/CoreStyle.h"

namespace
{
	FButtonStyle RowStyle(bool bSelected)
	{
		return ACELobbyArt::QuietRowStyle(bSelected, FMargin(10, 0, 12, 0));
	}

	FButtonStyle GlyphButtonStyle()
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource()).SetPressed(FSlateNoResource()).SetDisabled(FSlateNoResource());
		Style.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
		return Style;
	}
}

TSharedRef<SWidget> UACELobbyAccountRow::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto* Height = WidgetTree->ConstructWidget<USizeBox>();
		Height->SetHeightOverride(RowHeight);
		WidgetTree->RootWidget = Height;
		RowButton = WidgetTree->ConstructWidget<UACELobbyPlainButton>();
		RowButton->SetStyle(RowStyle(false));
		RowButton->OnClicked.AddDynamic(this, &UACELobbyAccountRow::HandleClicked);
		// Selected rows carry a thin gold accent on their left edge.
		auto* Layers = WidgetTree->ConstructWidget<UOverlay>();
		Height->SetContent(Layers);
		auto* RowSlot = Layers->AddChildToOverlay(RowButton);
		RowSlot->SetHorizontalAlignment(HAlign_Fill); RowSlot->SetVerticalAlignment(VAlign_Fill); RowSlot->SetPadding(FMargin(0, 0, 4, 0));
		auto* AccentHost = WidgetTree->ConstructWidget<UNativeWidgetHost>();
		AccentHost->SetContent(SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(ACELobbyArt::RowAccent).Padding(0));
		auto* AccentSize = WidgetTree->ConstructWidget<USizeBox>(); AccentSize->SetWidthOverride(2); AccentSize->SetContent(AccentHost);
		AccentSize->SetVisibility(ESlateVisibility::Hidden);
		auto* AccentSlot = Layers->AddChildToOverlay(AccentSize);
		AccentSlot->SetHorizontalAlignment(HAlign_Left); AccentSlot->SetVerticalAlignment(VAlign_Fill); AccentSlot->SetPadding(FMargin(0, 1, 0, 1));
		Accent = AccentSize;
		auto* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
		RowButton->SetContent(Line);
		if (auto* ContentSlot = Cast<UButtonSlot>(Line->Slot))
		{
			ContentSlot->SetHorizontalAlignment(HAlign_Fill); ContentSlot->SetVerticalAlignment(VAlign_Fill);
		}
		StarButton = WidgetTree->ConstructWidget<UACELobbyPlainButton>();
		StarButton->SetStyle(GlyphButtonStyle());
		StarButton->SetToolTipText(FText::FromString(TEXT("Pin to favorites")));
		StarButton->OnClicked.AddDynamic(this, &UACELobbyAccountRow::HandleStarClicked);
		auto* Star = WidgetTree->ConstructWidget<UNativeWidgetHost>();
		TWeakObjectPtr<UACELobbyAccountRow> Weak(this);
		TWeakObjectPtr<UButton> WeakStar(StarButton);
		Star->SetContent(SNew(SACELobbyGlyph).Kind(SACELobbyGlyph::EKind::Star).Size(12.f)
			.Filled_Lambda([Weak]() { return Weak.IsValid() && Weak->bStarFilled; })
			.Color_Lambda([Weak, WeakStar]()
			{
				const bool bFav = Weak.IsValid() && Weak->bStarFilled;
				const bool bHot = WeakStar.IsValid() && WeakStar->IsHovered();
				// Mockup: on #f6c445, off #6e5c38.
				return bFav ? (bHot ? FLinearColor(FColor(0xff, 0xdd, 0x77)) : FLinearColor(FColor(0xf6, 0xc4, 0x45)))
					: (bHot ? FLinearColor(FColor(0xc9, 0xa0, 0x52)) : FLinearColor(FColor(0x6e, 0x5c, 0x38)));
			}));
		auto* StarSize = WidgetTree->ConstructWidget<USizeBox>();
		StarSize->SetWidthOverride(14); StarSize->SetHeightOverride(14);
		StarSize->SetContent(Star);
		StarButton->SetContent(StarSize);
		auto* StarSlot = Line->AddChildToHorizontalBox(StarButton);
		StarSlot->SetVerticalAlignment(VAlign_Center); StarSlot->SetPadding(FMargin(0, 0, 10, 0));
		auto* Lines = WidgetTree->ConstructWidget<UVerticalBox>();
		auto MakeText = [this](float Px, const TCHAR* Weight = TEXT("Regular"))
		{
			auto* T = WidgetTree->ConstructWidget<UTextBlock>();
			T->SetFont(FCoreStyle::GetDefaultFontStyle(Weight, FMath::RoundToInt(Px * .75f)));
			T->SetShadowOffset(FVector2D(1, 1)); T->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, .7f));
			T->SetVisibility(ESlateVisibility::HitTestInvisible); T->SetClipping(EWidgetClipping::ClipToBounds);
			T->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
			return T;
		};
		// Crisp engine text with a 1px shadow; the name is split so a search match can be lit.
		auto* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		Name = MakeText(15.f); NameMatch = MakeText(15.f); NameTail = MakeText(15.f);
		NameMatch->SetColorAndOpacity(ACELobbyArt::Highlight);
		NameRow->AddChildToHorizontalBox(Name); NameRow->AddChildToHorizontalBox(NameMatch);
		NameRow->AddChildToHorizontalBox(NameTail)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NameRow->SetClipping(EWidgetClipping::ClipToBounds);
		Lines->AddChildToVerticalBox(NameRow);
		UserLine = MakeText(11.5f); UserLine->SetColorAndOpacity(FLinearColor(FColor(0x8f, 0x81, 0x66)));
		Lines->AddChildToVerticalBox(UserLine)->SetPadding(FMargin(1, -1, 0, 0));
		auto* LinesSlot = Line->AddChildToHorizontalBox(Lines);
		LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LinesSlot->SetVerticalAlignment(VAlign_Center);
		WhenText = MakeText(11.5f);
		auto* WhenSlot = Line->AddChildToHorizontalBox(WhenText);
		WhenSlot->SetVerticalAlignment(VAlign_Center); WhenSlot->SetPadding(FMargin(10, 0, 0, 0));
	}
	// Rows are recycled, so the grip and drop target read the current item each time.
	TWeakObjectPtr<UACELobbyAccountRow> Self(this);
	return ACEMakeAccountOrderRow(
		TAttribute<UACELoginWidget*>::CreateLambda([Self]() -> UACELoginWidget* { return Self.IsValid()&&Self->Item?Self->Item->Owner.Get():nullptr; }),
		TAttribute<FString>::CreateLambda([Self]() { return Self.IsValid()&&Self->Item?Self->Item->Id:FString(); }),
		TAttribute<FString>::CreateLambda([Self]() { return Self.IsValid()&&Self->Item?Self->Item->Title:FString(); }),
		Super::RebuildWidget());
}

void UACELobbyAccountRow::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	Item = Cast<UACELobbyAccountItem>(ListItemObject);
	Refresh();
}

void UACELobbyAccountRow::Refresh()
{
	if (!Item || !Name || !RowButton) return;
	RowButton->SetToolTipText(FText::FromString(Item->Title));
	const FLinearColor Base = Item->bSelected ? FLinearColor(FColor(0xff, 0xf2, 0xcc)) : FLinearColor(FColor(0xdc, 0xcf, 0xab));
	if (UserLine)
	{
		if (UserLine->GetText().ToString() != Item->Username) UserLine->SetText(FText::FromString(Item->Username));
		UserLine->SetVisibility(Item->Username.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (WhenText)
	{
		if (WhenText->GetText().ToString() != Item->When) WhenText->SetText(FText::FromString(Item->When));
		WhenText->SetColorAndOpacity(Item->bSelected ? FLinearColor(FColor(0xd8, 0xc0, 0x8a)) : FLinearColor(FColor(0x84, 0x77, 0x5e)));
	}
	if (Accent) Accent->SetVisibility(Item->bSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	// Split the title around the search match (if any) so only the match is lit.
	FString Head = Item->Title, Match, Tail;
	if (Item->MatchStart != INDEX_NONE && Item->MatchLen > 0 && Item->MatchStart < Item->Title.Len())
	{
		Head = Item->Title.Left(Item->MatchStart);
		Match = Item->Title.Mid(Item->MatchStart, Item->MatchLen);
		Tail = Item->Title.Mid(Item->MatchStart + Item->MatchLen);
	}
	auto Set = [](UTextBlock* T, const FString& S) { if (T && T->GetText().ToString() != S) T->SetText(FText::FromString(S)); };
	Set(Name, Head); Set(NameMatch, Match); Set(NameTail, Tail);
	Name->SetColorAndOpacity(Base); NameTail->SetColorAndOpacity(Base);
	NameMatch->SetVisibility(Match.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	RowButton->SetStyle(RowStyle(Item->bSelected));
	bStarFilled = Item->bFavorite;
	StarButton->SetToolTipText(FText::FromString(Item->bFavorite ? TEXT("Unpin from favorites") : TEXT("Pin to favorites")));
}

void UACELobbyAccountRow::HandleClicked()
{
	if (Item && Item->Owner.IsValid()) Item->Owner->RunAction(TEXT("selectaccount"), Item->Id);
}

void UACELobbyAccountRow::HandleStarClicked()
{
	if (Item && Item->Owner.IsValid()) Item->Owner->RunAction(TEXT("togglefavorite"), Item->Id);
}

UACELobbyAccountListView::UACELobbyAccountListView(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	EntryWidgetClass = UACELobbyAccountRow::StaticClass();
	SelectionMode = ESelectionMode::None;
	bIsFocusable = false; // Keyboard stays with the launcher (type-to-jump, arrows, Enter).
	// No setter is exposed for the spacing in UE 5.8; it is a plain reflected float.
	if (FFloatProperty* Spacing = FindFProperty<FFloatProperty>(UListView::StaticClass(), TEXT("VerticalEntrySpacing")))
		Spacing->SetPropertyValue_InContainer(this, UACELobbyAccountRow::RowGap);
	WidgetStyle.SetBackgroundBrush(FSlateNoResource());
	ScrollBarStyle = ACELobbyArt::QuietScrollBar(6.f);
	SetScrollBarPadding(FMargin(4, 16, 0, 16));
}

void UACELobbyAccountListView::ApplyArt()
{
	// Rows and the slim scrollbar are drawn without DAT art; just restyle what is shown.
	if (MyListView) MyListView->Invalidate(EInvalidateWidgetReason::Paint);
	RefreshRows();
}

bool UACELobbyAccountListView::CanScroll() const
{
	return MyListView.IsValid() && MyListView->IsScrollbarNeeded();
}

void UACELobbyAccountListView::ScrollBy(float Items)
{
	SetScrollOffset(FMath::Clamp(GetScrollOffset() + Items, 0.f, float(FMath::Max(0, GetNumItems() - 1))));
}

void UACELobbyAccountListView::RefreshRows()
{
	for (UUserWidget* Entry : GetDisplayedEntryWidgets())
		if (auto* Row = Cast<UACELobbyAccountRow>(Entry)) Row->Refresh();
}
