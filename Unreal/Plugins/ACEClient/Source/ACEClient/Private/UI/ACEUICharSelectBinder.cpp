#include "UI/ACEUICharSelectBinder.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUIElement.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACELobbyArt.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"

namespace
{
	constexpr uint32 DidSlotTopIdle = 0x06005EA7u;
	constexpr uint32 DidSlotMidIdle = 0x06005EA9u;
	constexpr uint32 DidSlotBotIdle = 0x06005EABu;
	constexpr uint32 DidSlotTopSel = 0x06005EB4u;
	constexpr uint32 DidSlotMidSel = 0x06005EB5u;
	constexpr uint32 DidSlotBotSel = 0x06005EB6u;

	constexpr uint32 DidEnterIdle = 0x06004CB2u;
	constexpr uint32 DidEnterHover = 0x06004CB3u;
	constexpr uint32 DidEnterPress = 0x06004CB3u;
	constexpr uint32 DidEnterAlpha = 0x06004CB1u;

	constexpr uint32 DidRedLargeIdle = 0x06004C9Eu;
	constexpr uint32 DidRedLargeHover = 0x06004C9Fu;
	constexpr uint32 DidRedLargePress = 0x06004CA0u;
	constexpr uint32 DidRedLargeAlpha = 0x06004C9Du;

	constexpr uint32 DidRedHugeIdle = 0x06004CA3u;
	constexpr uint32 DidRedHugeHover = 0x06004CA4u;
	constexpr uint32 DidRedHugePress = 0x06004CA5u;
	constexpr uint32 DidRedHugeAlpha = 0x06004CA2u;

	constexpr uint32 DidListTL = 0x06004CC3u;
	constexpr uint32 DidListTR = 0x06004CC4u;
	constexpr uint32 DidListBL = 0x06004CC5u;
	constexpr uint32 DidListBR = 0x06004CC6u;
	constexpr uint32 DidListTopEdge = 0x06004CC7u;
	constexpr uint32 DidListSideEdge = 0x06004CB8u;

	/** Retail UICore_Text_fonts from charactermanagement templates. */
	constexpr uint32 FontSans20 = 0x4000000Bu;
	constexpr uint32 FontSans22 = 0x4000000Cu;
	constexpr uint32 FontSans16 = 0x40000009u;
	constexpr uint32 FontFancy40 = 0x40000012u;

	// gmCharacterManagementUI sizes rows from the server character-slot limit.
	constexpr int32 CharSelectOverlayZ = 200;
	/** Opaque surround while the credits roll (retail black). */
	const FLinearColor LetterboxBlack = FLinearColor::Black;
	/** Behind the backdrop until its art loads: retail night blue. */
	const FLinearColor LetterboxNight = FLinearColor(.006f, .012f, .03f, 1.f);

	/** The full-screen 800x600 backdrop image in the character-management layout. */
	uint32 FindBackdropImage(const TSharedPtr<FACEUIElement>& El, int32 Depth = 0)
	{
		if (!El.IsValid() || Depth > 3) return 0;
		if (El->Width >= ACEUI::ReferenceWidth && El->Height >= ACEUI::ReferenceHeight)
		{
			if ((El->ImageFileId >> 24) == 0x06) return El->ImageFileId;
			for (const auto& Pair : El->States) if ((Pair.Value.ImageFileId >> 24) == 0x06) return Pair.Value.ImageFileId;
		}
		for (const TSharedPtr<FACEUIElement>& Child : El->Children)
			if (const uint32 Id = FindBackdropImage(Child, Depth + 1)) return Id;
		return 0;
	}

	FSlateBrush ColorFill(const FLinearColor& Color)
	{
		FSlateBrush Fill; Fill.DrawAs = ESlateBrushDrawType::Box; Fill.TintColor = FSlateColor(Color);
		return Fill;
	}

	bool ElementIsOrUnder(const TSharedPtr<FACEUIElement>& Needle, const TSharedPtr<FACEUIElement>& Hay)
	{
		if (!Needle.IsValid() || !Hay.IsValid())
		{
			return false;
		}
		for (TSharedPtr<FACEUIElement> Cur = Needle; Cur.IsValid(); Cur = Cur->Parent.Pin())
		{
			if (Cur == Hay)
			{
				return true;
			}
		}
		return false;
	}
}

void UACEUICharSelectBinder::Initialize(UACEClientSubsystem* InClient, UACEUIElementManager* InManager,
	UACEUICanvasWidget* InCanvas, AACEPlayerController* InPC,
	const TArray<FACECharacterInfo>& InCharacters, const FString& InServerName)
{
	const int32 PreviousSelection = SelectedCharacterId;
	Shutdown();
	Client = InClient;
	Manager = InManager;
	Canvas = InCanvas;
	PlayerController = InPC;
	Characters = InCharacters;
	if (Canvas && Canvas->GetResourceResolver() && Canvas->GetResourceResolver()->GetDatSubsystem())
		CharacterStrings.LoadStrings(Canvas->GetResourceResolver()->GetDatSubsystem()->GetDatDirectory());
	ServerName = InServerName;
	SelectedIndex = INDEX_NONE;
	SelectedCharacterId = 0;

	if (Manager)
	{
		ActivatedHandle = Manager->OnElementActivated.AddUObject(this, &UACEUICharSelectBinder::OnElementActivated);
		Manager->SetElementVisibleByName(TEXT("CharacterSlotTemplate"), false);
	}
	bBound = true;
	if (Characters.Num() > 0)
	{
		const int32 PreviousIndex = Characters.IndexOfByPredicate([PreviousSelection](const FACECharacterInfo& C) { return C.CharacterId == PreviousSelection; });
		SelectCharacterIndex(PreviousIndex == INDEX_NONE ? 0 : PreviousIndex);
	}
	EnsureOverlays();
	SyncButtonStates();
	RefreshOverlays();
}

