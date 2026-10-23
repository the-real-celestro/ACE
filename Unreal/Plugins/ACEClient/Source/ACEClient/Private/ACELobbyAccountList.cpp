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

namespace
{
	FButtonStyle RowStyle(bool bSelected)
	{
		FButtonStyle Style;
		if (const FACELobbyArt* Art = ACELobbyArt::Get())
		{
			Style.SetNormal(Art->Row[bSelected ? 2 : 0]).SetHovered(Art->Row[bSelected ? 2 : 1]).SetPressed(Art->Row[2]).SetDisabled(Art->Row[0]);
		}
		else
		{
			const FLinearColor Edge = bSelected ? ACELobbyArt::Highlight : FLinearColor(.3f, .24f, .14f, 1);
			Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.03f, .05f, .1f, .8f), 2.f, Edge, 1.f));
			Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.06f, .08f, .14f, .9f), 2.f, ACELobbyArt::Gold, 1.f));
			Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.02f, .03f, .06f, 1), 2.f, ACELobbyArt::Highlight, 1.f));
			Style.SetDisabled(Style.Normal);
		}
		Style.SetNormalPadding(FMargin(9, 0, 14, 0)).SetPressedPadding(FMargin(9, 0, 14, 0));
		return Style;
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
		// The selected row's gold pointer sits just past its right edge.
		auto* Layers = WidgetTree->ConstructWidget<UOverlay>();
		Height->SetContent(Layers);
		auto* RowSlot = Layers->AddChildToOverlay(RowButton);
		RowSlot->SetHorizontalAlignment(HAlign_Fill); RowSlot->SetVerticalAlignment(VAlign_Fill); RowSlot->SetPadding(FMargin(0, 0, 10, 0));
		auto* PointerHost = WidgetTree->ConstructWidget<UNativeWidgetHost>();
		PointerHost->SetContent(SNew(SACELobbyGlyph).Kind(SACELobbyGlyph::EKind::Pointer).Size(10.f).Color(FLinearColor(FColor(0xf2, 0xc4, 0x5c))));
		auto* PointerSize = WidgetTree->ConstructWidget<USizeBox>();
		PointerSize->SetWidthOverride(10); PointerSize->SetHeightOverride(10); PointerSize->SetContent(PointerHost);
		PointerSize->SetVisibility(ESlateVisibility::Hidden);
		auto* PointerSlot = Layers->AddChildToOverlay(PointerSize);
		PointerSlot->SetHorizontalAlignment(HAlign_Right); PointerSlot->SetVerticalAlignment(VAlign_Center);
		Pointer = PointerSize;
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
		Star->SetContent(SNew(SACELobbyGlyph).Kind(SACELobbyGlyph::EKind::Star).Size(15.f)
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
		StarSize->SetWidthOverride(18); StarSize->SetHeightOverride(18);
		StarSize->SetContent(Star);
		StarButton->SetContent(StarSize);
		auto* StarSlot = Line->AddChildToHorizontalBox(StarButton);
		StarSlot->SetVerticalAlignment(VAlign_Center); StarSlot->SetPadding(FMargin(0, 0, 11, 0));
		auto* Lines = WidgetTree->ConstructWidget<UVerticalBox>();
		Name = WidgetTree->ConstructWidget<UACERetailTextBlock>();
		Name->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
		// Character select's slot-row font, so accounts and characters read alike.
		Name->SetFontOverride(ACEUIFontStyles::FontIdForStyleName(TEXT("sansfont16")));
		Name->SetAutoWrapText(false);
		Name->SetClipping(EWidgetClipping::ClipToBounds);
		Name->SetVisibility(ESlateVisibility::HitTestInvisible);
		UACEDatSubsystem* Dat = GetGameInstance() ? GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr;
		Name->SetRetailElement(Dat && Dat->IsDatReady() ? Dat->GetUiResources() : nullptr, nullptr, FVector2D(1.3f), 0, false);
		Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Name->SetShadowOffset(FVector2D(1, 1)); Name->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, .85f));
		Lines->AddChildToVerticalBox(Name);
		auto MakeText = [this](float Px)
		{
			auto* T = WidgetTree->ConstructWidget<UTextBlock>();
			T->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FMath::RoundToInt(Px * .75f)));
			T->SetShadowOffset(FVector2D(1, 1)); T->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, .8f));
			T->SetVisibility(ESlateVisibility::HitTestInvisible); T->SetClipping(EWidgetClipping::ClipToBounds);
			T->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
			return T;
		};
		UserLine = MakeText(12.5f); UserLine->SetColorAndOpacity(FLinearColor(FColor(0x9f, 0x90, 0x70)));
		Lines->AddChildToVerticalBox(UserLine)->SetPadding(FMargin(1, 0, 0, 0));
		auto* LinesSlot = Line->AddChildToHorizontalBox(Lines);
		LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LinesSlot->SetVerticalAlignment(VAlign_Center);
		WhenText = MakeText(12.f);
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
	if (Name->GetText().ToString() != Item->Title) Name->SetText(FText::FromString(Item->Title));
	Name->SetToolTipText(FText::FromString(Item->Title));
	RowButton->SetToolTipText(FText::FromString(Item->Title));
	// The DAT may have finished loading after this row was built.
	if (!Name->GetBitmapFont())
		if (UACEDatSubsystem* Dat = GetGameInstance() ? GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr; Dat && Dat->IsDatReady())
			Name->SetRetailElement(Dat->GetUiResources(), nullptr, FVector2D(1.3f), 0, false);
	const FLinearColor Base = Item->bSelected ? FLinearColor(FColor(0xff, 0xf7, 0xd6)) : FLinearColor(FColor(0xe9, 0xdc, 0xb8));
	if (UserLine)
	{
		if (UserLine->GetText().ToString() != Item->Username) UserLine->SetText(FText::FromString(Item->Username));
		UserLine->SetVisibility(Item->Username.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (WhenText)
	{
		if (WhenText->GetText().ToString() != Item->When) WhenText->SetText(FText::FromString(Item->When));
		WhenText->SetColorAndOpacity(Item->bSelected ? FLinearColor(FColor(0xe4, 0xcf, 0x9c)) : FLinearColor(FColor(0x8f, 0x81, 0x66)));
	}
	if (Pointer) Pointer->SetVisibility(Item->bSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	Name->SetColorAndOpacity(Base);
	TArray<FLinearColor> Colors;
	if (Item->MatchStart != INDEX_NONE && Item->MatchLen > 0)
	{
		Colors.Init(Base, Item->Title.Len());
		for (int32 I = Item->MatchStart; I < FMath::Min(Item->Title.Len(), Item->MatchStart + Item->MatchLen); ++I)
			Colors[I] = ACELobbyArt::Highlight;
	}
	Name->SetTextColors(Colors);
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
	ScrollBarStyle = FScrollBarStyle()
		.SetVerticalBackgroundImage(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, .35f), 2.f))
		.SetVerticalTopSlotImage(FSlateNoResource()).SetVerticalBottomSlotImage(FSlateNoResource())
		.SetHorizontalBackgroundImage(FSlateNoResource()).SetHorizontalTopSlotImage(FSlateNoResource()).SetHorizontalBottomSlotImage(FSlateNoResource())
		.SetNormalThumbImage(FSlateRoundedBoxBrush(FLinearColor(.36f, .27f, .14f, 1), 3.f))
		.SetHoveredThumbImage(FSlateRoundedBoxBrush(FLinearColor(.6f, .45f, .22f, 1), 3.f))
		.SetDraggedThumbImage(FSlateRoundedBoxBrush(FLinearColor(.75f, .56f, .27f, 1), 3.f))
		.SetThickness(16.f);
	// Leave room above and below the bar for the retail arrow buttons drawn over the list.
	SetScrollBarPadding(FMargin(4, 16, 0, 16));
}

void UACELobbyAccountListView::ApplyArt()
{
	const FACELobbyArt* Art = ACELobbyArt::Get();
	if (!Art) return;
	// SScrollBar keeps pointers into this style, so updating it in place restyles the live bar.
	ScrollBarStyle.SetVerticalBackgroundImage(Art->Track)
		.SetNormalThumbImage(Art->Thumb[0]).SetHoveredThumbImage(Art->Thumb[1]).SetDraggedThumbImage(Art->Thumb[1]);
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