void UACEUICharSelectBinder::Shutdown()
{
	ShutdownActions();
	if (Manager && ActivatedHandle.IsValid())
	{
		Manager->OnElementActivated.Remove(ActivatedHandle);
		ActivatedHandle.Reset();
	}

	auto RemoveOverlay = [](UWidget* W)
	{
		if (W)
		{
			W->RemoveFromParent();
		}
	};
	RemoveOverlay(LetterboxFill);
	LetterboxFill = nullptr;
	for (TObjectPtr<UBorder>& B : BackdropTiles) { RemoveOverlay(B.Get()); }
	BackdropTiles.Reset();
	BackdropBrushes.Reset();
	BackdropTexture = nullptr;
	BackdropExtent = FIntPoint(-1, -1);
	BackdropSpread = -1;
	RemoveOverlay(WorldNameImage);
	WorldNameImage = nullptr;
	for (TObjectPtr<UBorder>& L : CharNameImages) { RemoveOverlay(L.Get()); }
	CharNameImages.Reset();
	for (TObjectPtr<UBorder>& B : CharSlotTops) { RemoveOverlay(B.Get()); }
	CharSlotTops.Reset();
	for (TObjectPtr<UBorder>& B : CharSlotMiddles) { RemoveOverlay(B.Get()); }
	CharSlotMiddles.Reset();
	for (TObjectPtr<UBorder>& B : CharSlotBottoms) { RemoveOverlay(B.Get()); }
	CharSlotBottoms.Reset();
	for (TObjectPtr<UBorder>& B : ListFrameParts) { RemoveOverlay(B.Get()); }
	ListFrameParts.Reset();
	for (auto& Pair : ButtonTextImages) { RemoveOverlay(Pair.Value.Get()); }
	ButtonTextImages.Reset();

	Client = nullptr;
	Manager = nullptr;
	Canvas = nullptr;
	PlayerController = nullptr;
	bBound = false;
}

void UACEUICharSelectBinder::TickRefresh(float DeltaSeconds)
{
	if (!bBound)
	{
		return;
	}
	if (Client)
	{
		const FString Live = Client->GetServerName();
		if (!Live.IsEmpty())
		{
			ServerName = Live;
		}
	}
	EnsureOverlays();
	SyncButtonStates();
	RefreshOverlays();
	if (bCredits) TickCredits(DeltaSeconds);
	else RefreshDialog();
}

void UACEUICharSelectBinder::SelectCharacterIndex(int32 Index)
{
	if (!Characters.IsValidIndex(Index))
	{
		SelectedIndex = INDEX_NONE;
		SelectedCharacterId = 0;
		return;
	}
	SelectedIndex = Index;
	SelectedCharacterId = Characters[Index].CharacterId;
}

void UACEUICharSelectBinder::EnterSelectedCharacter()
{
	if (!Client || SelectedCharacterId == 0 || !Characters.IsValidIndex(SelectedIndex)
		|| Characters[SelectedIndex].DeleteSeconds != 0)
	{
		return;
	}
	Client->EnterWorld(SelectedCharacterId);
}

void UACEUICharSelectBinder::OnElementActivated(TSharedPtr<FACEUIElement> Element)
{
	if (bCredits) { SetCreditsVisible(false); return; }
	if (Dialog && Dialog->bVisible)
	{
		for (auto Cur = Element; Cur; Cur = Cur->Parent.Pin())
		{
			if (Cur->ElementId == 0x2E) { ConfirmDelete(); return; }
			if (Cur->ElementId == 0x2F) { CloseDialog(); return; }
		}
		return;
	}
	if (Client && Client->GetSession() && Client->GetSession()->IsCharacterManagementPending()) return;
	if (!Element.IsValid())
	{
		return;
	}
	for (TSharedPtr<FACEUIElement> Cur = Element; Cur.IsValid(); Cur = Cur->Parent.Pin())
	{
		const FString& Name = Cur->ElementName;
		if (Name == TEXT("EnterGameButton"))
		{
			EnterSelectedCharacter();
			return;
		}
		if (Name == TEXT("ExitButton"))
		{
			if (Client)
			{
				Client->Logout();
			}
			return;
		}
		if(Name==TEXT("CreateCharacterButton")){if(PlayerController)PlayerController->ShowCharacterCreationUI();return;}
		if (Name == TEXT("DeleteCharacterButton")) { ShowDeleteConfirmation(); return; }
		if (Name == TEXT("RestoreCharacterButton"))
		{
			if (Client && Client->GetSession()) Client->GetSession()->RestoreCharacter(SelectedCharacterId);
			return;
		}
		if (Name == TEXT("CreditsButton")) { SetCreditsVisible(true); return; }
	}
}

int32 UACEUICharSelectBinder::GetCharacterRowHeight() const
{
	const auto List = Manager ? Manager->FindElementByName(TEXT("CharacterListBox")) : nullptr;
	if (!List.IsValid())
	{
		return 16;
	}
	const auto Session = Client ? Client->GetSession() : nullptr;
	const int32 Allowed = Session ? Session->GetCharacterSlotCount() : 11;
	const int32 Rows = FMath::Max(1, FMath::Max(Allowed, Characters.Num()));
	// Retail caps the density at twenty rows, while a normal eleven-slot account
	// gets a taller selection strip and centered, unscaled 16-pixel text.
	return FMath::Max(1, FMath::Max(List->Height / Rows, List->Height / 20));
}

bool UACEUICharSelectBinder::TryHandleOverlayClick(FVector2D LayoutPos)
{
	if (bCredits) { SetCreditsVisible(false); return true; }
	if (Dialog && Dialog->bVisible)
	{
		// Consume every click outside the modal as well as inside it. The original
		// character-list hit targets must never change the pending delete target.
		OnElementActivated(Manager->HitTestCanvas(LayoutPos.X, LayoutPos.Y));
		return true;
	}
	if (Client && Client->GetSession() && Client->GetSession()->IsCharacterManagementPending()) return true;
	if (!Manager)
	{
		return false;
	}
	TSharedPtr<FACEUIElement> List = Manager->FindElementByName(TEXT("CharacterListBox"));
	if (!List.IsValid() || !List->bVisible)
	{
		return false;
	}
	const FIntPoint Origin = List->GetScreenOrigin();
	const float ListW = static_cast<float>(FMath::Max(1, List->Width));
	const float ListH = static_cast<float>(FMath::Max(1, List->Height));
	if (LayoutPos.X < Origin.X || LayoutPos.Y < Origin.Y
		|| LayoutPos.X >= Origin.X + ListW || LayoutPos.Y >= Origin.Y + ListH)
	{
		return false;
	}
	const int32 Row = static_cast<int32>((LayoutPos.Y - Origin.Y) / static_cast<float>(GetCharacterRowHeight()));
	if (!Characters.IsValidIndex(Row))
	{
		return false;
	}

	const double Now = FPlatformTime::Seconds();
	const bool bDouble = (LastClickIndex == Row && (Now - LastClickTime) < 0.35);
	LastClickTime = Now;
	LastClickIndex = Row;
	SelectCharacterIndex(Row);
	if (bDouble)
	{
		EnterSelectedCharacter();
	}
	return true;
}

void UACEUICharSelectBinder::PlaceScaled(UWidget* Widget, float LayoutX, float LayoutY, float LayoutW, float LayoutH, int32 ZOrder)
{
	if (!Widget || !Canvas)
	{
		return;
	}
	const float SX = Canvas->GetLastScaleX();
	const float SY = Canvas->GetLastScaleY();
	if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
	{
		Slot->SetAnchors(FAnchors(0.f, 0.f));
		Slot->SetAlignment(FVector2D(0.f, 0.f));
		Slot->SetAutoSize(false);
		Slot->SetPosition(FVector2D(LayoutX * SX, LayoutY * SY));
		Slot->SetSize(FVector2D(FMath::Max(1.f, LayoutW * SX), FMath::Max(1.f, LayoutH * SY)));
		Slot->SetZOrder(ZOrder);
	}
}

UBorder* UACEUICharSelectBinder::EnsureTextImage(TObjectPtr<UBorder>& Slot)
{
	if (!Canvas || !Canvas->WidgetTree || !Canvas->GetElementLayer())
	{
		return nullptr;
	}
	if (!Slot)
	{
		Slot = Canvas->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Slot->SetPadding(FMargin(0.f));
		Slot->SetVisibility(ESlateVisibility::HitTestInvisible);
		Canvas->GetElementLayer()->AddChild(Slot);
	}
	return Slot.Get();
}

void UACEUICharSelectBinder::PlaceDatText(UBorder* Border, uint32 FontId, const FString& Text, FColor Color,
	float BoxX, float BoxY, float BoxW, float BoxH, int32 ZOrder, bool bOutline,
	FString& CacheKey, TObjectPtr<UTexture2D>& CacheTex, int32& CacheW, int32& CacheH,
	bool bCenter, float LayoutYNudge)
{
	if (!Border || !Canvas || !Canvas->GetResourceResolver() || Text.IsEmpty() || Text == TEXT(" "))
	{
		if (Border)
		{
			Border->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}
	// Natural glyph texture only — do not bake into the full button rect. Full-box
	// textures + UBorder ImageSize scaling were leaving RedLarge labels low in the face.
	const FString Key = FString::Printf(TEXT("%08X|%s|%08X|%d|nat"), FontId, *Text,
		Color.DWColor(), bOutline ? 1 : 0);
	if (CacheKey != Key || !CacheTex)
	{
		int32 Tw = 0;
		int32 Th = 0;
		UTexture2D* Tex = Canvas->GetResourceResolver()->RenderDatText(
			FontId, Text, Color, bOutline, FColor(20, 12, 4, 255), &Tw, &Th, 0, 0);
		if (!Tex || Tw <= 0 || Th <= 0)
		{
			Border->SetVisibility(ESlateVisibility::Collapsed);
			return;
		}
		CacheKey = Key;
		CacheTex = Tex;
		CacheW = Tw;
		CacheH = Th;
	}

	const float SX = Canvas->GetLastScaleX();
	const float SY = Canvas->GetLastScaleY();
	// Float placement (same as PlaceScaled) so bilinear-filtered glyph textures scale
	// smoothly; integer snapping was fighting subpixel canvas scale and looked aliased.
	const float BoxX0 = BoxX * SX;
	const float BoxY0 = (BoxY + LayoutYNudge) * SY;
	const float BoxPW = FMath::Max(1.f, BoxW * SX);
	const float BoxPH = FMath::Max(1.f, BoxH * SY);
	const float TextPW = FMath::Max(1.f, static_cast<float>(CacheW) * SX);
	const float TextPH = FMath::Max(1.f, static_cast<float>(CacheH) * SY);
	const float DrawX = bCenter ? BoxX0 + (BoxPW - TextPW) * 0.5f : BoxX0;
	const float DrawY = BoxY0 + (BoxPH - TextPH) * 0.5f;

	FSlateBrush Brush;
	Brush.SetResourceObject(CacheTex.Get());
	Brush.ImageSize = FVector2D(TextPW, TextPH);
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Border->SetBrush(Brush);
	Border->SetBrushColor(FLinearColor::White);
	Border->SetPadding(FMargin(0.f));
	Border->SetVisibility(ESlateVisibility::HitTestInvisible);

	if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Border->Slot))
	{
		Slot->SetAnchors(FAnchors(0.f, 0.f));
		Slot->SetAlignment(FVector2D(0.f, 0.f));
		Slot->SetAutoSize(false);
		Slot->SetPosition(FVector2D(FMath::RoundToFloat(DrawX), FMath::RoundToFloat(DrawY)));
		Slot->SetSize(FVector2D(TextPW, TextPH));
		Slot->SetZOrder(ZOrder);
	}
}

void UACEUICharSelectBinder::SyncButtonStates()
{
	if (!Manager)
	{
		return;
	}
	const TSharedPtr<FACEUIElement> Hover = Manager->GetHoverElement();
	const TSharedPtr<FACEUIElement> Active = Manager->GetActiveElement();
	const TSharedPtr<FACEUIElement> Capture = Manager->GetCaptureElement();
	const auto Session = Client ? Client->GetSession() : nullptr;
	const bool bReady = !bCredits && !(Dialog && Dialog->bVisible) && (!Session || !Session->IsCharacterManagementPending());
	const bool bSelected = Characters.IsValidIndex(SelectedIndex);
	const bool bDeleted = bSelected && Characters[SelectedIndex].DeleteSeconds != 0;
	for (const TCHAR* Name : {TEXT("EnterGameButton"), TEXT("DeleteCharacterButton"), TEXT("RestoreCharacterButton")})
		if (auto Button = Manager->FindElementByName(Name))
		{
			const bool bRestore = FCString::Strcmp(Name, TEXT("RestoreCharacterButton")) == 0;
			if (bRestore || FCString::Strcmp(Name, TEXT("DeleteCharacterButton")) == 0) Button->bVisible = bRestore == bDeleted;
			Button->bActivatable = bReady && bSelected && bRestore == bDeleted;
			Button->bGhosted = !Button->bActivatable;
		}
	if(auto Create=Manager->FindElementByName(TEXT("CreateCharacterButton")))
	{
		Create->bActivatable=bReady&&Session&&Session->GetState()==EACESessionState::CharacterSelect&&
			Client->GetCharacters().Num()<int32(Session->GetCharacterSlotCount());
		Create->bGhosted=!Create->bActivatable;
	}

	auto Apply = [&](const FString& Name, uint32 Idle, uint32 HoverDid, uint32 PressDid, uint32 Alpha)
	{
		TSharedPtr<FACEUIElement> El = Manager->FindElementByName(Name);
		if (!El.IsValid())
		{
			return;
		}
		const bool bPress = El->bActivatable&&(ElementIsOrUnder(Active, El) || ElementIsOrUnder(Capture, El));
		const bool bHover = El->bActivatable&&ElementIsOrUnder(Hover, El);
		El->ImageFileId = bPress ? PressDid : (bHover ? HoverDid : Idle);
		El->AlphaFileId = Alpha;
	};

	Apply(TEXT("EnterGameButton"), DidEnterIdle, DidEnterHover, DidEnterPress, DidEnterAlpha);
	Apply(TEXT("CreateCharacterButton"), DidRedHugeIdle, DidRedHugeHover, DidRedHugePress, DidRedHugeAlpha);
	Apply(TEXT("DeleteCharacterButton"), DidRedLargeIdle, DidRedLargeHover, DidRedLargePress, DidRedLargeAlpha);
	Apply(TEXT("RestoreCharacterButton"), DidRedLargeIdle, DidRedLargeHover, DidRedLargePress, DidRedLargeAlpha);
	Apply(TEXT("CreditsButton"), DidRedLargeIdle, DidRedLargeHover, DidRedLargePress, DidRedLargeAlpha);
	Apply(TEXT("ExitButton"), DidRedLargeIdle, DidRedLargeHover, DidRedLargePress, DidRedLargeAlpha);
}

void UACEUICharSelectBinder::PlaceSlotChrome(int32 SlotIndex, float LayoutX, float LayoutY, float LayoutW, bool bSelected)
{
	const int32 SlotH = GetCharacterRowHeight();
	if (!Canvas || !Canvas->GetElementLayer() || !Canvas->WidgetTree || !Canvas->GetResourceResolver())
	{
		return;
	}
	auto EnsureBorder = [&](TArray<TObjectPtr<UBorder>>& Arr, int32 Idx) -> UBorder*
	{
		while (Arr.Num() <= Idx && Canvas->WidgetTree)
		{
			UBorder* B = Canvas->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			B->SetPadding(FMargin(0.f));
			B->SetVisibility(ESlateVisibility::HitTestInvisible);
			Canvas->GetElementLayer()->AddChild(B);
			Arr.Add(B);
		}
		return Arr.IsValidIndex(Idx) ? Arr[Idx].Get() : nullptr;
	};

	UACEUIResourceResolver* Res = Canvas->GetResourceResolver();
	const uint32 TopDid = bSelected ? DidSlotTopSel : DidSlotTopIdle;
	const uint32 MidDid = bSelected ? DidSlotMidSel : DidSlotMidIdle;
	const uint32 BotDid = bSelected ? DidSlotBotSel : DidSlotBotIdle;
	const float SX = Canvas->GetLastScaleX();
	const float SY = Canvas->GetLastScaleY();

	auto PlaceStrip = [&](UBorder* B, uint32 Did, float LX, float LY, float LW, float LH, int32 Z)
	{
		if (!B)
		{
			return;
		}
		const float VW = FMath::Max(1.f, LW * SX);
		const float VH = FMath::Max(1.f, LH * SY);
		if (UTexture2D* Tex = Res->ResolveTexture(Did))
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Tex);
			Brush.ImageSize = FVector2D(VW, VH);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			B->SetBrush(Brush);
			B->SetBrushColor(FLinearColor::White);
		}
		B->SetVisibility(ESlateVisibility::HitTestInvisible);
		PlaceScaled(static_cast<UWidget*>(B), LX, LY, LW, LH, Z);
	};

	PlaceStrip(EnsureBorder(CharSlotTops, SlotIndex), TopDid, LayoutX, LayoutY, LayoutW, 4.f, CharSelectOverlayZ);
	// Stretch the middle strip between the native four-pixel caps.
	PlaceStrip(EnsureBorder(CharSlotMiddles, SlotIndex), MidDid, LayoutX, LayoutY + 4.f, LayoutW, float(SlotH - 8), CharSelectOverlayZ);
	PlaceStrip(EnsureBorder(CharSlotBottoms, SlotIndex), BotDid, LayoutX, LayoutY + float(SlotH - 4), LayoutW, 4.f, CharSelectOverlayZ);
}

void UACEUICharSelectBinder::PlaceListFrameChrome(float LayoutX, float LayoutY, float LayoutW, float LayoutH)
{
	if (!Canvas || !Canvas->GetElementLayer() || !Canvas->WidgetTree || !Canvas->GetResourceResolver())
	{
		return;
	}
	while (ListFrameParts.Num() < 8 && Canvas->WidgetTree)
	{
		UBorder* B = Canvas->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		B->SetPadding(FMargin(0.f));
		B->SetVisibility(ESlateVisibility::HitTestInvisible);
		Canvas->GetElementLayer()->AddChild(B);
		ListFrameParts.Add(B);
	}

	UACEUIResourceResolver* Res = Canvas->GetResourceResolver();
	const float SX = Canvas->GetLastScaleX();
	const float SY = Canvas->GetLastScaleY();
	constexpr float Corner = 34.f;
	constexpr float EdgeT = 7.f;
	constexpr float EdgeS = 5.f;

	auto PlacePart = [&](int32 Idx, uint32 Did, float LX, float LY, float LW, float LH)
	{
		if (!ListFrameParts.IsValidIndex(Idx) || !ListFrameParts[Idx])
		{
			return;
		}
		UBorder* B = ListFrameParts[Idx].Get();
		const float VW = FMath::Max(1.f, LW * SX);
		const float VH = FMath::Max(1.f, LH * SY);
		if (UTexture2D* Tex = Res->ResolveTexture(Did))
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Tex);
			Brush.ImageSize = FVector2D(VW, VH);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			B->SetBrush(Brush);
			B->SetBrushColor(FLinearColor::White);
		}
		B->SetVisibility(ESlateVisibility::HitTestInvisible);
		PlaceScaled(static_cast<UWidget*>(B), LX, LY, LW, LH, CharSelectOverlayZ - 1);
	};

	const float InnerW = FMath::Max(1.f, LayoutW - Corner * 2.f);
	const float InnerH = FMath::Max(1.f, LayoutH - Corner * 2.f);
	PlacePart(0, DidListTL, LayoutX, LayoutY, Corner, Corner);
	PlacePart(1, DidListTR, LayoutX + LayoutW - Corner, LayoutY, Corner, Corner);
	PlacePart(2, DidListBL, LayoutX, LayoutY + LayoutH - Corner, Corner, Corner);
	PlacePart(3, DidListBR, LayoutX + LayoutW - Corner, LayoutY + LayoutH - Corner, Corner, Corner);
	PlacePart(4, DidListTopEdge, LayoutX + Corner, LayoutY, InnerW, EdgeT);
	PlacePart(5, DidListTopEdge, LayoutX + Corner, LayoutY + LayoutH - EdgeT, InnerW, EdgeT);
	PlacePart(6, DidListSideEdge, LayoutX, LayoutY + Corner, EdgeS, InnerH);
	PlacePart(7, DidListSideEdge, LayoutX + LayoutW - EdgeS, LayoutY + Corner, EdgeS, InnerH);
}

void UACEUICharSelectBinder::PlaceBackdrop(const TSharedPtr<FACEUIElement>& Root, const FVector2D& ViewSize)
{
	const float S = Canvas->GetLastScaleX();
	if (!BackdropImageId) BackdropImageId = FindBackdropImage(Root);
	bool bShown = false;
	if (BackdropImageId && Root.IsValid() && S > KINDA_SMALL_NUMBER && ViewSize.X >= 1.f && ViewSize.Y >= 1.f)
	{
		// The authored composition stays one centered block (ApplyEdgeAnchoredLayout); the art
		// behind it is continued outward to the window edges by ExtendedBackdrop, so the join
		// at the block's edge is invisible and nothing is letterboxed, cropped or repeated.
		const int32 ExtraW = FMath::Max(0, Root->Width - ACEUI::ReferenceWidth);
		const int32 ExtraH = FMath::Max(0, Root->Height - ACEUI::ReferenceHeight);
		// The art splits where the composition opens up (CharacterSelectColumnShift).
		const int32 Spread = UACEUIElementManager::CharacterSelectSpread(ExtraW);
		const int32 Base = (ExtraW - Spread) / 2;
		const float BlockX = float(Base), BlockY = float(ExtraH / 2);
		const FIntPoint Extent(ExtraW - Spread - Base + 1, ExtraH - ExtraH / 2 + 1);
		if (Extent != BackdropExtent || Spread != BackdropSpread || !BackdropTexture)
		{
			UACEDatSubsystem* Dat = Canvas->GetGameInstance() ? Canvas->GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr;
			if (UTexture2D* Texture = ACELobbyArt::ExtendedBackdrop(Dat, BackdropImageId, Extent.X, Extent.Y, Spread))
			{
				BackdropTexture = Texture;
				BackdropExtent = Extent;
				BackdropSpread = Spread;
			}
		}
		if (BackdropTexture)
		{
			if (BackdropTiles.IsEmpty())
			{
				UBorder* Tile = Canvas->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
				Tile->SetPadding(FMargin(0.f));
				BackdropTiles.Add(Tile);
			}
			UBorder* Tile = BackdropTiles[0];
			if (Tile->GetParent() != Canvas->GetElementLayer()) Canvas->GetElementLayer()->AddChild(Tile);
			const FVector2D TexSize(BackdropTexture->GetSizeX(), BackdropTexture->GetSizeY());
			FSlateBrush Brush;
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Brush.SetResourceObject(BackdropTexture);
			Brush.ImageSize = TexSize;
			if (BackdropBrushes.Num() != 1 || !(BackdropBrushes[0] == Brush))
			{
				BackdropBrushes = { Brush };
				Tile->SetBrush(Brush);
			}
			Tile->SetVisibility(ESlateVisibility::HitTestInvisible);
			// One art texel per layout unit, exactly like the block's own elements, so the art
			// under them lines up with the titles and frames.
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Tile->Slot))
			{
				Slot->SetAnchors(FAnchors(0.f, 0.f));
				Slot->SetAlignment(FVector2D(0.f, 0.f));
				Slot->SetAutoSize(false);
				Slot->SetPosition(FVector2D((BlockX - BackdropExtent.X) * S, (BlockY - BackdropExtent.Y) * S));
				Slot->SetSize(TexSize * S);
				Slot->SetZOrder(1);
			}
			bShown = true;
		}
	}
	for (int32 I = bShown ? 1 : 0; I < BackdropTiles.Num(); ++I)
		if (BackdropTiles[I]) BackdropTiles[I]->SetVisibility(ESlateVisibility::Collapsed);
}

void UACEUICharSelectBinder::EnsureOverlays()
{
	if (!Canvas || !Canvas->WidgetTree || !Canvas->GetElementLayer())
	{
		return;
	}
	if (!LetterboxFill)
	{
		LetterboxFill = Canvas->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		LetterboxFill->SetPadding(FMargin(0.f));
		LetterboxBrush = ColorFill(LetterboxNight);
		LetterboxFill->SetBrush(LetterboxBrush);
		LetterboxFill->SetVisibility(ESlateVisibility::HitTestInvisible);
		Canvas->GetElementLayer()->AddChild(LetterboxFill);
	}
	EnsureTextImage(WorldNameImage);
	while (CharNameImages.Num() < Characters.Num() && Canvas->WidgetTree)
	{
		TObjectPtr<UBorder> Slot;
		EnsureTextImage(Slot);
		CharNameImages.Add(Slot);
	}
	static const TCHAR* ButtonNames[] = {
		TEXT("EnterGameButton"),
		TEXT("CreateCharacterButton"),
		TEXT("DeleteCharacterButton"),
		TEXT("RestoreCharacterButton"),
		TEXT("CreditsButton"),
		TEXT("ExitButton"),
	};
	for (const TCHAR* Name : ButtonNames)
	{
		if (ButtonTextImages.Contains(Name))
		{
			continue;
		}
		auto* Label = Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>();
		Label->SetVisibility(ESlateVisibility::HitTestInvisible);
		Canvas->GetElementLayer()->AddChild(Label);
		ButtonTextImages.Add(Name, Label);
	}
}

void UACEUICharSelectBinder::RefreshOverlays()
{
	if (!bBound || !Manager || !Canvas)
	{
		return;
	}
	if (bCredits)
	{
		if (WorldNameImage) WorldNameImage->SetVisibility(ESlateVisibility::Collapsed);
		for (auto& Array : {&CharNameImages, &CharSlotTops, &CharSlotMiddles, &CharSlotBottoms, &ListFrameParts})
			for (auto& Widget : *Array) if (Widget) Widget->SetVisibility(ESlateVisibility::Collapsed);
		for (auto& Pair : ButtonTextImages) Pair.Value->SetVisibility(ESlateVisibility::Collapsed);
		for (UBorder* Tile : BackdropTiles) if (Tile) Tile->SetVisibility(ESlateVisibility::Collapsed);
		if (LetterboxFill && !(LetterboxBrush == ColorFill(LetterboxBlack)))
		{
			LetterboxBrush = ColorFill(LetterboxBlack);
			LetterboxFill->SetBrush(LetterboxBrush);
		}
		return;
	}

	// Full-viewport fill and backdrop behind the panel (viewport pixels —
	// do not run through PlaceScaled or Scale would inflate past the screen).
	{
		TSharedPtr<FACEUIElement> Root = Manager->FindElementByName(TEXT("CharacterManagementField"));
		if (!Root.IsValid()) Root = Manager->GetSyntheticRoot();
		PlaceBackdrop(Root, Canvas->GetCachedGeometry().GetLocalSize());
	}
	if (LetterboxFill)
	{
		const FVector2D Size = Canvas->GetCachedGeometry().GetLocalSize();
		LetterboxFill->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (LetterboxFill->GetParent() != Canvas->GetElementLayer())
		{
			Canvas->GetElementLayer()->AddChild(LetterboxFill);
		}
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(LetterboxFill->Slot))
		{
			Slot->SetAnchors(FAnchors(0.f, 0.f));
			Slot->SetAlignment(FVector2D(0.f, 0.f));
			Slot->SetAutoSize(false);
			Slot->SetPosition(FVector2D::ZeroVector);
			Slot->SetSize(FVector2D(FMath::Max(1.f, Size.X), FMath::Max(1.f, Size.Y)));
			Slot->SetZOrder(0);
		}
		const FSlateBrush Fill = ColorFill(LetterboxNight);
		if (!(Fill == LetterboxBrush))
		{
			LetterboxBrush = Fill;
			LetterboxFill->SetBrush(LetterboxBrush);
		}
	}

	if (WorldNameImage)
	{
		if (TSharedPtr<FACEUIElement> World = Manager->FindElementByName(TEXT("WorldName")))
		{
			const FIntPoint O = World->GetScreenOrigin();
			// WorldName art 0x06004D64 gold frame outer bbox (27,26)-(165,81).
			// Inset past the bevel so Sans20 (0x4000000B) centers in the dark well —
			// retail color override 0xFFD8AF5B, H/V justify center.
			constexpr float FrameL = 27.f;
			constexpr float FrameT = 26.f;
			constexpr float FrameR = 165.f;
			constexpr float FrameB = 81.f;
			constexpr float Bevel = 8.f;
			const float BoxX = static_cast<float>(O.X) + FrameL + Bevel;
			const float BoxY = static_cast<float>(O.Y) + FrameT + Bevel;
			const float BoxW = (FrameR - FrameL) - 2.f * Bevel;
			const float BoxH = (FrameB - FrameT) - 2.f * Bevel;
			const FString Name = ServerName.IsEmpty() ? FString() : ServerName;
			PlaceDatText(WorldNameImage.Get(), FontSans20, Name, FColor(0xD8, 0xAF, 0x5B, 0xFF),
				BoxX, BoxY, BoxW, BoxH, CharSelectOverlayZ + 2, false,
				WorldNameCacheKey, WorldNameCacheTex, WorldNameCacheW, WorldNameCacheH,
				/*bCenter*/ true);
		}
		else
		{
			WorldNameImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	struct FBtnSpec
	{
		const TCHAR* Name;
		const TCHAR* Label;
		uint32 FontId;
		FColor Color;
		bool bOutline;
		/** Layout-space Y adjustment (negative = raise). RedLarge shadow art reads low if geometrically centered. */
		float YNudge;
	};
	// Retail Button_RedLarge_Shadow (0x100002D1): font 0x4000000B, color 0xFFFFB219 (Language DAT).
	// Retail Button_RedHuge_Shadow (0x100002D2): font 0x4000000C via button_sansfont22.
	// Outline uses each font's BackgroundSurface (bold look); idle media has no outline flag.
	static const FBtnSpec ButtonSpecs[] = {
		{ TEXT("EnterGameButton"), TEXT("ENTER"), FontFancy40, FColor(0xFF, 0xF2, 0x7F, 0xFF), true, 0.f },
		{ TEXT("CreateCharacterButton"), TEXT("Create Character"), FontSans22, FColor(0xFF, 0xB2, 0x19, 0xFF), true, 0.f },
		{ TEXT("DeleteCharacterButton"), TEXT("DELETE"), FontSans20, FColor(0xFF, 0xB2, 0x19, 0xFF), true, -12.f },
		{ TEXT("RestoreCharacterButton"), TEXT("RESTORE"), FontSans20, FColor(0xFF, 0xB2, 0x19, 0xFF), true, -12.f },
		{ TEXT("CreditsButton"), TEXT("CREDITS"), FontSans20, FColor(0xFF, 0xB2, 0x19, 0xFF), true, -12.f },
		{ TEXT("ExitButton"), TEXT("EXIT"), FontSans20, FColor(0xFF, 0xB2, 0x19, 0xFF), true, -12.f },
	};
	for (const FBtnSpec& Spec : ButtonSpecs)
	{
		auto* Found = ButtonTextImages.Find(Spec.Name);
		if (!Found || !Found->Get())
		{
			continue;
		}
		TSharedPtr<FACEUIElement> El = Manager->FindElementByName(Spec.Name);
		if (!El.IsValid() || !El->bVisible)
		{
			Found->Get()->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
        UACERetailTextBlock* Label = Found->Get();
        const auto* State = El->States.Find(El->PaintState);
        Label->SetText(FText::FromString(Spec.Label));
        Label->SetColorAndOpacity(FSlateColor(State && State->TextColor.IsSet() ? State->TextColor.GetValue()
            : El->TextColor.Get(FLinearColor(Spec.Color))));
        Label->SetJustification(El->TextHorizontalJustification == 1 ? ETextJustify::Center
            : El->TextHorizontalJustification == 3 ? ETextJustify::Right : ETextJustify::Left);
        Label->SetMargin(El->TextMargins);
        Label->SetVisibility(ESlateVisibility::HitTestInvisible);
        Canvas->PlaceWidgetAtElement(Label,El,CharSelectOverlayZ+5);

	}

	if (TSharedPtr<FACEUIElement> Frame = Manager->FindElementByName(TEXT("CharacterListBoxFrame")))
	{
		// Hide unstretched DAT Box_GoldBorder children (template ~78×79) — PlaceListFrameChrome
		// draws the correctly sized 9-slice; leaving both creates a nested wrong frame.
		TArray<TSharedPtr<FACEUIElement>> Stack;
		Stack.Add(Frame);
		while (Stack.Num() > 0)
		{
			TSharedPtr<FACEUIElement> Cur = Stack.Pop(EAllowShrinking::No);
			if (!Cur.IsValid())
			{
				continue;
			}
			if (Cur != Frame && (Cur->ElementName.Contains(TEXT("corner"))
				|| Cur->ElementName.Contains(TEXT("edge"))
				|| Cur->ElementName.Contains(TEXT("Corner"))
				|| Cur->ElementName.Contains(TEXT("Edge"))
				|| Cur->ElementName.Contains(TEXT("top_left"))
				|| Cur->ElementName.Contains(TEXT("top_right"))
				|| Cur->ElementName.Contains(TEXT("bottom_left"))
				|| Cur->ElementName.Contains(TEXT("bottom_right"))
				|| Cur->ElementName.Contains(TEXT("side"))))
			{
				Cur->bVisible = false;
			}
			for (const TSharedPtr<FACEUIElement>& Ch : Cur->Children)
			{
				Stack.Add(Ch);
			}
		}
		const FIntPoint FO = Frame->GetScreenOrigin();
		PlaceListFrameChrome(
			static_cast<float>(FO.X), static_cast<float>(FO.Y),
			static_cast<float>(Frame->Width), static_cast<float>(Frame->Height));
	}
	else
	{
		for (UBorder* B : ListFrameParts)
		{
			if (B) { B->SetVisibility(ESlateVisibility::Collapsed); }
		}
	}

	TSharedPtr<FACEUIElement> List = Manager->FindElementByName(TEXT("CharacterListBox"));
	if (!List.IsValid())
	{
		for (UBorder* L : CharNameImages)
		{
			if (L) { L->SetVisibility(ESlateVisibility::Collapsed); }
		}
		return;
	}

	const FIntPoint Origin = List->GetScreenOrigin();
	const float ListW = static_cast<float>(FMath::Max(1, List->Width));
	const int32 SlotH = GetCharacterRowHeight();
	const int32 MaxRows = FMath::Max(1, List->Height / SlotH);

	for (int32 i = 0; i < CharNameImages.Num(); ++i)
	{
		UBorder* NameImg = CharNameImages[i].Get();
		if (!NameImg)
		{
			continue;
		}
		if (!Characters.IsValidIndex(i) || i >= MaxRows)
		{
			NameImg->SetVisibility(ESlateVisibility::Collapsed);
			if (CharSlotTops.IsValidIndex(i) && CharSlotTops[i])
			{
				CharSlotTops[i]->SetVisibility(ESlateVisibility::Collapsed);
			}
			if (CharSlotMiddles.IsValidIndex(i) && CharSlotMiddles[i])
			{
				CharSlotMiddles[i]->SetVisibility(ESlateVisibility::Collapsed);
			}
			if (CharSlotBottoms.IsValidIndex(i) && CharSlotBottoms[i])
			{
				CharSlotBottoms[i]->SetVisibility(ESlateVisibility::Collapsed);
			}
			continue;
		}

		const float Y = static_cast<float>(Origin.Y + i * SlotH);
		const float X = static_cast<float>(Origin.X);
		const bool bSel = (i == SelectedIndex);
		// Retail only paints CharacterSlot chrome for the selected row (idle strips are
		// unused on the char-select list — unselected names sit on the list backdrop).
		if (bSel)
		{
			PlaceSlotChrome(i, X, Y, ListW, true);
		}
		else
		{
			auto Hide = [](const TArray<TObjectPtr<UBorder>>& Arr, int32 Idx)
			{
				if (Arr.IsValidIndex(Idx) && Arr[Idx])
				{
					Arr[Idx]->SetVisibility(ESlateVisibility::Collapsed);
				}
			};
			Hide(CharSlotTops, i);
			Hide(CharSlotMiddles, i);
			Hide(CharSlotBottoms, i);
		}

		while (CharNameCacheKeys.Num() <= i)
		{
			CharNameCacheKeys.Add(FString());
			CharNameCacheTex.Add(nullptr);
			CharNameCacheW.Add(0);
			CharNameCacheH.Add(0);
		}
		// CharacterSlotTemplate (0x10000488): font 0x40000009, color 0xFFFFF2CC,
		// left_margin=6, HJustify left, vertically centered in the character row.
		constexpr float SlotLeftMargin = 6.f;
		PlaceDatText(NameImg, FontSans16, Characters[i].Name, FColor(0xFF, 0xF2, 0xCC, 0xFF),
			X + SlotLeftMargin, Y, ListW - SlotLeftMargin - 4.f, static_cast<float>(SlotH),
			CharSelectOverlayZ + 3, false,
			CharNameCacheKeys[i], CharNameCacheTex[i], CharNameCacheW[i], CharNameCacheH[i],
			/*bCenter*/ false);
	}
}
