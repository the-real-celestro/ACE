#include "ACELoginWidget.h"
#include "ACELobbyArt.h"
#include "ACELobbyAccountList.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIFontStyles.h"
#include "UI/ACEUIResourceResolver.h"
#include "Mods/ACEPluginPanel.h"
#include "ACELoginSettings.h"
#include "ACEClientBuild.h"
#include "ACEUpdateSubsystem.h"
#include "Components/ProgressBar.h"
#include "Components/CheckBox.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "UnrealClient.h"
#include "Algo/StableSort.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/NativeWidgetHost.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Engine/GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include <shobjidl.h>
#endif

namespace
{
	const FLinearColor Ink(.008f, .006f, .003f, 1);
	const FLinearColor Surface(.018f, .014f, .009f, 1);
	const FLinearColor Tile(.045f, .036f, .025f, 1);
	const FLinearColor Gold(.36f, .27f, .14f, 1);
	const FLinearColor Muted(.75f, .68f, .53f, 1);
	const FLinearColor LobbyTextColor(.94f, .89f, .77f, 1);
	/** The retail art each launcher button is drawn with (UACELoginActionButton::LobbyStyle). */
	enum ELobbyStyle : uint8 { LobbyRed = 0, LobbySmall = 1, LobbyRow = 2, LobbyRing = 3, LobbyGlyph = 4 };
	/** Logical lobby size; everything is laid out at this scale and the root ScaleBox fits it to the window. */
	constexpr float DesignW = 1440.f, DesignH = 900.f;
	constexpr float ServerRowHeight = 52.f;
	// Draw with Slate primitives so the eye works before DAT loads and in packages.
	class SPasswordVisibilityIcon : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPasswordVisibilityIcon) {} SLATE_ATTRIBUTE(bool, Revealed) SLATE_END_ARGS()
		void Construct(const FArguments& Args)
		{
			Revealed=Args._Revealed;
			for(int32 I=0;I<=32;++I){const float T=I/32.f;Outline.Add(FVector2D(2+20*T,10-6*FMath::Sin(PI*T)));}
			for(int32 I=32;I>=0;--I){const float T=I/32.f;Outline.Add(FVector2D(2+20*T,10+6*FMath::Sin(PI*T)));}
			for(int32 I=0;I<=24;++I){const float A=2*PI*I/24.f;Pupil.Add(FVector2D(12+3*FMath::Cos(A),10+3*FMath::Sin(A)));}
		}
		FVector2D ComputeDesiredSize(float) const override {return FVector2D(24,20);}
		int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override
		{
			const double Scale=FMath::Min(G.GetLocalSize().X/24.,G.GetLocalSize().Y/20.);
			const auto Art=G.MakeChild(FVector2D(24,20),FSlateLayoutTransform(Scale,(G.GetLocalSize()-FVector2D(24,20)*Scale)*.5));
			const auto Effect=ShouldBeEnabled(Enabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect;
			const auto Color=LobbyTextColor*Style.GetColorAndOpacityTint();
			for(const auto* Path:{&Outline,&Pupil})FSlateDrawElement::MakeLines(Out,Layer,Art.ToPaintGeometry(),*Path,Effect,Color,true,1.6f);
			if(Revealed.Get())FSlateDrawElement::MakeLines(Out,Layer,Art.ToPaintGeometry(),{FVector2D(3,2),FVector2D(21,18)},Effect,Color,true,1.8f);
			return Layer;
		}
	private:
		TAttribute<bool> Revealed;
		TArray<FVector2D> Outline,Pupil;
	};
	uint8 LobbyStyleFor(const FString& Action)
	{
		if (Action==TEXT("launch")) return LobbyRing;
		if (Action==TEXT("selectserver") || Action==TEXT("selectaccount") || Action==TEXT("directoryselect")) return LobbyRow;
		if (Action.StartsWith(TEXT("sort")) || Action==TEXT("togglepassword")) return LobbySmall;
		if (Action==TEXT("clearsearch") || Action.StartsWith(TEXT("scrollaccounts"))) return LobbyGlyph;
		return LobbyRed;
	}
	/** Retail art once the DAT has loaded (ACELobbyArt), otherwise flat boxes in the same layout. */
	void ButtonStyle(UButton* Button, bool Accent)
	{
		auto* Action=Cast<UACELoginActionButton>(Button);
		const uint8 Kind=Action?Action->LobbyStyle:uint8(LobbyRed);
		if (Action) Action->bLobbyAccent=Accent;
		const FACELobbyArt* Art=ACELobbyArt::Get();
		FButtonStyle Style;
		FMargin Padding(14,7);
		auto Dim=[](FSlateBrush Brush, float Amount){ Brush.TintColor=FLinearColor(Amount,Amount,Amount,1); return Brush; };
		if (Kind==LobbyGlyph)
		{
			const bool Up=Action && Action->Action==TEXT("scrollaccountsup"), Down=Action && Action->Action==TEXT("scrollaccountsdown");
			if (Art && (Up || Down))
			{
				const FSlateBrush* Arrow=Up?Art->ArrowUp:Art->ArrowDown;
				Style.SetNormal(Arrow[0]).SetHovered(Arrow[1]).SetPressed(Arrow[1]).SetDisabled(Dim(Arrow[0],.45f));
			}
			else
			{
				const FSlateNoResource None;
				Style.SetNormal(Up||Down?FSlateBrush(FSlateRoundedBoxBrush(Tile,2.f,Gold,1.f)):FSlateBrush(None));
				Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.08f),3.f)).SetPressed(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.2f),3.f)).SetDisabled(None);
			}
			Padding=FMargin(0);
		}
		else if (Art)
		{
			if (Kind==LobbyRing)
			{
				Style.SetNormal(Art->Ring[0]).SetHovered(Art->Ring[1]).SetPressed(Art->Ring[1]).SetDisabled(Dim(Art->Ring[0],.45f));
				Padding=FMargin(0);
			}
			else if (Kind==LobbyRow)
			{
				Style.SetNormal(Art->Row[Accent?2:0]).SetHovered(Art->Row[Accent?2:1]).SetPressed(Art->Row[2]).SetDisabled(Dim(Art->Row[0],.6f));
				Padding=FMargin(12,4);
			}
			else
			{
				const FSlateBrush* Plate=Kind==LobbySmall?Art->SmallButton:Art->Button;
				Style.SetNormal(Plate[Accent?2:0]).SetHovered(Plate[Accent?2:1]).SetPressed(Plate[2]).SetDisabled(Dim(Plate[0],.4f));
				// The plate art carries a drop shadow on its right and bottom edges.
				Padding=Kind==LobbySmall?FMargin(11,5,13,8):FMargin(22,12,26,16);
			}
		}
		else
		{
			const float Radius=Kind==LobbyRing?145.f:0.f;
			const FLinearColor Base=Kind==LobbyRing||Accent?FLinearColor(.16f,.035f,.02f,1):Kind==LobbyRow?FLinearColor(.02f,.03f,.06f,.8f):FLinearColor(.11f,.03f,.02f,1);
			Style.SetNormal(FSlateRoundedBoxBrush(Base,Radius,Accent||Kind==LobbyRing?FLinearColor(.8f,.55f,.2f,1):FLinearColor(.4f,.3f,.16f,1),Kind==LobbyRing?3.f:1.f));
			Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.22f,.06f,.03f,1),Radius,FLinearColor(.9f,.66f,.3f,1),Kind==LobbyRing?3.f:1.f));
			Style.SetPressed(FSlateRoundedBoxBrush(Ink,Radius,Gold,1.f));
			Style.SetDisabled(FSlateRoundedBoxBrush(Surface,Radius,FLinearColor(.07f,.06f,.04f,1),1.f));
			Padding=Kind==LobbyRing?FMargin(0):Kind==LobbySmall?FMargin(10,4):Kind==LobbyRow?FMargin(12,4):FMargin(18,9);
		}
		Style.SetNormalPadding(Padding);
		Style.SetPressedPadding(Kind==LobbyRed||Kind==LobbySmall?FMargin(Padding.Left+1,Padding.Top+1,Padding.Right-1,Padding.Bottom-1):Padding);
		Button->SetStyle(Style);
		if (auto* Size=Cast<USizeBox>(Button->GetContent()))
			if (auto* Caption=Cast<UTextBlock>(Size->GetContent()))
				// Same caption colors as character select: Enter, red plates, and slot rows.
				Caption->SetColorAndOpacity(Kind==LobbyRing?ACELobbyArt::EnterText:Kind==LobbyRow?(Accent?FLinearColor(1.f,.98f,.9f,1):ACELobbyArt::RowText)
					:Accent?ACELobbyArt::Highlight:ACELobbyArt::CaptionText);
	}
	void AddLine(UVerticalBox* Box, UWidget* Child, float Bottom = 12)
	{
		Box->AddChildToVerticalBox(Child)->SetPadding(FMargin(0,0,0,Bottom));
	}
	UACEUIResourceResolver* UiResources(const UUserWidget* Widget)
	{
		if (auto* GI=Widget->GetGameInstance()) if (auto* Dat=GI->GetSubsystem<UACEDatSubsystem>()) return Dat->GetUiResources();
		return nullptr;
	}
	FString TimeOfDay(const FDateTime& T)
	{
		return FString::Printf(TEXT("%d:%02d %s"),T.GetHour12(),T.GetMinute(),T.IsMorning()?TEXT("AM"):TEXT("PM"));
	}
}

/** Retail progress-meter art from the connect layout (local DAT 0x21000000): fifteen
 *  animation frames each of the filled (blue mana) and empty glass vial, 325x52. */
struct FACEVialArt
{
	TArray<FSlateBrush> Full, Empty;
	bool IsReady() const { return Full.Num() > 0 && Full.Num() == Empty.Num(); }
};

namespace
{
	constexpr uint32 ACETitleLogoId = 0x06001343u; // 800x482 gold-ring title art
	const uint32 ACEVialFullIds[] = {0x0600195Cu,0x0600195Du,0x0600195Eu,0x0600195Fu,0x06001960u,0x06001961u,0x06001962u,0x06001963u,
		0x06001964u,0x06001965u,0x06001970u,0x06001971u,0x06001972u,0x06001973u,0x06001974u};
	const uint32 ACEVialEmptyIds[] = {0x06001966u,0x06001967u,0x06001968u,0x06001969u,0x0600196Au,0x0600196Bu,0x0600196Cu,0x0600196Du,
		0x0600196Eu,0x0600196Fu,0x06001975u,0x06001976u,0x06001977u,0x06001978u,0x06001979u};
	constexpr float VialW = 325.f, VialH = 52.f;

	/** Draws one retail vial: the empty glass, then the filled frame revealed left to right
	 *  with a gently rippling front edge. Falls back to a plain bar until DAT art loads. */
	class SACEManaVial : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SACEManaVial) {} SLATE_ATTRIBUTE(float, Fill) SLATE_ARGUMENT(TSharedPtr<FACEVialArt>, Art) SLATE_END_ARGS()
		void Construct(const FArguments& Args)
		{
			Fill = Args._Fill; Art = Args._Art;
			RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SACEManaVial::Animate));
		}
		EActiveTimerReturnType Animate(double, float)
		{
			Invalidate(EInvalidateWidgetReason::Paint);
			return EActiveTimerReturnType::Continue;
		}
		FVector2D ComputeDesiredSize(float) const override { return FVector2D(VialW, VialH); }
		int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override
		{
			const FVector2D Size = G.GetLocalSize();
			if (Size.X <= 0 || Size.Y <= 0) return Layer;
			const double Scale = FMath::Min(Size.X / VialW, Size.Y / VialH);
			const FGeometry ArtGeo = G.MakeChild(FVector2D(VialW, VialH), FSlateLayoutTransform(Scale, (Size - FVector2D(VialW, VialH) * Scale) * .5));
			const float F = FMath::Clamp(Fill.Get(0.f), 0.f, 1.f);
			const double Now = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : FPlatformTime::Seconds();
			const FLinearColor Tint = Style.GetColorAndOpacityTint();
			if (Art.IsValid() && Art->IsReady())
			{
				const int32 Frame = FMath::Clamp(int32(FMath::Fmod(Now * 12.0, double(Art->Full.Num()))), 0, Art->Full.Num() - 1);
				FSlateDrawElement::MakeBox(Out, Layer, ArtGeo.ToPaintGeometry(), &Art->Empty[Frame], ESlateDrawEffect::None, Tint);
				if (F > 0.f)
				{
					// Horizontal bands each get their own edge offset so the mana front ripples.
					constexpr int32 Bands = 13; constexpr float BandH = VialH / Bands;
					const float Ripple = F < .995f ? 3.f * FMath::Min(1.f, F * 20.f) : 0.f;
					for (int32 B = 0; B < Bands; ++B)
					{
						const float Y0 = B * BandH;
						const float X = FMath::Clamp(F * VialW + Ripple * float(FMath::Sin(Now * 5.0 + B * .8)), 0.f, VialW);
						if (X <= .5f) continue;
						FSlateBrush Slice = Art->Full[Frame];
						Slice.SetUVRegion(FBox2f(FVector2f(0.f, Y0 / VialH), FVector2f(X / VialW, (Y0 + BandH) / VialH)));
						FSlateDrawElement::MakeBox(Out, Layer + 1, ArtGeo.ToPaintGeometry(FVector2D(X, BandH), FSlateLayoutTransform(FVector2D(0, Y0))),
							&Slice, ESlateDrawEffect::None, Tint);
					}
				}
				return Layer + 1;
			}
			static const FSlateRoundedBoxBrush Glass(FLinearColor(.02f, .018f, .015f, 1), 10.f, FLinearColor(.25f, .2f, .12f, 1), 1.f);
			static const FSlateRoundedBoxBrush Mana(FLinearColor(.05f, .3f, 1.f, 1), 8.f);
			FSlateDrawElement::MakeBox(Out, Layer, ArtGeo.ToPaintGeometry(FVector2D(VialW - 16, 18), FSlateLayoutTransform(FVector2D(8, 17))), &Glass, ESlateDrawEffect::None, Tint);
			if (F > 0.f)
				FSlateDrawElement::MakeBox(Out, Layer + 1, ArtGeo.ToPaintGeometry(FVector2D((VialW - 20) * F, 12), FSlateLayoutTransform(FVector2D(10, 20))), &Mana, ESlateDrawEffect::None, Tint);
			return Layer + 1;
		}
	private:
		TAttribute<float> Fill;
		TSharedPtr<FACEVialArt> Art;
	};
}

void UACELoginActionButton::HandleAction()
{
	// The callback can rebuild its list, including this button.
	const FString Command = Action, Argument = Value;
	if (Owner.IsValid()) Owner->RunAction(Command, Argument);
}


UTextBlock* UACELoginWidget::Label(const FString& String, int32 Size, bool bMuted, float BitmapScale)
{
	auto* T = WidgetTree->ConstructWidget<UACERetailTextBlock>();
	T->SetText(FText::FromString(String));
	T->SetFont(FCoreStyle::GetDefaultFontStyle(Size >= 26 ? TEXT("Bold") : TEXT("Regular"), Size));
	// The authored serif glyphs match the rest of the client. Until DAT is
	// available, UACERetailTextBlock keeps its readable engine-font fallback.
	const int32 Pixels=Size>=26?30:FMath::Clamp(Size,12,24);
	T->SetFontOverride(ACEUIFontStyles::FontIdForStyleName((Size>=26?FString(TEXT("fancyfont")):FString(TEXT("basefont")))+FString::FromInt(Pixels)));
	// Records the scale even before the DAT loads; NativeTick re-applies it once fonts are ready.
	T->SetRetailElement(UiResources(this),nullptr,FVector2D(BitmapScale),0,false);
	T->SetColorAndOpacity(bMuted ? Muted : LobbyTextColor);
	T->SetAutoWrapText(true);
	T->SetVisibility(ESlateVisibility::HitTestInvisible);
	return T;
}

UTextBlock* UACELoginWidget::Heading(const FString& String, int32 FancySize)
{
	auto* T = Cast<UACERetailTextBlock>(Label(String,26,false,1.f));
	T->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),FMath::Max(14,FancySize*3/4)));
	T->SetFontOverride(ACEUIFontStyles::FontIdForStyleName(FString::Printf(TEXT("fancyfont%d"),FancySize)));
	T->SetRetailElement(UiResources(this),nullptr,FVector2D(1.35f),0,false);
	T->SetColorAndOpacity(ACELobbyArt::Gold);
	T->SetShadowOffset(FVector2D(1,1)); T->SetShadowColorAndOpacity(FLinearColor(0,0,0,.85f));
	T->SetAutoWrapText(false);
	return T;
}

UWidget* UACELoginWidget::Frame(UWidget* Content, const FMargin& Padding)
{
	auto* Layers=WidgetTree->ConstructWidget<UOverlay>();
	auto* Host=WidgetTree->ConstructWidget<UNativeWidgetHost>(); Host->SetVisibility(ESlateVisibility::HitTestInvisible);
	TSharedRef<SACELobbyFrame> Painted=SNew(SACELobbyFrame);
	Host->SetContent(Painted); LobbyFrames.Add(Painted);
	auto* HostSlot=Layers->AddChildToOverlay(Host); HostSlot->SetHorizontalAlignment(HAlign_Fill); HostSlot->SetVerticalAlignment(VAlign_Fill);
	if (Content)
	{
		auto* ContentSlot=Layers->AddChildToOverlay(Content); ContentSlot->SetPadding(Padding);
		ContentSlot->SetHorizontalAlignment(HAlign_Fill); ContentSlot->SetVerticalAlignment(VAlign_Fill);
	}
	return Layers;
}

UACELoginActionButton* UACELoginWidget::ActionButton(const FString& Title, const FString& Action, const FString& Value, bool bAccent)
{
	auto* B = WidgetTree->ConstructWidget<UACELoginActionButton>(UACELoginActionButton::StaticClass(),Action==TEXT("launch")?FName(TEXT("LoginButton")):NAME_None);
	B->Action = Action; B->Value = Value; B->Owner = this;
	B->LobbyStyle = LobbyStyleFor(Action);
	B->OnClicked.AddDynamic(B, &UACELoginActionButton::HandleAction);
	const bool bSmall=B->LobbyStyle==LobbySmall;
	const bool bRow=B->LobbyStyle==LobbyRow;
	auto* T = Label(Title, bSmall?14:16, false, bRow?1.1f:1.f); T->SetAutoWrapText(false); T->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	// Character select draws its buttons in sansfont20 and its slot rows in sansfont16.
	if (auto* Retail=Cast<UACERetailTextBlock>(T))
	{
		Retail->SetFontOverride(ACEUIFontStyles::FontIdForStyleName(bSmall||bRow?TEXT("sansfont16"):TEXT("sansfont20")));
		Retail->SetRetailElement(UiResources(this),nullptr,Retail->GetBitmapScale(),0,false);
		if (!bRow) { Retail->SetShadowOffset(FVector2D(1,1)); Retail->SetShadowColorAndOpacity(FLinearColor(0,0,0,.85f)); }
	}
	T->SetJustification(ETextJustify::Center);
	auto* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetMinDesiredHeight(bSmall?18:26);
	Size->SetContent(T); Cast<USizeBoxSlot>(T->Slot)->SetVerticalAlignment(VAlign_Center);
	B->SetContent(Size);
	ButtonStyle(B, bAccent);
	return B;
}

UEditableTextBox* UACELoginWidget::Field(UVerticalBox* Parent, const FString& Title, const FName Name, bool bSecret)
{
	AddLine(Parent, Label(Title, 16, true), 5);
	auto* Box = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), Name);
	FEditableTextBoxStyle Style = Box->GetWidgetStyle();
	Style.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Ink, 0.f, Muted*FLinearColor(1,1,1,.6f), 1.f));
	Style.SetBackgroundImageHovered(FSlateRoundedBoxBrush(Surface, 0.f, Gold, 1.f));
	Style.SetBackgroundImageFocused(FSlateRoundedBoxBrush(Ink, 0.f, ACELobbyArt::Gold, 2.f));
	Style.SetForegroundColor(LobbyTextColor); Style.SetPadding(FMargin(12,7));
	Style.SetTextStyle(FTextBlockStyle().SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),20)).SetColorAndOpacity(LobbyTextColor));
	Box->SetWidgetStyle(Style); Box->SetIsPassword(bSecret); Box->SetClearKeyboardFocusOnCommit(true);
	Box->SetSelectAllTextWhenFocused(true);
	AddLine(Parent, Box, 12);
	return Box;
}

TSharedRef<SWidget> UACELoginWidget::RebuildWidget()
{
	EnsureDefaultLayout(); // Build before Super captures the root Slate widget.
	return Super::RebuildWidget();
}

void UACELoginWidget::EnsureDefaultLayout()
{
	if (HostBox || !WidgetTree) return;
	SetIsFocusable(true); SetVisibility(ESlateVisibility::Visible);
	ResponsiveScale=WidgetTree->ConstructWidget<UScaleBox>(); ResponsiveScale->SetStretch(EStretch::UserSpecified);
	WidgetTree->RootWidget=ResponsiveScale;
	auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(); ResponsiveScale->SetContent(Canvas);
	// UScaleBoxSlot defaults to Center, which would allocate only the canvas's
	// tiny desired size instead of the viewport and clip all anchored controls.
	Cast<UScaleBoxSlot>(Canvas->Slot)->SetHorizontalAlignment(HAlign_Fill);
	Cast<UScaleBoxSlot>(Canvas->Slot)->SetVerticalAlignment(VAlign_Fill);
	BackdropBorder = WidgetTree->ConstructWidget<UBorder>();
	BackdropBorder->SetBrush(FSlateRoundedBoxBrush(Ink,0.f)); BackdropBorder->SetPadding(FMargin(0));
	auto* BackSlot = Canvas->AddChildToCanvas(BackdropBorder); BackSlot->SetAnchors(FAnchors(0,0,1,1)); BackSlot->SetOffsets(FMargin(0));
	SetUseDatIntroBackdrop(bUseDatIntroBackdrop);
	auto Place=[&](UWidget* W){ auto* S=Canvas->AddChildToCanvas(W); S->SetAnchors(FAnchors(0,0)); return S; };
	auto Fill=FSlateChildSize(ESlateSizeRule::Fill);
	// Retail character-select backdrop (0x06007075). Positions are set by LayoutLobby.
	LobbyBackgroundSide=WidgetTree->ConstructWidget<UImage>(); LobbyBackgroundSide->SetVisibility(ESlateVisibility::Collapsed);
	LobbyBackgroundSide->SetRenderOpacity(0); Place(LobbyBackgroundSide);
	LobbyBackground=WidgetTree->ConstructWidget<UImage>(); LobbyBackground->SetVisibility(ESlateVisibility::HitTestInvisible);
	LobbyBackground->SetRenderOpacity(0); Place(LobbyBackground);
	LobbyBrand=Heading(TEXT("Asheron's Call"),40); LobbyBrand->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(LobbyBrand)->SetAlignment(FVector2D(.5f,0));
#if PLATFORM_ANDROID
	LobbySubtitle=WidgetTree->ConstructWidget<UTextBlock>(); LobbySubtitle->SetText(FText::FromString(TEXT("VR")));
#else
	LobbySubtitle=WidgetTree->ConstructWidget<UTextBlock>(); LobbySubtitle->SetText(FText::FromString(TEXT("UNREAL")));
#endif
	LobbySubtitle->SetColorAndOpacity(FLinearColor(.78f,.8f,.84f,1)); LobbySubtitle->SetShadowOffset(FVector2D(1,2));
	LobbySubtitle->SetShadowColorAndOpacity(FLinearColor(0,0,0,.9f)); LobbySubtitle->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(LobbySubtitle)->SetAlignment(FVector2D(.5f,0));
	auto Glyph=[&](SACELobbyGlyph::EKind Kind, float Size, TAttribute<FLinearColor> Color, TAttribute<bool> Filled) -> UWidget*
	{
		auto* Host=WidgetTree->ConstructWidget<UNativeWidgetHost>(); Host->SetVisibility(ESlateVisibility::HitTestInvisible);
		Host->SetContent(SNew(SACELobbyGlyph).Kind(Kind).Size(Size).Color(Color).Filled(Filled));
		auto* Box=WidgetTree->ConstructWidget<USizeBox>(); Box->SetWidthOverride(Size); Box->SetHeightOverride(Size); Box->SetContent(Host);
		return Box;
	};
	auto SmallCaps=[&](const FString& Text, bool bGold){ auto* T=Label(Text,12,!bGold,1.f); T->SetAutoWrapText(false); if(bGold) T->SetColorAndOpacity(ACELobbyArt::Gold); return T; };

	// Left column: saved servers, then the account picker.
	SidebarSize=WidgetTree->ConstructWidget<USizeBox>(); Place(SidebarSize);
	auto* Left=WidgetTree->ConstructWidget<UVerticalBox>(); SidebarSize->SetContent(Left);
	AddLine(Left,Heading(TEXT("Your Servers"),30),2);
	ServerListSize=WidgetTree->ConstructWidget<USizeBox>(); ServerListSize->SetHeightOverride(ServerRowHeight);
	ServerList = WidgetTree->ConstructWidget<UScrollBox>(); ServerList->SetScrollbarThickness(FVector2D(8,8)); ServerListSize->SetContent(ServerList);
	AddLine(Left,Frame(ServerListSize,FMargin(12,12,12,10)),8);
	auto* ServerActions=WidgetTree->ConstructWidget<UHorizontalBox>(); AddLine(Left,ServerActions,8);
	auto* NewServerSlot=ServerActions->AddChildToHorizontalBox(ActionButton(TEXT("+ Custom server"),TEXT("newserver")));
	NewServerSlot->SetSize(Fill); NewServerSlot->SetPadding(FMargin(0,0,4,0));
	ServerActions->AddChildToHorizontalBox(ActionButton(TEXT("Browse directory"),TEXT("browser")))->SetSize(Fill);
	AccountsHeading=Heading(TEXT("Accounts"),30); AddLine(Left,AccountsHeading,2);
	auto* Panel=WidgetTree->ConstructWidget<UVerticalBox>();
	auto* PanelSlot=Left->AddChildToVerticalBox(Frame(Panel,FMargin(14,14,14,12))); PanelSlot->SetSize(Fill); PanelSlot->SetPadding(FMargin(0,0,0,8));
	// Search: the retail field with a magnifier, and a clear button once there is text.
	auto* Search=WidgetTree->ConstructWidget<UOverlay>(); AddLine(Panel,Search,10);
	AccountSearchBox=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),TEXT("AccountSearch"));
	{
		FEditableTextBoxStyle Style=AccountSearchBox->GetWidgetStyle();
		const FSlateRoundedBoxBrush Idle(FLinearColor(.004f,.008f,.02f,.85f),4.f,FLinearColor(.45f,.36f,.2f,1),1.f), Focus(FLinearColor(.004f,.008f,.02f,.9f),4.f,ACELobbyArt::Gold,1.f);
		Style.SetBackgroundImageNormal(Idle).SetBackgroundImageHovered(Idle).SetBackgroundImageFocused(Focus).SetBackgroundImageReadOnly(Idle);
		Style.SetForegroundColor(LobbyTextColor); Style.SetPadding(FMargin(36,8,32,8));
		Style.SetTextStyle(FTextBlockStyle().SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),13)).SetColorAndOpacity(LobbyTextColor));
		AccountSearchBox->SetWidgetStyle(Style);
	}
	AccountSearchBox->SetHintText(FText::FromString(TEXT("Search accounts \u2014 or just type")));
	AccountSearchBox->OnTextChanged.AddDynamic(this,&UACELoginWidget::OnAccountSearchChanged);
	AccountSearchBox->OnTextCommitted.AddDynamic(this,&UACELoginWidget::OnAccountSearchCommitted);
	auto* SearchSlot=Search->AddChildToOverlay(AccountSearchBox); SearchSlot->SetHorizontalAlignment(HAlign_Fill); SearchSlot->SetVerticalAlignment(VAlign_Fill);
	auto* MagnifierSlot=Search->AddChildToOverlay(Glyph(SACELobbyGlyph::EKind::Magnifier,15,ACELobbyArt::Muted,false));
	MagnifierSlot->SetHorizontalAlignment(HAlign_Left); MagnifierSlot->SetVerticalAlignment(VAlign_Center); MagnifierSlot->SetPadding(FMargin(13,0,0,0));
	auto* Clear=ActionButton(TEXT(""),TEXT("clearsearch")); Clear->SetToolTipText(FText::FromString(TEXT("Clear search (Esc)")));
	Clear->SetContent(Glyph(SACELobbyGlyph::EKind::Cross,14,ACELobbyArt::Muted,false));
	auto* ClearSlot=Search->AddChildToOverlay(Clear); ClearSlot->SetHorizontalAlignment(HAlign_Right); ClearSlot->SetVerticalAlignment(VAlign_Center); ClearSlot->SetPadding(FMargin(0,0,9,0));
	AccountSearchClear=Clear; Clear->SetVisibility(ESlateVisibility::Collapsed);
	// Favourites are pinned above the full list.
	auto* FavoriteRow=WidgetTree->ConstructWidget<UHorizontalBox>(); FavoriteHeader=FavoriteRow; AddLine(Panel,FavoriteRow,5);
	auto* FavoriteStar=FavoriteRow->AddChildToHorizontalBox(Glyph(SACELobbyGlyph::EKind::Star,12,ACELobbyArt::Gold,true));
	FavoriteStar->SetVerticalAlignment(VAlign_Center); FavoriteStar->SetPadding(FMargin(2,0,6,0));
	FavoriteRow->AddChildToHorizontalBox(SmallCaps(TEXT("FAVORITES"),true))->SetVerticalAlignment(VAlign_Center);
	FavoriteSize=WidgetTree->ConstructWidget<USizeBox>(); AddLine(Panel,FavoriteSize,10);
	FavoriteList=WidgetTree->ConstructWidget<UACELobbyAccountListView>(UACELobbyAccountListView::StaticClass(),TEXT("FavoriteList"));
	FavoriteSize->SetContent(FavoriteList);
	auto* AllRow=WidgetTree->ConstructWidget<UHorizontalBox>(); AddLine(Panel,AllRow,6);
	auto* AllSlot=AllRow->AddChildToHorizontalBox(SmallCaps(TEXT("ALL ACCOUNTS"),true)); AllSlot->SetSize(Fill); AllSlot->SetVerticalAlignment(VAlign_Center);
	AllRow->AddChildToHorizontalBox(SmallCaps(TEXT("Sort"),false))->SetVerticalAlignment(VAlign_Center);
	for (const auto& Sort : TArray<TPair<FString,FString>>{{TEXT("A\u2013Z"),TEXT("sortaz")},{TEXT("Recent"),TEXT("sortrecent")},{TEXT("Custom"),TEXT("sortcustom")}})
	{
		auto* B=ActionButton(Sort.Key,Sort.Value); SortButtons.Add(B);
		auto* SortSlot=AllRow->AddChildToHorizontalBox(B); SortSlot->SetPadding(FMargin(4,0,0,0)); SortSlot->SetVerticalAlignment(VAlign_Center);
	}
	SortButtons[2]->SetToolTipText(FText::FromString(TEXT("Your own order. Drag accounts on the Accounts tab to change it.")));
	auto* ListArea=WidgetTree->ConstructWidget<UOverlay>();
	auto* ListAreaSlot=Panel->AddChildToVerticalBox(ListArea); ListAreaSlot->SetSize(Fill);
	AccountList=WidgetTree->ConstructWidget<UACELobbyAccountListView>(UACELobbyAccountListView::StaticClass(),TEXT("AccountList"));
	auto* ListSlot=ListArea->AddChildToOverlay(AccountList); ListSlot->SetHorizontalAlignment(HAlign_Fill); ListSlot->SetVerticalAlignment(VAlign_Fill);
	AccountsEmpty=Label(TEXT(""),14,true,1.f); AccountsEmpty->SetVisibility(ESlateVisibility::Collapsed);
	auto* EmptySlot=ListArea->AddChildToOverlay(AccountsEmpty); EmptySlot->SetHorizontalAlignment(HAlign_Fill); EmptySlot->SetVerticalAlignment(VAlign_Top); EmptySlot->SetPadding(FMargin(4,6));
	for (int32 I=0;I<2;++I)
	{
		auto* Arrow=ActionButton(TEXT(""),I?TEXT("scrollaccountsdown"):TEXT("scrollaccountsup"));
		auto* ArrowSize=WidgetTree->ConstructWidget<USizeBox>(); ArrowSize->SetWidthOverride(16); ArrowSize->SetHeightOverride(16); Arrow->SetContent(ArrowSize);
		auto* ArrowSlot=ListArea->AddChildToOverlay(Arrow); ArrowSlot->SetHorizontalAlignment(HAlign_Right); ArrowSlot->SetVerticalAlignment(I?VAlign_Bottom:VAlign_Top);
		Arrow->SetVisibility(ESlateVisibility::Collapsed);
		(I?AccountScrollDown:AccountScrollUp)=Arrow;
	}
	AddLine(Left,Label(PLATFORM_LINUX ? TEXT("Account names are saved. Enter your password on the Accounts tab after restarting.") : PLATFORM_MAC ? TEXT("Accounts are saved on this Mac. Passwords are stored in your macOS Keychain.") : TEXT("Accounts are saved securely on this device."),13,true,1.f),0);

	// Right column: tabs, any status or confirmation, then the active page.
	DashboardSize = WidgetTree->ConstructWidget<USizeBox>(); Place(DashboardSize);
	auto* Root = WidgetTree->ConstructWidget<UVerticalBox>(); DashboardSize->SetContent(Root);
	auto* TabRow = WidgetTree->ConstructWidget<UHorizontalBox>(); AddLine(Root,TabRow,8);
	auto* Header = WidgetTree->ConstructWidget<UWrapBox>(); Header->SetInnerSlotPadding(FVector2D(4,4));
	TabRow->AddChildToHorizontalBox(Header)->SetSize(Fill);
	for (const auto& Item : TArray<TPair<FString,FString>>{{TEXT("Play"),TEXT("play")},{TEXT("Accounts"),TEXT("accounts")},{TEXT("Browse servers"),TEXT("browser")},{TEXT("Game files"),TEXT("files")}})
	{
		auto* Button=ActionButton(Item.Key,Item.Value); Header->AddChildToWrapBox(Button); NavigationButtons.Add(Button);
	}
	UpdateNavButton=ActionButton(TEXT("Updates"),TEXT("updates")); Header->AddChildToWrapBox(UpdateNavButton); NavigationButtons.Add(UpdateNavButton);
	auto* PluginsButton=ActionButton(TEXT("Plugins"),TEXT("plugins")); Header->AddChildToWrapBox(PluginsButton); NavigationButtons.Add(PluginsButton);
	auto* QuitSlot=TabRow->AddChildToHorizontalBox(ActionButton(TEXT("Quit"),TEXT("quit")));
	QuitSlot->SetHorizontalAlignment(HAlign_Right); QuitSlot->SetVerticalAlignment(VAlign_Top); QuitSlot->SetPadding(FMargin(8,0,0,0));
	auto Card = [&](UVerticalBox*& Content, bool bScroll) -> UBorder*
	{
		auto* B = WidgetTree->ConstructWidget<UBorder>(); B->SetBrush(FSlateNoResource()); B->SetPadding(FMargin(0));
		Content = WidgetTree->ConstructWidget<UVerticalBox>();
		UWidget* Inner=Content;
		if (bScroll)
		{
			// Tall forms scroll inside the frame instead of pushing the shell off small windows.
			auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(); Scroll->SetScrollbarThickness(FVector2D(8,8)); Scroll->AddChild(Content); Inner=Scroll;
		}
		B->SetContent(Frame(Inner,FMargin(22)));
		return B;
	};
	auto Row = [&](UVerticalBox* Parent, const TArray<TPair<FString,FString>>& Buttons)
	{
		auto* R = WidgetTree->ConstructWidget<UWrapBox>(); R->SetInnerSlotPadding(FVector2D(6,6)); AddLine(Parent,R);
		for (const auto& Item : Buttons) R->AddChildToWrapBox(ActionButton(Item.Key, Item.Value));
	};
	// Inline destructive-action confirmation is also fully usable with VR pointers.
	FooterSize=WidgetTree->ConstructWidget<USizeBox>(); AddLine(Root,FooterSize,0);
	auto* Footer=WidgetTree->ConstructWidget<UVerticalBox>(); FooterSize->SetContent(Footer);
	UVerticalBox* Confirmation; ConfirmPanel=Card(Confirmation,false); AddLine(Footer,ConfirmPanel,8);
	ConfirmText=Label(TEXT(""),18); AddLine(Confirmation,ConfirmText);
	Row(Confirmation,{{TEXT("Remove"),TEXT("confirmremove")},{TEXT("Keep"),TEXT("cancelremove")}}); ConfirmPanel->SetVisibility(ESlateVisibility::Collapsed);
	auto* StatusBorder=WidgetTree->ConstructWidget<UBorder>(); StatusBorder->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.004f,.008f,.02f,.85f),3.f,FLinearColor(.45f,.36f,.2f,1),1.f));
	StatusBorder->SetPadding(FMargin(12,8));
	StatusText=Label(TEXT(""),16,false,1.f); StatusBorder->SetContent(StatusText); AddLine(Footer,StatusBorder,8);
	StatusBorder->SetVisibility(ESlateVisibility::Collapsed);
	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(); Root->AddChildToVerticalBox(Pages)->SetSize(Fill);

	// Play: the selected server's window (title bar, details, selected account, links) and the Launch ring.
	auto* Play = WidgetTree->ConstructWidget<UVerticalBox>(); Pages->AddChild(Play);
	Play->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(Fill);
	auto* Bottom=WidgetTree->ConstructWidget<UHorizontalBox>(); Play->AddChildToVerticalBox(Bottom);
	PlayWindowSize=WidgetTree->ConstructWidget<USizeBox>(); PlayWindowSize->SetWidthOverride(600); PlayWindowSize->SetHeightOverride(452);
	Bottom->AddChildToHorizontalBox(PlayWindowSize)->SetVerticalAlignment(VAlign_Bottom);
	PlayWindow=WidgetTree->ConstructWidget<UBorder>(); PlayWindow->SetPadding(FMargin(24,0,24,18));
	PlayWindow->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.01f,.03f,.09f,.92f),4.f,FLinearColor(.5f,.4f,.22f,1),1.f));
	PlayWindowSize->SetContent(PlayWindow);
	auto* Window=WidgetTree->ConstructWidget<UVerticalBox>(); PlayWindow->SetContent(Window);
	auto* TitleBar=WidgetTree->ConstructWidget<USizeBox>(); TitleBar->SetHeightOverride(FACELobbyArt::WindowHeader); Window->AddChildToVerticalBox(TitleBar);
	ServerTitle=Heading(TEXT("Choose your world"),30); ServerTitle->SetClipping(EWidgetClipping::ClipToBounds);
	ServerTitle->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); TitleBar->SetContent(ServerTitle);
	Cast<USizeBoxSlot>(ServerTitle->Slot)->SetVerticalAlignment(VAlign_Center); Cast<USizeBoxSlot>(ServerTitle->Slot)->SetPadding(FMargin(54,0,4,4));
	auto* Body=WidgetTree->ConstructWidget<UVerticalBox>(); auto* BodySlot=Window->AddChildToVerticalBox(Body); BodySlot->SetSize(Fill); BodySlot->SetPadding(FMargin(4,16,4,0));
	ServerDetail=Label(TEXT("Save a server or discover one in the directory."),15,false,1.f); ServerDetail->SetColorAndOpacity(ACELobbyArt::Gold); AddLine(Body,ServerDetail,8);
	ServerDescription=Label(TEXT(""),15,false,1.f); AddLine(Body,ServerDescription,14);
	AddLine(Body,SmallCaps(TEXT("SELECTED ACCOUNT"),false),5);
	SelectedAccountRow=WidgetTree->ConstructWidget<UBorder>(); SelectedAccountRow->SetPadding(FMargin(12,0));
	SelectedAccountRow->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.02f,.05f,.12f,.9f),3.f,FLinearColor(.55f,.42f,.2f,1),1.f));
	AddLine(Body,SelectedAccountRow,6);
	auto* SelectedHeight=WidgetTree->ConstructWidget<USizeBox>(); SelectedHeight->SetHeightOverride(40); SelectedAccountRow->SetContent(SelectedHeight);
	auto* SelectedLine=WidgetTree->ConstructWidget<UHorizontalBox>(); SelectedHeight->SetContent(SelectedLine);
	TWeakObjectPtr<UACELoginWidget> WeakThis(this);
	auto* SelectedStar=SelectedLine->AddChildToHorizontalBox(Glyph(SACELobbyGlyph::EKind::Star,17,
		TAttribute<FLinearColor>::CreateLambda([WeakThis](){ return WeakThis.IsValid()&&WeakThis->bSelectedAccountFavorite?ACELobbyArt::Gold:ACELobbyArt::Muted*FLinearColor(1,1,1,.55f); }),
		TAttribute<bool>::CreateLambda([WeakThis](){ return WeakThis.IsValid()&&WeakThis->bSelectedAccountFavorite; })));
	SelectedStar->SetVerticalAlignment(VAlign_Center); SelectedStar->SetPadding(FMargin(0,0,10,0));
	SelectedAccountName=Label(TEXT(""),18,false,1.f); SelectedAccountName->SetAutoWrapText(false); SelectedAccountName->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	SelectedAccountName->SetColorAndOpacity(FLinearColor(1.f,.95f,.82f,1));
	auto* SelectedNameSlot=SelectedLine->AddChildToHorizontalBox(SelectedAccountName); SelectedNameSlot->SetSize(Fill); SelectedNameSlot->SetVerticalAlignment(VAlign_Center);
	LastLaunchedText=Label(TEXT(""),14,true,1.f); AddLine(Body,LastLaunchedText,0);
	Body->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(Fill);
	auto* Links=WidgetTree->ConstructWidget<UWrapBox>(); Links->SetInnerSlotPadding(FVector2D(4,4)); Body->AddChildToVerticalBox(Links)->SetPadding(FMargin(0,10,0,0));
	WebsiteButton=ActionButton(TEXT("Website"),TEXT("website")); Links->AddChildToWrapBox(WebsiteButton);
	DiscordButton=ActionButton(TEXT("Discord"),TEXT("discord")); Links->AddChildToWrapBox(DiscordButton);
	Links->AddChildToWrapBox(ActionButton(TEXT("Edit server"),TEXT("editserver")));
	Links->AddChildToWrapBox(ActionButton(TEXT("Remove server"),TEXT("removeserver")));
	Bottom->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(Fill);
	LaunchSize=WidgetTree->ConstructWidget<USizeBox>(); LaunchSize->SetWidthOverride(290); LaunchSize->SetHeightOverride(290);
	Bottom->AddChildToHorizontalBox(LaunchSize)->SetVerticalAlignment(VAlign_Bottom);
	LoginButton=ActionButton(TEXT("Launch"),TEXT("launch"),FString(),true); LaunchSize->SetContent(LoginButton);
	LoginButton->SetToolTipText(FText::FromString(TEXT("Launch the selected account (Enter)")));
	if (auto* CaptionSize=Cast<USizeBox>(LoginButton->GetContent())) if (auto* Caption=Cast<UACERetailTextBlock>(CaptionSize->GetContent()))
	{
		Caption->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),24));
		Caption->SetFontOverride(ACEUIFontStyles::FontIdForStyleName(TEXT("fancyfont40")));
		Caption->SetRetailElement(UiResources(this),nullptr,FVector2D(1.5f),0,false);
		Caption->SetShadowOffset(FVector2D(1,2)); Caption->SetShadowColorAndOpacity(FLinearColor(0,0,0,.9f));
	}
	auto* BuildLabel=Label(FString::Printf(TEXT("Version %s"),ACEClientBuild::Version),13,true,1.f); BuildLabel->SetAutoWrapText(false);
	auto* BuildSlot=Canvas->AddChildToCanvas(BuildLabel); BuildSlot->SetAnchors(FAnchors(1,1)); BuildSlot->SetAlignment(FVector2D(1,1));
	BuildSlot->SetPosition(FVector2D(-30,-8)); BuildSlot->SetAutoSize(true);
	// Server editor: explicit save/cancel avoids sending credentials to an edited endpoint by accident.
	UVerticalBox* Edit; Pages->AddChild(Card(Edit,true));
	AddLine(Edit,Label(TEXT("Server details"),30),18);
	NameBox=Field(Edit,TEXT("Server name"),TEXT("ServerName"));
	auto* Address=WidgetTree->ConstructWidget<UHorizontalBox>(); AddLine(Edit,Address,0);
	auto* HostCol=WidgetTree->ConstructWidget<UVerticalBox>(); Address->AddChildToHorizontalBox(HostCol)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	HostBox=Field(HostCol,TEXT("Hostname or IP address"),TEXT("HostBox"));
	auto* PortWidth=WidgetTree->ConstructWidget<USizeBox>(); PortWidth->SetWidthOverride(140); Address->AddChildToHorizontalBox(PortWidth)->SetPadding(FMargin(16,0,0,0));
	auto* PortCol=WidgetTree->ConstructWidget<UVerticalBox>(); PortWidth->SetContent(PortCol); PortBox=Field(PortCol,TEXT("Port"),TEXT("PortBox"));
	AddLine(Edit,Label(TEXT("Server type"),16,true),5);
	auto* Types=WidgetTree->ConstructWidget<UHorizontalBox>(); AddLine(Edit,Types,12);
	ACETypeButton=ActionButton(TEXT("ACE"),TEXT("ace")); GDLETypeButton=ActionButton(TEXT("GDLE"),TEXT("gdle"));
	Types->AddChildToHorizontalBox(ACETypeButton)->SetPadding(FMargin(0,0,8,0)); Types->AddChildToHorizontalBox(GDLETypeButton);
	DescriptionBox=Field(Edit,TEXT("Description (optional)"),TEXT("ServerDescription"));
	WebsiteBox=Field(Edit,TEXT("Website (optional)"),TEXT("ServerWebsite")); DiscordBox=Field(Edit,TEXT("Discord invite (optional)"),TEXT("ServerDiscord"));
	Row(Edit,{{TEXT("Save server"),TEXT("saveserver")},{TEXT("Cancel"),TEXT("play")}});
	// Directory stays inside the same widget surface: no OS dropdowns in stereo.
	UVerticalBox* Browser; Pages->AddChild(Card(Browser,false));
	AddLine(Browser,Label(TEXT("Find your next world"),30),6);
	AddLine(Browser,Label(TEXT("Community server directory  /  ACE + GDLE"),18,true),18);
	SearchBox=Field(Browser,TEXT("Search names, descriptions or addresses"),TEXT("ServerSearch"));
	SearchBox->OnTextChanged.AddDynamic(this,&UACELoginWidget::OnSearchChanged);
	BrowserList=WidgetTree->ConstructWidget<UScrollBox>(); BrowserList->SetScrollbarThickness(FVector2D(10,10));
	auto* BrowserSlot=Browser->AddChildToVerticalBox(BrowserList); BrowserSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BrowserSlot->SetPadding(FMargin(0,0,0,14));
	BrowserDetail=Label(TEXT("Refresh to load the public server directory."),18); AddLine(Browser,BrowserDetail,16);
	Row(Browser,{{TEXT("Add to my servers"),TEXT("import")},{TEXT("Refresh"),TEXT("refresh")},{TEXT("Website"),TEXT("browserwebsite")},{TEXT("Discord"),TEXT("browserdiscord")}});
	AddLine(Browser,Label(TEXT("Directory maintained by acresources / Servers.xml. Adding a server does not connect to it."),14,true),0);
	UVerticalBox* Files; Pages->AddChild(Card(Files,true));
	AddLine(Files,Label(TEXT("Your game files"),30),12);
	FileProblem=Label(TEXT(""),20);
	FileProblem->SetColorAndOpacity(FLinearColor(1.f,.65f,.3f,1.f));
	FileProblem->SetVisibility(ESlateVisibility::Collapsed); AddLine(Files,FileProblem,16);
	AddLine(Files,Label(TEXT("Choose the folder containing your Asheron's Call .dat files. The client reads these files directly."),20),24);
	DatBox=Field(Files,TEXT("DAT folder"),TEXT("DatDirectory")); DatBox->OnTextChanged.AddDynamic(this,&UACELoginWidget::OnDatChanged);
#if PLATFORM_WINDOWS
	if (!UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled()) AddLine(Files,ActionButton(TEXT("Choose folder"),TEXT("browsefolder")));
	Row(Files,{{TEXT("Save location"),TEXT("savedat")},{TEXT("Use default"),TEXT("defaultdat")}});
#else
	Row(Files,{{TEXT("Save location"),TEXT("savedat")},{TEXT("Use app DAT folder"),TEXT("defaultdat")}});
	AddLine(Files,Label(PLATFORM_LINUX ? TEXT("Enter an absolute Linux folder path. DAT filenames are case sensitive.") : PLATFORM_MAC ? TEXT("Enter an absolute folder path, for example /Users/you/Games/Asheron's Call.") : TEXT("On Quest, the folder must be accessible to this app. The installer uses the app's DAT folder."),18,true),20);
#endif
	FileStatus=Label(TEXT(""),20); AddLine(Files,FileStatus,24);
	AddLine(Files,Label(TEXT("Changing an already-loaded installation takes effect after restarting. No game files are moved or copied."),18,true));
	UVerticalBox* Updates; Pages->AddChild(Card(Updates,true));
	AddLine(Updates,Label(TEXT("Client updates"),28));
	AddLine(Updates,Label(TEXT("New versions are checked automatically when the launcher opens. Updates are optional and keep your accounts, settings, and DAT files."),18,true));
	AutoUpdateCheckBox=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),TEXT("AutoUpdateCheckBox"));
	// Draw the indicator with Slate primitives and text so it stays legible in
	// packaged clients without relying on the editor's checkbox image resources.
	FCheckBoxStyle AutoUpdateStyle;
	AutoUpdateStyle.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
		.SetUncheckedImage(FSlateNoResource()).SetCheckedImage(FSlateNoResource())
		.SetUncheckedHoveredImage(FSlateRoundedBoxBrush(Tile,4.f)).SetCheckedHoveredImage(FSlateRoundedBoxBrush(Tile,4.f))
		.SetUncheckedPressedImage(FSlateRoundedBoxBrush(Ink,4.f)).SetCheckedPressedImage(FSlateRoundedBoxBrush(Ink,4.f));
	AutoUpdateStyle.SetForegroundColor(LobbyTextColor).SetHoveredForegroundColor(LobbyTextColor).SetPressedForegroundColor(LobbyTextColor)
		.SetCheckedForegroundColor(LobbyTextColor).SetCheckedHoveredForegroundColor(LobbyTextColor).SetCheckedPressedForegroundColor(LobbyTextColor);
	AutoUpdateStyle.SetPadding(FMargin(0,6)); AutoUpdateCheckBox->SetWidgetStyle(AutoUpdateStyle);
	auto* AutoUpdateRow=WidgetTree->ConstructWidget<UHorizontalBox>(); AutoUpdateCheckBox->SetContent(AutoUpdateRow);
	auto* IndicatorSize=WidgetTree->ConstructWidget<USizeBox>(); IndicatorSize->SetWidthOverride(28); IndicatorSize->SetHeightOverride(28);
	AutoUpdateRow->AddChildToHorizontalBox(IndicatorSize)->SetVerticalAlignment(VAlign_Center);
	auto* Indicator=WidgetTree->ConstructWidget<UBorder>(); Indicator->SetBrush(FSlateRoundedBoxBrush(Ink,3.f,Gold,2.f));
	Indicator->SetPadding(FMargin(0)); Indicator->SetVerticalAlignment(VAlign_Center); IndicatorSize->SetContent(Indicator);
	AutoUpdateMark=WidgetTree->ConstructWidget<UTextBlock>(); AutoUpdateMark->SetText(FText::FromString(TEXT("\u2713")));
	AutoUpdateMark->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),22));
	AutoUpdateMark->SetAutoWrapText(false); AutoUpdateMark->SetJustification(ETextJustify::Center);
	AutoUpdateMark->SetColorAndOpacity(LobbyTextColor); AutoUpdateMark->SetRenderOpacity(0); Indicator->SetContent(AutoUpdateMark);
	auto* AutoUpdateCaption=AutoUpdateRow->AddChildToHorizontalBox(Label(TEXT("Automatically download and install updates"),18));
	AutoUpdateCaption->SetPadding(FMargin(10,0,0,0)); AutoUpdateCaption->SetVerticalAlignment(VAlign_Center);
	AutoUpdateCaption->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AutoUpdateCheckBox->SetIsChecked(false);
	AutoUpdateCheckBox->OnCheckStateChanged.AddDynamic(this,&UACELoginWidget::OnAutoUpdateChanged);
	AddLine(Updates,AutoUpdateCheckBox,8);
	AddLine(Updates,Label(TEXT("Off by default. When enabled, checks every 15 minutes in the launcher and updates before you log in. Never interrupts gameplay. Cancel pauses automatic updates until the next app launch or Check now."),16,true));
	UpdateText=Label(TEXT("Checking for updates..."),20); AddLine(Updates,UpdateText,20);
	if (!VialArt) VialArt=MakeShared<FACEVialArt>();
	auto* UpdateVial=WidgetTree->ConstructWidget<UNativeWidgetHost>(); UpdateVial->SetVisibility(ESlateVisibility::HitTestInvisible);
	UpdateVial->SetContent(SNew(SACEManaVial).Art(VialArt).Fill_Lambda([WeakThis](){return WeakThis.IsValid()?WeakThis->UpdatePageFill:0.f;}));
	auto* ProgressSize=WidgetTree->ConstructWidget<USizeBox>(); ProgressSize->SetWidthOverride(VialW); ProgressSize->SetHeightOverride(VialH); ProgressSize->SetContent(UpdateVial);
	AddLine(Updates,ProgressSize,20); Cast<UVerticalBoxSlot>(ProgressSize->Slot)->SetHorizontalAlignment(HAlign_Left);
	auto* UpdateActions=WidgetTree->ConstructWidget<UWrapBox>(); UpdateActions->SetInnerSlotPadding(FVector2D(10,10)); AddLine(Updates,UpdateActions,20);
	CheckUpdateButton=ActionButton(TEXT("Check now"),TEXT("checkupdate")); UpdateActions->AddChildToWrapBox(CheckUpdateButton);
	DownloadUpdateButton=ActionButton(TEXT("Download update"),TEXT("downloadupdate"),FString(),true); UpdateActions->AddChildToWrapBox(DownloadUpdateButton);
	InstallUpdateButton=ActionButton(PLATFORM_ANDROID?TEXT("Install"):TEXT("Install and restart"),TEXT("installupdate"),FString(),true); UpdateActions->AddChildToWrapBox(InstallUpdateButton);
	CancelUpdateButton=ActionButton(TEXT("Cancel update"),TEXT("cancelupdate")); UpdateActions->AddChildToWrapBox(CancelUpdateButton);
	UpdateInstallHelp=Label(ACEUpdates::InstallationNotice(PLATFORM_ANDROID),18,true); AddLine(Updates,UpdateInstallHelp);
	Row(Updates,{{TEXT("Release notes"),TEXT("updatenotes")},{TEXT("Back to play"),TEXT("play")}});
	auto* PluginPanel=WidgetTree->ConstructWidget<UACEPluginPanel>();
	Pages->AddChild(Frame(PluginPanel,FMargin(18)));
	UVerticalBox* Accounts; Pages->AddChild(Card(Accounts,false));
	AddLine(Accounts,Label(TEXT("Manage accounts"),30));
	AddLine(Accounts,Label(TEXT("Drag the grip beside an account to reorder. Use New account to add another login, even with the same username."),18,true));
	ManagedAccountList=WidgetTree->ConstructWidget<UScrollBox>(); ManagedAccountList->SetScrollbarThickness(FVector2D(8,8));
	auto* ManagedSlot=Accounts->AddChildToVerticalBox(ManagedAccountList); ManagedSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); ManagedSlot->SetPadding(FMargin(0,0,0,12));
	AccountAliasBox=Field(Accounts,TEXT("Alias (optional)"),TEXT("AccountAliasBox"));
	auto* Credentials=WidgetTree->ConstructWidget<UHorizontalBox>(); AddLine(Accounts,Credentials,0);
	auto* UserColumn=WidgetTree->ConstructWidget<UVerticalBox>(); auto* UserSlot=Credentials->AddChildToHorizontalBox(UserColumn);
	UserSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); UserSlot->SetPadding(FMargin(0,0,16,0));
	auto* PasswordColumn=WidgetTree->ConstructWidget<UVerticalBox>(); Credentials->AddChildToHorizontalBox(PasswordColumn)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AccountBox=Field(UserColumn,TEXT("Username"),TEXT("AccountBox"));
	PasswordBox=Field(PasswordColumn,TEXT("Password"),TEXT("PasswordBox"),true);
	PasswordBox->RemoveFromParent();
	auto* PasswordRow=WidgetTree->ConstructWidget<UHorizontalBox>(); AddLine(PasswordColumn,PasswordRow);
	PasswordRow->AddChildToHorizontalBox(PasswordBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	PasswordVisibilityButton=ActionButton(TEXT(""),TEXT("togglepassword"));
	auto EyeStyle=PasswordVisibilityButton->GetStyle(); EyeStyle.SetNormalPadding(FMargin(8)); EyeStyle.SetPressedPadding(FMargin(8)); PasswordVisibilityButton->SetStyle(EyeStyle);
	auto* Eye=WidgetTree->ConstructWidget<UNativeWidgetHost>(); Eye->SetVisibility(ESlateVisibility::HitTestInvisible);
	Eye->SetContent(SNew(SPasswordVisibilityIcon).Revealed_Lambda([Box=TWeakObjectPtr<UEditableTextBox>(PasswordBox)](){return Box.IsValid()&&!Box->GetIsPassword();}));
	PasswordVisibilityButton->SetContent(Eye);
	auto* EyeWidth=WidgetTree->ConstructWidget<USizeBox>(); EyeWidth->SetWidthOverride(44); EyeWidth->SetContent(PasswordVisibilityButton);
	PasswordRow->AddChildToHorizontalBox(EyeWidth)->SetPadding(FMargin(8,0,0,0));
	SetPasswordVisible(false);
	Row(Accounts,{{TEXT("Save account"),TEXT("saveaccount")},{TEXT("New account"),TEXT("newaccount")},{TEXT("Remove account"),TEXT("removeaccount")}});
	AddLine(Accounts,Label(PLATFORM_LINUX ? TEXT("Passwords are kept for this session only. Enter them here after restarting.") : PLATFORM_MAC ? TEXT("Passwords are saved securely in your macOS Keychain.") : TEXT("Accounts are saved securely on this device."),14,true));
	Row(Accounts,{{TEXT("Back to play"),TEXT("play")}});
	// Retained for Blueprint/test compatibility; retail DAT owns character selection.
	EnterWorldButton=ActionButton(TEXT("Enter world"),TEXT("enter")); EnterWorldButton->SetVisibility(ESlateVisibility::Collapsed);
	CharacterListLabel=Label(TEXT("")); CharacterListBox=WidgetTree->ConstructWidget<UScrollBox>();
	// Retail connect screen (layout 0x21000000, authored at 800x600): logo at the top,
	// Connect and Update progress vials beneath, letterboxed in black over the lobby.
	ConnectScreen=WidgetTree->ConstructWidget<UBorder>(); ConnectScreen->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Black,0.f)); ConnectScreen->SetPadding(FMargin(0));
	auto* ConnectSlot=Canvas->AddChildToCanvas(ConnectScreen); ConnectSlot->SetAnchors(FAnchors(0,0,1,1)); ConnectSlot->SetOffsets(FMargin(0));
	ConnectScreen->SetHorizontalAlignment(HAlign_Fill); ConnectScreen->SetVerticalAlignment(VAlign_Fill);
	auto* ConnectFit=WidgetTree->ConstructWidget<UScaleBox>(); ConnectFit->SetStretch(EStretch::ScaleToFit); ConnectScreen->SetContent(ConnectFit);
	auto* ConnectFrame=WidgetTree->ConstructWidget<USizeBox>(); ConnectFrame->SetWidthOverride(800); ConnectFrame->SetHeightOverride(600); ConnectFit->SetContent(ConnectFrame);
	auto* ConnectCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(); ConnectFrame->SetContent(ConnectCanvas);
	auto PlaceConnect=[&](UWidget* W,float X,float Y,float Wd,float Ht){auto* S=ConnectCanvas->AddChildToCanvas(W); S->SetPosition(FVector2D(X,Y)); S->SetSize(FVector2D(Wd,Ht));};
	ConnectLogo=WidgetTree->ConstructWidget<UImage>(); ConnectLogo->SetVisibility(ESlateVisibility::HitTestInvisible); ConnectLogo->SetRenderOpacity(0);
	PlaceConnect(ConnectLogo,0,30,800,482);
	for (int32 I=0;I<2;++I)
	{
		auto* Host=WidgetTree->ConstructWidget<UNativeWidgetHost>(); Host->SetVisibility(ESlateVisibility::HitTestInvisible);
		Host->SetContent(SNew(SACEManaVial).Art(VialArt).Fill_Lambda([WeakThis,I](){return WeakThis.IsValid()?(I?WeakThis->UpdateVialFill:WeakThis->ConnectFill):0.f;}));
		PlaceConnect(Host,I?420.f:40.f,511,VialW,VialH);
	}
	auto MakeConnectText=[&](float X,float Y,float Wd,float Ht,int32 Size)
	{
		auto* T=Label(TEXT(""),Size); T->SetJustification(ETextJustify::Center); T->SetColorAndOpacity(FLinearColor::White); PlaceConnect(T,X,Y,Wd,Ht); return T;
	};
	ConnectLabel=MakeConnectText(28,562,354,30,14); ConnectLabel->SetAutoWrapText(false);
	ConnectUpdateLabel=MakeConnectText(408,562,354,30,14); ConnectUpdateLabel->SetAutoWrapText(false);
	ConnectDetail=MakeConnectText(60,452,680,56,16);
	ConnectCancelButton=ActionButton(TEXT("Cancel"),TEXT("cancelconnect")); PlaceConnect(ConnectCancelButton,680,24,104,42);
	ConnectScreen->SetVisibility(ESlateVisibility::Collapsed);
}

void UACELoginWidget::NativeConstruct()
{
	EnsureDefaultLayout(); Super::NativeConstruct();
	if (auto* GI=GetGameInstance())
	{
		Updater=GI->GetSubsystem<UACEUpdateSubsystem>();
		Client=GI->GetSubsystem<UACEClientSubsystem>();
		if (Client)
		{
			Client->OnSessionStateChanged.AddUniqueDynamic(this,&UACELoginWidget::HandleState);
			Client->OnCharacterList.AddUniqueDynamic(this,&UACELoginWidget::HandleCharacters);
			Client->OnLogMessage.AddUniqueDynamic(this,&UACELoginWidget::HandleLog);
		}
	}
	if (!bLoginSettingsLoaded)
	{
		Profile.Host=DefaultHost; Profile.Port=FString::FromInt(DefaultPort); Profile.Account=DefaultAccount; Profile.Password=DefaultPassword;
		ACELoginSettings::Load(Profile, ProfilePath.IsEmpty()?ACELoginSettings::GetPath():ProfilePath); Profile.MigrateLegacy(); bLoginSettingsLoaded=true;
		SelectServer(Profile.SelectedServerId);
		if (Profile.DatDirectory.IsEmpty())
			if (auto* GI=GetGameInstance()) if (auto* Dat=GI->GetSubsystem<UACEDatSubsystem>()) Profile.DatDirectory=Dat->GetDatDirectory();
		DatBox->SetText(FText::FromString(Profile.DatDirectory));
		FString Cached, Error;
		if (IFileManager::Get().FileSize(*ACELoginProfile::DirectoryCachePath()) <= 2*1024*1024
			&& FFileHelper::LoadFileToString(Cached,*ACELoginProfile::DirectoryCachePath())) ACELoginProfile::ParseDirectory(Cached,Directory,Error);
		SetStatus(FString());
		ValidateGameFiles();
	}
#if PLATFORM_MAC
	Profile.bAutoUpdate=false;
#endif
	AutoUpdateCheckBox->SetIsChecked(Profile.bAutoUpdate);
	if (Updater) { Updater->SetAutoUpdateEnabled(Profile.bAutoUpdate); Updater->Check(true); }
	RefreshUpdateControls();
#if !UE_BUILD_SHIPPING
	// Lets the connect screen be reviewed and captured without any account or server.
	if (FParse::Param(FCommandLine::Get(),TEXT("ACEConnectScreenPreview"))) { bConnectScreenPreview=true; ShowConnectScreen(); }
#endif
	ForceLayoutPrepass();
}

void UACELoginWidget::NativeDestruct()
{
	SaveLoginEntries();
	if (DirectoryRequest) { DirectoryRequest->OnProcessRequestComplete().Unbind(); DirectoryRequest->CancelRequest(); DirectoryRequest.Reset(); bRefreshingDirectory=false; }
	if (Client)
	{
		Client->OnSessionStateChanged.RemoveDynamic(this,&UACELoginWidget::HandleState);
		Client->OnCharacterList.RemoveDynamic(this,&UACELoginWidget::HandleCharacters);
		Client->OnLogMessage.RemoveDynamic(this,&UACELoginWidget::HandleLog);
	}
	Super::NativeDestruct();
}

void UACELoginWidget::NativeTick(const FGeometry& Geometry, float Dt)
{
	Super::NativeTick(Geometry,Dt);
	if (bAccountOrderDirty) RefreshAccounts();
	TickAccountDrag(Dt);
	UpdatePollTime-=Dt;
	if (UpdatePollTime<=0)
	{
		UpdatePollTime=.25f;
		UACEDatSubsystem* Dat=nullptr;
		if (auto* GI=GetGameInstance()) Dat=GI->GetSubsystem<UACEDatSubsystem>();
		// The launcher can open before the asynchronous DAT load completes.
		// Install the retail fonts and art once ready without rebuilding the form.
		if(!bRetailFontsReady && Dat && Dat->IsDatReady())
		{
			WidgetTree->ForEachWidget([Dat](UWidget* Widget)
			{
				if(auto* Text=Cast<UACERetailTextBlock>(Widget))
					Text->SetRetailElement(Dat->GetUiResources(),nullptr,Text->GetBitmapScale(),0,false);
			});
			bRetailFontsReady=true;
			for (auto* List:{AccountList.Get(),FavoriteList.Get()}) if (List) List->RefreshRows();
		}
		if (!bLobbyArtApplied && Dat && Dat->IsDatReady() && ACELobbyArt::Load(Dat)) ApplyLobbyArt();
		LoadVialArt();
		if (Updater)
		{
			Updater->PollInstall();
			if (Updater->IsAutoUpdateEnabled()) SaveLoginEntries(); // Persist edits before an automatic restart.
			const auto Action=Updater->PollAutoUpdate(UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled());
			if (Action==EACEAutoUpdateAction::Download || Action==EACEAutoUpdateAction::Install
				|| Updater->State==EACEUpdateState::InstallNotice) Pages->SetActiveWidgetIndex(4);
		}
		RefreshUpdateControls();
	}
	TickConnectScreen(Dt);
#if !UE_BUILD_SHIPPING
	// -ACELobbyShot: once the DAT art is in, capture the lobby (with UI), a search, and another tab, then quit.
	if (bLobbyArtApplied && FParse::Param(FCommandLine::Get(),TEXT("ACELobbyShot")))
	{
		if (LobbyShotTime<0) LobbyShotAccountId=Profile.SelectedAccountId; // Put the user's selection back afterwards.
		const float Previous=LobbyShotTime<0?0.f:LobbyShotTime; LobbyShotTime=Previous+Dt;
		auto At=[&](float T){ return Previous<T && LobbyShotTime>=T; };
		auto Shot=[](const TCHAR* Name){ FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/Name,true,false); };
		if (At(3.f)) Shot(TEXT("Lobby_Play.png"));
		if (At(4.f) && AccountSearchBox) { AccountSearchBox->SetText(FText::FromString(TEXT("a"))); OnAccountSearchChanged(AccountSearchBox->GetText()); }
		if (At(5.f)) Shot(TEXT("Lobby_Search.png"));
		if (At(6.f)) { RunAction(TEXT("clearsearch"),FString()); RunAction(TEXT("accounts"),FString()); }
		if (At(7.f)) Shot(TEXT("Lobby_Accounts.png"));
		if (At(8.f)) RunAction(TEXT("files"),FString());
		if (At(9.f)) Shot(TEXT("Lobby_Files.png"));
		if (At(10.f)) { RunAction(TEXT("play"),FString()); RunAction(TEXT("selectaccount"),LobbyShotAccountId); }
		if (At(11.f)) FPlatformMisc::RequestExit(false);
	}
#endif
	UpdatePageFill=FMath::FInterpTo(UpdatePageFill,UpdatePageTarget,Dt,6.f);
	if(StyledPage!=Pages->GetActiveWidgetIndex())
	{
		StyledPage=Pages->GetActiveWidgetIndex();
		const int32 PageForTab[]={0,6,2,3,4,5};
		for(int32 I=0;I<NavigationButtons.Num();++I) ButtonStyle(NavigationButtons[I],PageForTab[I]==StyledPage);
	}
	const FVector2D View=Geometry.GetLocalSize();
	if(View.X<=0 || View.Y<=0) return;
	LayoutLobby(View);
}

void UACELoginWidget::LayoutLobby(const FVector2D& View)
{
	// One scale for the whole lobby, fitted to a 1440x900 design so nothing leaves small windows.
	const float Scale=FMath::Clamp(float(FMath::Min(View.X/DesignW,View.Y/DesignH)),.1f,2.5f);
	if (ResponsiveScale && !FMath::IsNearlyEqual(ResponsiveScale->GetUserSpecifiedScale(),Scale)) ResponsiveScale->SetUserSpecifiedScale(Scale);
	const FVector2D L=View/Scale;
	if (!L.Equals(LayoutView,.5f))
	{
		LayoutView=L;
		// The 800x600 backdrop covers the window; extra height is cropped from the top
		// (only sky), extra width from both sides, so the logo always stays in view. Past
		// the crop limit (ultra-wide) it keeps to the right edge, like character select.
		float S=FMath::Max(float(L.X/800.),FMath::Min(float(L.Y/600.),float(L.X/800.*1.25)));
		S=FMath::Min(S,float(1.5*L.Y/600.));
		LayoutBgScale=S;
		const float BgX=FMath::Max(float(L.X-800.f*S)*.5f,float(L.X-800.f*S)), BgY=-FMath::Min(50.f,FMath::Max(0.f,float(600.f*S-L.Y)));
		auto SlotOf=[](UWidget* W){ return W?Cast<UCanvasPanelSlot>(W->Slot):nullptr; };
		if (auto* Bg=SlotOf(LobbyBackground)) { Bg->SetPosition(FVector2D(BgX,BgY)); Bg->SetSize(FVector2D(800.f*S,600.f*S)); }
		// Ultra-wide windows: a mirrored copy continues the art seamlessly to the left edge.
		if (auto* Side=SlotOf(LobbyBackgroundSide))
		{
			Side->SetPosition(FVector2D(BgX-800.f*S,BgY)); Side->SetSize(FVector2D(800.f*S,600.f*S));
			LobbyBackgroundSide->SetVisibility(BgX>0.f?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
		}
		if (auto* Sub=SlotOf(LobbySubtitle))
		{
			Sub->SetPosition(FVector2D(BgX+607.f*S,BgY+137.f*S)); Sub->SetAutoSize(true);
			FSlateFontInfo Font=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),FMath::Max(10,FMath::RoundToInt(15.f*S)));
			Font.LetterSpacing=550; LobbySubtitle->SetFont(Font);
		}
		if (auto* Brand=SlotOf(LobbyBrand)) { Brand->SetPosition(FVector2D(BgX+607.f*S,BgY+78.f*S)); Brand->SetAutoSize(true); }
		const float TabsY=FMath::Max(24.f,BgY+173.f*S);
		const float RX=FMath::Max(500.f,BgX+278.f*S);
		const float RW=FMath::Max(300.f,float(L.X)-RX-36.f);
		if (auto* Side=SlotOf(SidebarSize)) { Side->SetPosition(FVector2D(48,24)); Side->SetSize(FVector2D(380,FMath::Max(300.f,float(L.Y)-40.f))); }
		if (auto* Dash=SlotOf(DashboardSize)) { Dash->SetPosition(FVector2D(RX,TabsY)); Dash->SetSize(FVector2D(RW,FMath::Max(200.f,float(L.Y)-TabsY-28.f))); }
		if (PlayWindowSize) PlayWindowSize->SetWidthOverride(FMath::Clamp(RW-314.f,520.f,820.f));
	}
	// The window takes the height left under the tabs and any status, so messages never push it off screen.
	if (PlayWindowSize && DashboardSize && FooterSize)
		if (auto* Dash=Cast<UCanvasPanelSlot>(DashboardSize->Slot))
		{
			float Used=FooterSize->GetDesiredSize().Y;
			if (auto* Root=Cast<UPanelWidget>(DashboardSize->GetContent())) if (UWidget* Tabs=Root->GetChildAt(0)) Used+=Tabs->GetDesiredSize().Y+8.f;
			const float Avail=float(Dash->GetSize().Y)-Used-4.f;
			const float H=FMath::Clamp(Avail,300.f,452.f);
			if (!FMath::IsNearlyEqual(PlayWindowSize->GetHeightOverride(),H,.5f)) PlayWindowSize->SetHeightOverride(H);
			const float Ring=FMath::Clamp(Avail,180.f,290.f);
			if (LaunchSize && !FMath::IsNearlyEqual(LaunchSize->GetHeightOverride(),Ring,.5f)) { LaunchSize->SetWidthOverride(Ring); LaunchSize->SetHeightOverride(Ring); }
			if (ServerDescription)
			{
				const ESlateVisibility Want=!ServerDescription->GetText().IsEmpty() && H>=400.f?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed;
				if (ServerDescription->GetVisibility()!=Want) ServerDescription->SetVisibility(Want);
			}
		}
	// Retail arrows sit at the ends of the list's own scrollbar and appear with it.
	if (AccountList && AccountScrollUp && AccountScrollDown)
	{
		const ESlateVisibility Want=AccountList->CanScroll()?ESlateVisibility::Visible:ESlateVisibility::Collapsed;
		if (AccountScrollUp->GetVisibility()!=Want) { AccountScrollUp->SetVisibility(Want); AccountScrollDown->SetVisibility(Want); }
	}
}

void UACELoginWidget::ApplyLobbyArt()
{
	const FACELobbyArt* Art=ACELobbyArt::Get();
	if (!Art) return;
	bLobbyArtApplied=true;
	if (LobbyBackground) { LobbyBackground->SetBrush(Art->Background); LobbyBackground->SetRenderOpacity(1); }
	if (LobbyBackgroundSide)
	{
		FSlateBrush Mirrored=Art->Background; Mirrored.Mirroring=ESlateBrushMirrorType::Horizontal;
		LobbyBackgroundSide->SetBrush(Mirrored); LobbyBackgroundSide->SetRenderOpacity(1);
	}
	if (LobbyBrand) LobbyBrand->SetVisibility(ESlateVisibility::Collapsed); // The backdrop carries the retail logo.
	if (PlayWindow) PlayWindow->SetBrush(Art->Window);
	if (SelectedAccountRow) SelectedAccountRow->SetBrush(Art->Row[2]);
	if (AccountSearchBox)
	{
		FEditableTextBoxStyle Style=AccountSearchBox->GetWidgetStyle();
		Style.SetBackgroundImageNormal(Art->Search[0]).SetBackgroundImageHovered(Art->Search[0]).SetBackgroundImageFocused(Art->Search[1]).SetBackgroundImageReadOnly(Art->Search[0]);
		AccountSearchBox->SetWidgetStyle(Style);
		// UEditableTextBox::SetWidgetStyle hands Slate a pointer to its argument; re-point it at the
		// widget's own copy so the live text box never reads the (now gone) local above.
		AccountSearchBox->SetWidgetStyle(AccountSearchBox->GetWidgetStyle());
	}
	WidgetTree->ForEachWidget([](UWidget* Widget){ if (auto* B=Cast<UACELoginActionButton>(Widget)) ButtonStyle(B,B->bLobbyAccent); });
	for (auto* List:{AccountList.Get(),FavoriteList.Get()}) if (List) List->ApplyArt();
	for (const auto& Frame:LobbyFrames) if (const TSharedPtr<SWidget> Pinned=Frame.Pin()) Pinned->Invalidate(EInvalidateWidgetReason::Paint);
	StyledPage=INDEX_NONE;
}

bool UACELoginWidget::IsLobbyKeyboardActive() const
{
	return Pages && Pages->GetActiveWidgetIndex()==0 && PendingRemoval.IsEmpty()
		&& !IsConnectScreenVisible();
}

FReply UACELoginWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsLobbyKeyboardActive() && AccountSearchBox)
	{
		const FKey Key=InKeyEvent.GetKey();
		const FString Search=AccountSearchBox->GetText().ToString();
		if (Key==EKeys::Escape && !Search.IsEmpty()) { RunAction(TEXT("clearsearch")); return FReply::Handled(); }
		if (Key==EKeys::BackSpace && !Search.IsEmpty() && !AccountSearchBox->HasKeyboardFocus())
		{
			AccountSearchBox->SetText(FText::FromString(Search.LeftChop(1))); OnAccountSearchChanged(AccountSearchBox->GetText());
			return FReply::Handled();
		}
		if (Key==EKeys::Up || Key==EKeys::Down) { MoveAccountSelection(Key==EKeys::Up?-1:1); return FReply::Handled(); }
		if (Key==EKeys::Enter && !InKeyEvent.IsRepeat()) { RunAction(TEXT("launch")); return FReply::Handled(); }
	}
	return Super::NativeOnKeyDown(InGeometry,InKeyEvent);
}

FReply UACELoginWidget::NativeOnKeyChar(const FGeometry& InGeometry, const FCharacterEvent& InCharEvent)
{
	const TCHAR C=InCharEvent.GetCharacter();
	if (IsLobbyKeyboardActive() && AccountSearchBox && !AccountSearchBox->HasKeyboardFocus() && C>=32 && C!=127
		&& !InCharEvent.IsControlDown() && !InCharEvent.IsCommandDown())
	{
		// Type-to-jump: printable keys go straight into the account search.
		const FString Search=AccountSearchBox->GetText().ToString();
		if (!(C==TEXT(' ') && Search.IsEmpty()) && Search.Len()<64)
		{
			AccountSearchBox->SetText(FText::FromString(Search+C)); OnAccountSearchChanged(AccountSearchBox->GetText());
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyChar(InGeometry,InCharEvent);
}

void UACELoginWidget::OnAccountSearchChanged(const FText&)
{
	if (AccountList) AccountList->SetScrollOffset(0);
	RefreshAccountLists();
	if (!VisibleAccountOrder.IsEmpty() && !VisibleAccountOrder.Contains(Profile.SelectedAccountId))
		RunAction(TEXT("selectaccount"),VisibleAccountOrder[0]);
}

void UACELoginWidget::OnAccountSearchCommitted(const FText&, ETextCommit::Type CommitMethod)
{
	if (CommitMethod==ETextCommit::OnEnter && IsLobbyKeyboardActive()) RunAction(TEXT("launch"));
}

void UACELoginWidget::OnAutoUpdateChanged(bool bChecked)
{
	Profile.bAutoUpdate=bChecked;
	bLoginEntriesDirty=true;
	SaveLoginEntries();
	if (Updater) Updater->SetAutoUpdateEnabled(bChecked);
	RefreshUpdateControls();
}

void UACELoginWidget::RefreshUpdateControls()
{
	if (AutoUpdateMark) AutoUpdateMark->SetRenderOpacity(Profile.bAutoUpdate?1.f:0.f);
	if (!Updater || !UpdateText) return;
	const auto State=Updater->State;
	const bool Busy=Updater->IsBusy();
	const bool Available=Updater->Release.Number>ACEClientBuild::ReleaseNumber;
	FString Text=Updater->Message;
	if (State==EACEUpdateState::Downloading) Text+=FString::Printf(TEXT(" %.0f%%"),Updater->Progress()*100);
	if (UpdateText->GetText().ToString()!=Text) UpdateText->SetText(FText::FromString(Text));
	UpdateText->SetColorAndOpacity(LobbyTextColor);
	UpdateInstallHelp->SetVisibility(State==EACEUpdateState::Ready || State==EACEUpdateState::InstallNotice || State==EACEUpdateState::Installing?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
	UpdatePageTarget=State==EACEUpdateState::Ready || State==EACEUpdateState::InstallNotice || State==EACEUpdateState::Installing?1.f:Updater->Progress();
	CheckUpdateButton->SetIsEnabled(!Busy && State!=EACEUpdateState::Checking);
	DownloadUpdateButton->SetVisibility(Available && !Busy && State!=EACEUpdateState::Ready?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
	InstallUpdateButton->SetVisibility(!PLATFORM_MAC && State==EACEUpdateState::Ready?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
	CancelUpdateButton->SetVisibility(State==EACEUpdateState::Downloading || State==EACEUpdateState::Verifying || State==EACEUpdateState::InstallNotice?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
	AutoUpdateCheckBox->SetIsEnabled(State!=EACEUpdateState::Installing);
	LoginButton->SetIsEnabled(!Busy && Profile.SelectedServer()!=nullptr);
	if (auto* Size=Cast<USizeBox>(UpdateNavButton->GetContent())) if(auto* Label=Cast<UTextBlock>(Size->GetContent()))
	{ const FString Title=Available?TEXT("Update available"):TEXT("Updates"); if(Label->GetText().ToString()!=Title)Label->SetText(FText::FromString(Title)); }
}

void UACELoginWidget::OnLoginEntryChanged(const FText&) { bLoginEntriesDirty=true; }
void UACELoginWidget::OnLoginEntryCommitted(const FText&, ETextCommit::Type) { SaveLoginEntries(); }
void UACELoginWidget::SaveLoginEntries()
{
	if (!bLoginSettingsLoaded || !bLoginEntriesDirty) return;
	if (ACELoginSettings::Save(Profile, ProfilePath.IsEmpty()?ACELoginSettings::GetPath():ProfilePath)) bLoginEntriesDirty=false;
	else SetStatus(PLATFORM_MAC ? TEXT("Could not save settings. Check available storage and allow Keychain access.") : TEXT("Could not save settings on this device. Check available storage."));
}
void UACELoginWidget::OnLoginClicked() { DoLogin(); }
void UACELoginWidget::OnEnterWorldClicked() { DoEnterSelectedCharacter(); }

void UACELoginWidget::RefreshServers()
{
	ServerList->ClearChildren();
	if (Profile.Servers.IsEmpty()) ServerList->AddChild(Label(TEXT("No saved servers yet. Browse the directory or add your own."),15,true,1.f));
	for (const auto& S:Profile.Servers)
	{
		const bool bSelected=S.Id==Profile.SelectedServerId;
		auto* B=ActionButton(S.Name,TEXT("selectserver"),S.Id,bSelected);
		B->SetToolTipText(FText::FromString(S.Name));
		// Name, then type and address: enough to tell similar servers apart.
		auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();
		auto* Name=Label(S.Name,16,false,1.f); Name->SetAutoWrapText(false); Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		// The world name reads like character select's World box (sansfont20, 0xFFD8AF5B).
		if (auto* Retail=Cast<UACERetailTextBlock>(Name))
		{
			Retail->SetFontOverride(ACEUIFontStyles::FontIdForStyleName(TEXT("sansfont20")));
			Retail->SetRetailElement(UiResources(this),nullptr,Retail->GetBitmapScale(),0,false);
		}
		Name->SetColorAndOpacity(bSelected?ACELobbyArt::Highlight:ACELobbyArt::WorldText);
		auto* Detail=Label(FString::Printf(TEXT("%s  /  %s:%d"),S.bGDLE?TEXT("GDLE"):TEXT("ACE"),*S.Host,S.Port),13,true,1.f);
		Detail->SetAutoWrapText(false); Detail->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Lines->AddChildToVerticalBox(Name); Lines->AddChildToVerticalBox(Detail)->SetPadding(FMargin(0,2,0,0));
		auto* Height=WidgetTree->ConstructWidget<USizeBox>(); Height->SetHeightOverride(ServerRowHeight-12.f); Height->SetContent(Lines);
		Cast<USizeBoxSlot>(Lines->Slot)->SetVerticalAlignment(VAlign_Center);
		B->SetContent(Height);
		Cast<UScrollBoxSlot>(ServerList->AddChild(B))->SetPadding(FMargin(0,0,0,4));
	}
	if (ServerListSize) ServerListSize->SetHeightOverride(Profile.Servers.IsEmpty()?48.f:FMath::Min(Profile.Servers.Num(),3)*(ServerRowHeight+4.f)-4.f);
}

void UACELoginWidget::SelectServer(const FString& Id)
{
	SetPasswordVisible(false);
	const FString AccountId = Profile.SelectedAccountId;
	Profile.SelectedServerId=Id; Profile.SelectedAccountId=AccountId;
	const auto* S=Profile.SelectedServer();
	HostBox->SetText(FText::FromString(S?S->Host:TEXT(""))); PortBox->SetText(FText::FromString(FString::FromInt(S?S->Port:9000)));
	ServerTitle->SetText(FText::FromString(S?S->Name:TEXT("Choose your world")));
	ServerDetail->SetText(FText::FromString(S?FString::Printf(TEXT("%s  /  %s:%d"),S->bGDLE?TEXT("GDLE"):TEXT("ACE"),*S->Host,S->Port):TEXT("Add a server to get started.")));
	ServerDescription->SetText(FText::FromString(S?S->Description.Left(240):TEXT("")));
	ServerDescription->SetVisibility(S && !S->Description.IsEmpty()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	WebsiteButton->SetIsEnabled(S && ACELoginProfile::IsWebLink(S->Website)); DiscordButton->SetIsEnabled(S && ACELoginProfile::IsWebLink(S->Discord));
	LoginButton->SetIsEnabled(S!=nullptr);
	const auto* A=Profile.Accounts.FindByPredicate([&](const auto& E){return E.Id==AccountId;});
	if (!A && !Profile.Accounts.IsEmpty()) A=&Profile.Accounts[0];
	Profile.SelectedAccountId=A?A->Id:FString();
	AccountBox->SetText(FText::FromString(A?A->Username:TEXT(""))); PasswordBox->SetText(FText::FromString(A?A->Password:TEXT("")));
	AccountAliasBox->SetText(FText::FromString(A?A->Alias:TEXT("")));
	Profile.Host=S?S->Host:FString(); Profile.Port=S?FString::FromInt(S->Port):TEXT("9000");
	Profile.Account=A?A->Username:FString(); Profile.Password=A?A->Password:FString();
	RefreshServers(); RefreshAccounts(); bLoginEntriesDirty=true;
}

void UACELoginWidget::RefreshAccounts()
{
	bAccountOrderDirty=false;
	if (ManagedAccountList)
	{
		UScrollBox* List=ManagedAccountList;
		const float ScrollOffset=List->GetScrollOffset();
		List->ClearChildren();
		for (const auto& A:Profile.Accounts)
		{
			auto* Button=ActionButton(A.DisplayName(),TEXT("selectaccount"),A.Id,A.Id==Profile.SelectedAccountId);
			Button->bAccountDragHandle=true;
			Button->SetToolTipText(FText::FromString(A.DisplayName()));
			Cast<UScrollBoxSlot>(List->AddChild(Button))->SetPadding(FMargin(0,0,0,6));
		}
		if (!List->GetChildrenCount()) List->AddChild(Label(TEXT("Add a login on the Accounts tab."),18,true));
		List->SetScrollOffset(ScrollOffset);
	}
	RefreshAccountLists();
}

void UACELoginWidget::RefreshAccountLists()
{
	if (!AccountList || !FavoriteList) return;
	const FString Search=AccountSearchBox?AccountSearchBox->GetText().ToString().TrimStartAndEnd():FString();
	const FString Sort=Profile.AccountSort;
	TArray<const FACELoginAccount*> Favorites, Others;
	for (const auto& A:Profile.Accounts)
		if (Search.IsEmpty() || A.DisplayName().Contains(Search,ESearchCase::IgnoreCase)) (A.bFavorite?Favorites:Others).Add(&A);
	// Custom keeps the order from the Accounts tab; the others are stable so ties keep it too.
	auto ByName=[](const FACELoginAccount* X, const FACELoginAccount* Y){ return X->DisplayName().Compare(Y->DisplayName(),ESearchCase::IgnoreCase)<0; };
	auto ByRecent=[ByName](const FACELoginAccount* X, const FACELoginAccount* Y){ return X->LastUsed!=Y->LastUsed?X->LastUsed>Y->LastUsed:ByName(X,Y); };
	for (auto* Group:{&Favorites,&Others})
	{
		if (Sort==TEXT("recent")) Algo::StableSort(*Group,ByRecent);
		else if (Sort!=TEXT("custom")) Algo::StableSort(*Group,ByName);
	}
	TMap<FString,TObjectPtr<UACELobbyAccountItem>> Previous=MoveTemp(AccountItems);
	AccountItems.Reset();
	VisibleAccountOrder.Reset();
	auto Items=[&](const TArray<const FACELoginAccount*>& Accounts)
	{
		TArray<UObject*> Result;
		for (const FACELoginAccount* A:Accounts)
		{
			UACELobbyAccountItem* Item=nullptr;
			if (const TObjectPtr<UACELobbyAccountItem>* Found=Previous.Find(A->Id)) Item=*Found;
			if (!Item) Item=NewObject<UACELobbyAccountItem>(this);
			Item->Id=A->Id; Item->Title=A->DisplayName(); Item->bFavorite=A->bFavorite; Item->bSelected=A->Id==Profile.SelectedAccountId;
			Item->MatchStart=Search.IsEmpty()?INDEX_NONE:Item->Title.Find(Search,ESearchCase::IgnoreCase);
			Item->MatchLen=Item->MatchStart==INDEX_NONE?0:Search.Len();
			Item->Owner=this;
			AccountItems.Add(A->Id,Item); VisibleAccountOrder.Add(A->Id); Result.Add(Item);
		}
		return Result;
	};
	const TArray<UObject*> FavoriteItems=Items(Favorites), OtherItems=Items(Others);
	auto SameItems=[](const TArray<UObject*>& X, const TArray<UObject*>& Y){ return X==Y; };
	if (!SameItems(FavoriteList->GetListItems(),FavoriteItems)) FavoriteList->SetListItems(FavoriteItems);
	if (!SameItems(AccountList->GetListItems(),OtherItems)) AccountList->SetListItems(OtherItems);
	FavoriteList->RefreshRows(); AccountList->RefreshRows();
	const float RowPitch=UACELobbyAccountRow::RowHeight+3.f;
	if (FavoriteSize) FavoriteSize->SetHeightOverride(FMath::Max(1,FMath::Min(FavoriteItems.Num(),6))*RowPitch);
	const ESlateVisibility FavoriteVisibility=FavoriteItems.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible;
	if (FavoriteHeader) FavoriteHeader->SetVisibility(FavoriteVisibility);
	if (FavoriteSize) FavoriteSize->SetVisibility(FavoriteVisibility);
	const int32 Total=Profile.Accounts.Num(), Shown=VisibleAccountOrder.Num();
	if (AccountsHeading) AccountsHeading->SetText(FText::FromString(Search.IsEmpty()||Shown==Total?FString::Printf(TEXT("Accounts (%d)"),Total):FString::Printf(TEXT("Accounts (%d of %d)"),Shown,Total)));
	if (AccountsEmpty)
	{
		const FString Empty=Total==0?TEXT("Add a login on the Accounts tab."):OtherItems.IsEmpty()&&Shown==0?FString::Printf(TEXT("No accounts match \"%s\". Press Esc to clear."),*Search):OtherItems.IsEmpty()?TEXT("All matching accounts are favorites."):FString();
		AccountsEmpty->SetText(FText::FromString(Empty));
		AccountsEmpty->SetVisibility(Empty.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
	}
	if (AccountSearchClear) AccountSearchClear->SetVisibility(Search.IsEmpty()&&(!AccountSearchBox||AccountSearchBox->GetText().IsEmpty())?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
	for (UACELoginActionButton* B:SortButtons) if (B) ButtonStyle(B,B->Action.Mid(4)==(Sort.IsEmpty()?FString(TEXT("az")):Sort));
	UpdateSelectedAccountInfo();
}

void UACELoginWidget::UpdateSelectedAccountInfo()
{
	const auto* A=Profile.Accounts.FindByPredicate([&](const FACELoginAccount& E){return E.Id==Profile.SelectedAccountId;});
	bSelectedAccountFavorite=A && A->bFavorite;
	if (SelectedAccountName) SelectedAccountName->SetText(FText::FromString(A?A->DisplayName():TEXT("No account selected")));
	if (!LastLaunchedText) return;
	FString When;
	if (!A) When=Profile.Accounts.IsEmpty()?TEXT("Add a login on the Accounts tab."):TEXT("Pick an account from the list.");
	else if (A->LastUsed<=0) When=TEXT("Not launched yet on this device");
	else
	{
		// Stored as UTC seconds; shown in the player's local time.
		const FDateTime Local=FDateTime::FromUnixTimestamp(A->LastUsed)+(FDateTime::Now()-FDateTime::UtcNow());
		const int32 Days=(FDateTime::Now().GetDate()-Local.GetDate()).GetDays();
		if (Days==0) When=FString::Printf(TEXT("Last launched today at %s"),*TimeOfDay(Local));
		else if (Days==1) When=FString::Printf(TEXT("Last launched yesterday at %s"),*TimeOfDay(Local));
		else When=FString::Printf(TEXT("Last launched %s"),*Local.ToString(TEXT("%b %e, %Y")));
	}
	LastLaunchedText->SetText(FText::FromString(When));
}

void UACELoginWidget::MoveAccountSelection(int32 Delta)
{
	if (VisibleAccountOrder.IsEmpty()) return;
	int32 Index=VisibleAccountOrder.IndexOfByKey(Profile.SelectedAccountId);
	Index=Index==INDEX_NONE?(Delta>0?0:VisibleAccountOrder.Num()-1):FMath::Clamp(Index+Delta,0,VisibleAccountOrder.Num()-1);
	const FString Id=VisibleAccountOrder[Index];
	if (Id!=Profile.SelectedAccountId) RunAction(TEXT("selectaccount"),Id);
	if (const TObjectPtr<UACELobbyAccountItem>* Item=AccountItems.Find(Id))
		((*Item)->bFavorite?FavoriteList:AccountList)->RequestScrollItemIntoView(Item->Get());
}

bool UACELoginWidget::CanReorderAccounts() const
{
	return Pages && Pages->GetActiveWidgetIndex()==6 && PendingRemoval.IsEmpty();
}

bool UACELoginWidget::MoveAccountInOrder(const FString& Id, const FString& TargetId, bool bAfter)
{
	if (!CanReorderAccounts() || Id==TargetId) return false;
	const int32 From=Profile.Accounts.IndexOfByPredicate([&](const auto& A){return A.Id==Id;});
	const int32 Target=Profile.Accounts.IndexOfByPredicate([&](const auto& A){return A.Id==TargetId;});
	if (From==INDEX_NONE || Target==INDEX_NONE) return false;
	int32 To=Target+(bAfter?1:0);
	if (From<To) --To;
	if (From==To) return false;
	// Move the entire identity; usernames need not be unique. Leave selection
	// and any unsaved form edits alone, and use the existing profile format.
	FACELoginAccount Account=MoveTemp(Profile.Accounts[From]);
	Profile.Accounts.RemoveAt(From);
	Profile.Accounts.Insert(MoveTemp(Account),To);
	bLoginEntriesDirty=true;
	SaveLoginEntries();
	// Rebuild both lists after the drop callback has finished using the row.
	bAccountOrderDirty=true;
	return true;
}

bool UACELoginWidget::StoreAccount()
{
	const FString User=AccountBox->GetText().ToString().TrimStartAndEnd(), Secret=PasswordBox->GetText().ToString();
	const FString Alias=AccountAliasBox->GetText().ToString().TrimStartAndEnd();
	if (User.IsEmpty() || User.Len()>128 || Secret.IsEmpty() || Secret.Len()>256 || User.Contains(TEXT(":")))
	{ SetStatus(TEXT("Enter a username and password. Usernames cannot contain a colon.")); return false; }
	if (Alias.Len()>100 || Alias.Contains(TEXT("\n")) || Alias.Contains(TEXT("\r")))
	{ SetStatus(TEXT("Enter an alias of up to 100 characters on one line.")); return false; }
	auto* A=Profile.Accounts.FindByPredicate([&](const auto& E){return E.Id==Profile.SelectedAccountId;});
	// New account always creates an independent entry. Matching credentials can
	// belong to different aliases; only selection by ID is an edit operation.
	if (!A)
	{
		if (Profile.Accounts.Num()>=512) { SetStatus(TEXT("Remove an unused account before adding another.")); return false; }
		A=&Profile.Accounts.AddDefaulted_GetRef(); A->Id=FGuid::NewGuid().ToString();
	}
	A->Username=User; A->Password=Secret; A->Alias=Alias; Profile.SelectedAccountId=A->Id;
	Profile.Account=User; Profile.Password=Secret; bLoginEntriesDirty=true; SaveLoginEntries(); RefreshAccounts(); return !bLoginEntriesDirty;
}

void UACELoginWidget::EditServer(bool bNew)
{
	const auto* S=bNew?nullptr:Profile.SelectedServer();
	if (!bNew && !S) { SetStatus(TEXT("Select a server to edit.")); return; }
	EditingServerId=S?S->Id:FString(); bEditingGDLE=S && S->bGDLE;
	NameBox->SetText(FText::FromString(S?S->Name:TEXT(""))); HostBox->SetText(FText::FromString(S?S->Host:TEXT("")));
	PortBox->SetText(FText::FromString(S?FString::FromInt(S->Port):TEXT("9000")));
	DescriptionBox->SetText(FText::FromString(S?S->Description:TEXT("")));
	WebsiteBox->SetText(FText::FromString(S?S->Website:TEXT(""))); DiscordBox->SetText(FText::FromString(S?S->Discord:TEXT("")));
	ButtonStyle(ACETypeButton,!bEditingGDLE); ButtonStyle(GDLETypeButton,bEditingGDLE); Pages->SetActiveWidgetIndex(1);
	SetStatus(TEXT("Choose the login type used by this server."));
}

void UACELoginWidget::DoLogin()
{
	SetPasswordVisible(false);
	if (Updater && Updater->IsBusy()) { SetStatus(TEXT("Finish or cancel the update before logging in.")); return; }
	// Check both files before authenticating: the cell index is deliberately
	// deferred, so portal readiness alone can still lead to an empty world.
	if (!ValidateGameFiles()) return;
	if (!Profile.SelectedServer()) { SetStatus(TEXT("Choose a server first.")); return; }
	if (!StoreAccount()) { Pages->SetActiveWidgetIndex(6); return; }
	const auto* S=Profile.SelectedServer();
	if (Client && S)
	{
		if (auto* GI=GetGameInstance()) if (auto* Dat=GI->GetSubsystem<UACEDatSubsystem>()) Dat->BeginBackgroundLoad(true);
		SetStatus(TEXT("Connecting..."));
		// Connect() resets the previous session first; that Disconnected is not a cancel.
		bSuppressConnectDisconnect=true;
		const bool bStarted=Client->Login(S->Host,S->Port,Profile.Account,Profile.Password,S->bGDLE);
		bSuppressConnectDisconnect=false;
		if (!bStarted) { EndConnectScreen(); SetStatus(TEXT("Could not start the connection. Check the server address.")); }
		else if (auto* Used=Profile.Accounts.FindByPredicate([&](const FACELoginAccount& E){return E.Id==Profile.SelectedAccountId;}))
		{
			// Shown as "Last launched" and used by the Recent sort; stays on this device only.
			Used->LastUsed=FDateTime::UtcNow().ToUnixTimestamp(); bLoginEntriesDirty=true;
			SaveLoginEntries(); RefreshAccountLists();
		}
		else if (Client->GetSessionState()!=EACESessionState::Failed && Client->GetSessionState()!=EACESessionState::Disconnected) ShowConnectScreen();
	}
}

void UACELoginWidget::OnSearchChanged(const FText&) { RefreshBrowser(); }
void UACELoginWidget::OnDatChanged(const FText&) { UpdateDatStatus(); }

void UACELoginWidget::RefreshBrowser()
{
	BrowserList->ClearChildren(); const FString Search=SearchBox->GetText().ToString().TrimStartAndEnd();
	for (const auto& S:Directory)
	{
		if (!Search.IsEmpty() && !S.Name.Contains(Search) && !S.Description.Contains(Search) && !S.Host.Contains(Search)) continue;
		const bool Saved=Profile.Servers.ContainsByPredicate([&](const auto& E){return E.Host.Equals(S.Host,ESearchCase::IgnoreCase) && E.Port==S.Port;});
		const FString Name=FString::Printf(TEXT("%s  /  %s%s"),*S.Name,S.bGDLE?TEXT("GDLE"):TEXT("ACE"),Saved?TEXT("  /  Saved"):TEXT(""));
		Cast<UScrollBoxSlot>(BrowserList->AddChild(ActionButton(Name,TEXT("directoryselect"),S.Id,S.Id==BrowserServerId)))->SetPadding(FMargin(0,0,0,6));
	}
	if (!BrowserList->GetChildrenCount()) BrowserList->AddChild(Label(Directory.IsEmpty()?TEXT("No cached directory. Refresh when connected to the internet."):TEXT("No servers match your search."),18,true));
	const auto* S=Directory.FindByPredicate([&](const auto& E){return E.Id==BrowserServerId;});
	BrowserDetail->SetText(FText::FromString(S?FString::Printf(TEXT("%s:%d\n%s"),*S->Host,S->Port,*S->Description.Left(600)):TEXT("Select a server to see its details and community links.")));
}

void UACELoginWidget::FetchDirectory()
{
	if (bRefreshingDirectory) return;
	bRefreshingDirectory=true; SetStatus(TEXT("Refreshing the community directory..."));
	DirectoryRequest=FHttpModule::Get().CreateRequest(); DirectoryRequest->SetURL(ACELoginProfile::DirectoryUrl);
	DirectoryRequest->SetVerb(TEXT("GET")); DirectoryRequest->SetTimeout(20.f);
	DirectoryRequest->SetHeader(TEXT("Accept"),TEXT("application/xml,text/xml,text/plain"));
	TWeakObjectPtr<UACELoginWidget> Weak(this);
	DirectoryRequest->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Response, bool Success)
	{
		if (!Weak.IsValid()) return; auto* W=Weak.Get(); W->bRefreshingDirectory=false;
		FString Error; TArray<FACELoginServer> Parsed;
		if (!Success || !Response || Response->GetResponseCode()!=200 || Response->GetContent().Num()>2*1024*1024)
			Error=TEXT("Directory unavailable. Your saved servers and cached directory are still available.");
		else if (ACELoginProfile::ParseDirectory(Response->GetContentAsString(),Parsed,Error))
		{
			W->Directory=MoveTemp(Parsed);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ACELoginProfile::DirectoryCachePath()),true);
			FFileHelper::SaveStringToFile(Response->GetContentAsString(),*ACELoginProfile::DirectoryCachePath(),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			W->SetStatus(FString::Printf(TEXT("%d community servers. Select one to add it to your list."),W->Directory.Num()));
		}
		if (!Error.IsEmpty()) W->SetStatus(Error);
		W->RefreshBrowser();
	});
	if (!DirectoryRequest->ProcessRequest()) { bRefreshingDirectory=false; SetStatus(TEXT("Could not refresh the directory. You can still use saved servers.")); }
}

void UACELoginWidget::UpdateDatStatus()
{
	if (!FileStatus || !DatBox) return;
	const FString Dir=DatBox->GetText().ToString().TrimStartAndEnd();
	FString Report;
	for (const TCHAR* File:{TEXT("client_portal.dat"),TEXT("client_cell_1.dat"),TEXT("client_highres.dat")})
		Report+=FString::Printf(TEXT("%s   %s\n"),IFileManager::Get().FileSize(*(Dir/File))>0?TEXT("Found"):TEXT("Missing"),File);
	Report+=TEXT("Portal and cell files are required. High-resolution textures are used when available.");
	FileStatus->SetText(FText::FromString(Report));
}

bool UACELoginWidget::ValidateGameFiles()
{
	// Validate the active installation, not an unsaved edit in the folder field.
	FString Dir=Profile.DatDirectory;
	if (auto* GI=GetGameInstance()) if (auto* Dat=GI->GetSubsystem<UACEDatSubsystem>()) Dir=Dat->GetDatDirectory();
	TArray<FString> Missing;
	for (const TCHAR* File:{TEXT("client_portal.dat"),TEXT("client_cell_1.dat")})
		if (Dir.IsEmpty() || IFileManager::Get().FileSize(*(Dir/File))<=0) Missing.Add(File);
	if (Missing.IsEmpty())
	{
		FileProblem->SetVisibility(ESlateVisibility::Collapsed);
		return true;
	}
	FString Message=FString::Printf(TEXT("Game files are missing or empty: %s.\nSearched folder: %s\n"),
		*FString::Join(Missing,TEXT(", ")),Dir.IsEmpty()?TEXT("No folder selected"):*Dir);
#if PLATFORM_ANDROID
	Message+=TEXT("Close the game, connect your Quest to your computer, and run Repair-Quest.cmd from the extracted Quest installer to install the DAT files. Then reopen the game.");
#else
	Message+=TEXT("Select the folder containing your Asheron's Call DAT files below, then click Save location and return to Play. If you have not installed the game data yet, install it first.");
#endif
	// This text uses the packaged engine font; it must work without any DAT UI assets.
	FileProblem->SetText(FText::FromString(Message));
	FileProblem->SetVisibility(ESlateVisibility::HitTestInvisible);
	DatBox->SetText(FText::FromString(Dir));
	Pages->SetActiveWidgetIndex(3);
	SetVisibility(ESlateVisibility::Visible);
	SetStatus(TEXT("Game files required. See Game files for the missing files and repair instructions."));
	return false;
}

void UACELoginWidget::SetPasswordVisible(bool bVisible)
{
	if(PasswordBox)PasswordBox->SetIsPassword(!bVisible);
	if(PasswordVisibilityButton)
	{
		PasswordVisibilityButton->SetToolTipText(FText::FromString(bVisible?TEXT("Hide password"):TEXT("Show password")));
		if(auto* Eye=Cast<UNativeWidgetHost>(PasswordVisibilityButton->GetContent()))
			if(auto Icon=Eye->GetContent())Icon->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UACELoginWidget::RunAction(const FString& Action, const FString& Value)
{
	if(Action==TEXT("togglepassword")){SetPasswordVisible(PasswordBox&&PasswordBox->GetIsPassword());return;}
	if(Action==TEXT("clearsearch"))
	{
		if(AccountSearchBox && !AccountSearchBox->GetText().IsEmpty()) { AccountSearchBox->SetText(FText::GetEmpty()); OnAccountSearchChanged(FText::GetEmpty()); }
		return;
	}
	if(Action==TEXT("scrollaccountsup")||Action==TEXT("scrollaccountsdown")) { if(AccountList) AccountList->ScrollBy(Action.EndsWith(TEXT("up"))?-3.f:3.f); return; }
	if(Action==TEXT("togglefavorite"))
	{
		if(auto* A=Profile.Accounts.FindByPredicate([&](const FACELoginAccount& E){return E.Id==Value;}))
		{
			A->bFavorite=!A->bFavorite; bLoginEntriesDirty=true;
			RefreshAccountLists(); SaveLoginEntries();
		}
		return;
	}
	if(Action==TEXT("sortaz")||Action==TEXT("sortrecent")||Action==TEXT("sortcustom"))
	{
		Profile.AccountSort=Action.Mid(4); bLoginEntriesDirty=true;
		RefreshAccountLists(); SaveLoginEntries();
		return;
	}
	if(Action==TEXT("cancelconnect"))
	{
		// Drops the half-open session (the server is told to disconnect) and returns to the lobby.
		const auto Session=Client?Client->GetSession():nullptr;
		// A download started by the connect screen is part of this login; stop it too.
		if (bUpdateDownloadRequested && Updater && Updater->IsBusy()) Updater->Cancel();
		EndConnectScreen(); bConnectScreenPreview=false;
		if(Session && Session->GetState()!=EACESessionState::Disconnected) Session->Disconnect();
		SetStatus(TEXT("Connection cancelled."));
		return;
	}
	// Reveal is temporary UI state, never a saved account preference.
	if(Action==TEXT("play")||Action==TEXT("plugins")||Action==TEXT("updates")||Action==TEXT("files")||Action==TEXT("browser")||Action==TEXT("launch")||Action==TEXT("newaccount"))SetPasswordVisible(false);
	if (Action==TEXT("quit"))
	{
		SaveLoginEntries();
		UKismetSystemLibrary::QuitGame(this,GetOwningPlayer(),EQuitPreference::Quit,false);
		return;
	}
	if (Action==TEXT("plugins")) { Pages->SetActiveWidgetIndex(5); return; }
	if (Action==TEXT("accounts")) { Pages->SetActiveWidgetIndex(6); return; }
	if (Action==TEXT("updates")) { Pages->SetActiveWidgetIndex(4); RefreshUpdateControls(); return; }
	if (Updater)
	{
		if (Action==TEXT("checkupdate")) { Updater->Check(); RefreshUpdateControls(); return; }
		if (Action==TEXT("downloadupdate")) { Updater->Download(); RefreshUpdateControls(); return; }
		if (Action==TEXT("cancelupdate")) { Updater->Cancel(); RefreshUpdateControls(); return; }
		if (Action==TEXT("installupdate")) { SaveLoginEntries(); Updater->Install(UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled()); RefreshUpdateControls(); return; }
		if (Action==TEXT("updatenotes")) { FPlatformProcess::LaunchURL(*(Updater->Release.Number>0?FString::Printf(TEXT("https://thwargle.com/downloads/ac/v%d/RELEASE-NOTES.md"),Updater->Release.Number):FString(TEXT("https://thwargle.com/unreal/#download"))),nullptr,nullptr); return; }
	}
	if (Action==TEXT("launch")) { DoLogin(); return; }
	if (Action==TEXT("enter")) { DoEnterSelectedCharacter(); return; }
	if (Action==TEXT("play")) { Pages->SetActiveWidgetIndex(0); SelectServer(Profile.SelectedServerId); }
	else if (Action==TEXT("selectserver")) { SelectServer(Value); SetStatus(TEXT("Server selected. Choose an account and launch.")); }
	else if (Action==TEXT("selectaccount")) { Profile.SelectedAccountId=Value; SelectServer(Profile.SelectedServerId); }
	else if (Action==TEXT("newserver") || Action==TEXT("editserver")) EditServer(Action==TEXT("newserver"));
	else if (Action==TEXT("ace") || Action==TEXT("gdle"))
	{ bEditingGDLE=Action==TEXT("gdle"); ButtonStyle(ACETypeButton,!bEditingGDLE); ButtonStyle(GDLETypeButton,bEditingGDLE); }
	else if (Action==TEXT("saveserver"))
	{
		FACELoginServer S; S.Id=EditingServerId.IsEmpty()?FGuid::NewGuid().ToString():EditingServerId;
		S.Name=NameBox->GetText().ToString().TrimStartAndEnd(); S.Host=HostBox->GetText().ToString().TrimStartAndEnd();
		const FString Port=PortBox->GetText().ToString().TrimStartAndEnd(); S.Port=Port.IsNumeric()?FCString::Atoi(*Port):0;
		S.bGDLE=bEditingGDLE; S.Description=DescriptionBox->GetText().ToString().Left(4096);
		S.Website=WebsiteBox->GetText().ToString().TrimStartAndEnd(); S.Discord=DiscordBox->GetText().ToString().TrimStartAndEnd();
		FString Error; if (!ACELoginProfile::ValidateServer(S,Error)) { SetStatus(Error); return; }
		if (Profile.Servers.ContainsByPredicate([&](const auto& E){return E.Id!=S.Id && E.Host.Equals(S.Host,ESearchCase::IgnoreCase) && E.Port==S.Port;}))
		{ SetStatus(TEXT("This address and port are already in your server list.")); return; }
		auto* Existing=Profile.Servers.FindByPredicate([&](const auto& E){return E.Id==S.Id;});
		if (Existing) *Existing=S;
		else { if (Profile.Servers.Num()>=128) { SetStatus(TEXT("Remove an unused server before adding another.")); return; } Profile.Servers.Add(S); }
		SelectServer(S.Id); Pages->SetActiveWidgetIndex(0); SetStatus(TEXT("Server saved."));
	}
	else if (Action==TEXT("saveaccount")) { if (StoreAccount()) SetStatus(TEXT("Account saved securely on this device.")); }
	else if (Action==TEXT("newaccount"))
	{
		Profile.SelectedAccountId.Reset(); Profile.Account.Reset(); Profile.Password.Reset();
		AccountAliasBox->SetText(FText::GetEmpty());
		AccountBox->SetText(FText::GetEmpty()); PasswordBox->SetText(FText::GetEmpty()); RefreshAccounts(); bLoginEntriesDirty=true;
	}
	else if (Action==TEXT("removeserver") || Action==TEXT("removeaccount"))
	{
		if ((Action==TEXT("removeserver") && !Profile.SelectedServer()) || (Action==TEXT("removeaccount") && Profile.SelectedAccountId.IsEmpty())) return;
		PendingRemoval=Action==TEXT("removeserver")?TEXT("s:")+Profile.SelectedServerId:TEXT("a:")+Profile.SelectedAccountId;
		ConfirmText->SetText(FText::FromString(Action==TEXT("removeserver")?TEXT("Remove this server from your saved list? Your accounts will remain available."):TEXT("Remove this saved account from this device?")));
		ConfirmPanel->SetVisibility(ESlateVisibility::Visible);
	}
	else if (Action==TEXT("confirmremove"))
	{
		if (PendingRemoval.StartsWith(TEXT("s:"))) Profile.RemoveServer(PendingRemoval.Mid(2));
		else if (PendingRemoval.StartsWith(TEXT("a:"))) Profile.Accounts.RemoveAll([&](const auto& A){return A.Id==PendingRemoval.Mid(2);});
		PendingRemoval.Reset(); ConfirmPanel->SetVisibility(ESlateVisibility::Collapsed); SelectServer(Profile.SelectedServerId); SetStatus(TEXT("Removed from this device."));
	}
	else if (Action==TEXT("cancelremove")) { PendingRemoval.Reset(); ConfirmPanel->SetVisibility(ESlateVisibility::Collapsed); }
	else if (Action==TEXT("browser")) { Pages->SetActiveWidgetIndex(2); RefreshBrowser(); if (Directory.IsEmpty()) FetchDirectory(); }
	else if (Action==TEXT("refresh")) FetchDirectory();
	else if (Action==TEXT("directoryselect")) { BrowserServerId=Value; RefreshBrowser(); }
	else if (Action==TEXT("import"))
	{
		const auto* S=Directory.FindByPredicate([&](const auto& E){return E.Id==BrowserServerId;});
		if (!S) { SetStatus(TEXT("Select a server in the directory first.")); return; }
		const auto* Existing=Profile.Servers.FindByPredicate([&](const auto& E){return E.Host.Equals(S->Host,ESearchCase::IgnoreCase) && E.Port==S->Port;});
		if (Existing) SelectServer(Existing->Id);
		else
		{
			if (Profile.Servers.Num()>=128) { SetStatus(TEXT("Remove an unused server before adding another.")); return; }
			FACELoginServer Copy=*S; Copy.Id=FGuid::NewGuid().ToString(); Profile.Servers.Add(Copy); SelectServer(Copy.Id);
		}
		Pages->SetActiveWidgetIndex(0); SetStatus(TEXT("Server added. Choose an account to launch."));
	}
	else if (Action==TEXT("website") || Action==TEXT("discord") || Action==TEXT("browserwebsite") || Action==TEXT("browserdiscord"))
	{
		const auto* S=Action.StartsWith(TEXT("browser"))?Directory.FindByPredicate([&](const auto& E){return E.Id==BrowserServerId;}):Profile.SelectedServer();
		if (!S) return; const FString Url=Action.EndsWith(TEXT("discord"))?S->Discord:S->Website;
		if (ACELoginProfile::IsWebLink(Url)) FPlatformProcess::LaunchURL(*Url,nullptr,nullptr);
		else SetStatus(TEXT("This server has not provided that link."));
	}
	else if (Action==TEXT("files")) { Pages->SetActiveWidgetIndex(3); UpdateDatStatus(); }
	else if (Action==TEXT("defaultdat"))
	{
#if PLATFORM_ANDROID || PLATFORM_LINUX || PLATFORM_MAC
		DatBox->SetText(FText::FromString(FPaths::ProjectSavedDir()/TEXT("DAT")));
#else
		DatBox->SetText(FText::FromString(TEXT("C:/Turbine/Asheron's Call")));
#endif
	}
	else if (Action==TEXT("savedat"))
	{
		FString Dir=DatBox->GetText().ToString().TrimStartAndEnd(); FPaths::NormalizeDirectoryName(Dir);
		if (Dir.IsEmpty() || IFileManager::Get().FileSize(*(Dir/TEXT("client_portal.dat")))<=0 || IFileManager::Get().FileSize(*(Dir/TEXT("client_cell_1.dat")))<=0)
		{ SetStatus(TEXT("Choose a folder containing client_portal.dat and client_cell_1.dat.")); return; }
		Profile.DatDirectory=Dir; bLoginEntriesDirty=true;
		bool Restart=false;
		if (auto* GI=GetGameInstance()) if (auto* Dat=GI->GetSubsystem<UACEDatSubsystem>())
		{
			Restart=(Dat->IsDatReady() || Dat->IsDatLoading()) && Dat->GetDatDirectory()!=Dir;
			if (!Restart) Dat->SetDatDirectory(Dir);
		}
		SetStatus(Restart?TEXT("Location saved. Restart the client to use these game files."):TEXT("Game-file location saved."));
		FileProblem->SetVisibility(ESlateVisibility::Collapsed);
	}
	else if (Action==TEXT("browsefolder"))
	{
#if PLATFORM_WINDOWS
		// Runtime Windows picker; no dependency on editor-only DesktopPlatform.
		const HRESULT Init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
		IFileOpenDialog* Dialog=nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&Dialog))))
		{
			DWORD Options=0; Dialog->GetOptions(&Options); Dialog->SetOptions(Options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
			Dialog->SetTitle(L"Choose your Asheron's Call DAT folder");
			if (SUCCEEDED(Dialog->Show(nullptr)))
			{
				IShellItem* Item=nullptr;
				if (SUCCEEDED(Dialog->GetResult(&Item)))
				{
					PWSTR Path=nullptr; if (SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH,&Path))) { DatBox->SetText(FText::FromString(Path)); CoTaskMemFree(Path); } Item->Release();
				}
			}
			Dialog->Release();
		}
		if (SUCCEEDED(Init)) CoUninitialize();
#endif
	}
	SaveLoginEntries();
}

bool UACELoginWidget::IsConnectScreenVisible() const
{
	return ConnectScreen && ConnectScreen->GetVisibility()!=ESlateVisibility::Collapsed;
}

void UACELoginWidget::ShowConnectScreen()
{
	if (!ConnectScreen) return;
	ConnectFailTime=-1;
	if (!IsConnectScreenVisible())
	{
		// Both vials start empty, like retail, and fill as each stage completes.
		ConnectFill=UpdateVialFill=0; ConnectPhase=0; ConnectPhaseTime=0; bUpdateDownloadRequested=bUpdatePhaseFinished=false;
		SetConnectStep(.05f,.45f,TEXT("Connecting to server..."));
		if (ConnectUpdateLabel) ConnectUpdateLabel->SetText(FText::FromString(TEXT("Update progress")));
		ConnectDetail->SetText(FText::GetEmpty());
		ConnectCancelButton->SetVisibility(ESlateVisibility::Visible);
		ConnectScreen->SetVisibility(ESlateVisibility::Visible);
		LoadVialArt();
	}
}

void UACELoginWidget::EndConnectScreen()
{
	ConnectFailTime=-1; ConnectPhase=0;
	if (ConnectScreen) ConnectScreen->SetVisibility(ESlateVisibility::Collapsed);
}

void UACELoginWidget::SetConnectStep(float Base, float Cap, const FString& Text)
{
	ConnectStepBase=Base; ConnectStepCap=Cap; ConnectStepTime=0;
	if (ConnectLabel && ConnectLabel->GetText().ToString()!=Text) ConnectLabel->SetText(FText::FromString(Text));
}

namespace
{
	// Each vial is seen filling without holding up character select: a fill takes about a
	// quarter second, so a connection with no update adds roughly half a second in total.
	constexpr float ConnectMinPhase=.2f;
	constexpr float ConnectFillSpeed=16.f;
}

bool UACELoginWidget::IsConnectScreenHoldingCharacterSelect() const
{
	return IsConnectScreenVisible() && ConnectFailTime<0 && ConnectPhase<3;
}

void UACELoginWidget::MarkConnected()
{
	if (ConnectPhase!=0) return;
	ConnectPhase=1; ConnectPhaseTime=0;
	SetConnectStep(1.f,1.f,TEXT("Connected!"));
}

void UACELoginWidget::BeginUpdatePhase()
{
	ConnectPhase=2; ConnectPhaseTime=0; bUpdateDownloadRequested=bUpdatePhaseFinished=false;
	if (bConnectScreenPreview || !Updater) return;
	// Await a launcher check that is still in flight, and keep an already verified download.
	if (Updater->State!=EACEUpdateState::Checking && Updater->State!=EACEUpdateState::Ready && !Updater->IsBusy())
		Updater->CheckWhileConnecting();
}

void UACELoginWidget::TickUpdatePhase(float& Target, FString& Text)
{
	bUpdatePhaseFinished=false;
	const float Checking=.35f*(1.f-FMath::Exp(-ConnectPhaseTime/.3f));
	if (bConnectScreenPreview)
	{
		if (ConnectPhaseTime<.4f) { Text=TEXT("Checking for updates..."); Target=Checking; }
		else { Text=TEXT("Patching Done!"); Target=1; bUpdatePhaseFinished=true; }
		return;
	}
	if (!Updater) { Text=TEXT("Updates not checked"); Target=0; bUpdatePhaseFinished=true; return; }
	const int32 Number=Updater->Release.Number;
	switch (Updater->State)
	{
	case EACEUpdateState::Checking: Text=TEXT("Checking for updates..."); Target=Checking; return;
	case EACEUpdateState::Current: Text=TEXT("Patching Done!"); Target=1; break; // Retail wording.
	case EACEUpdateState::Available:
		if (!bUpdateDownloadRequested && Updater->CanDownloadWhileConnecting())
		{
			bUpdateDownloadRequested=true; Updater->DownloadWhileConnecting();
			if (Updater->State!=EACEUpdateState::Available) { Text=TEXT("Downloading update 0%"); Target=0; return; }
		}
		// Updates need consent (automatic updates) and an installer; macOS has neither, so only report it.
		Text=Number>0?FString::Printf(TEXT("Update available (v%d)"),Number):FString(TEXT("Update available")); Target=1; break;
	case EACEUpdateState::Downloading: Text=FString::Printf(TEXT("Downloading update %.0f%%"),Updater->Progress()*100); Target=Updater->Progress(); return;
	case EACEUpdateState::Verifying: Text=TEXT("Verifying update..."); Target=FMath::Max(Updater->Progress(),.97f); return;
	// Installing closes the game, so it waits for the lobby (the existing automatic install, or Install on the Updates page).
	case EACEUpdateState::Ready: Text=Number>0?FString::Printf(TEXT("Update v%d ready to install"),Number):FString(TEXT("Update ready to install")); Target=1; break;
	case EACEUpdateState::InstallNotice:
	case EACEUpdateState::Installing: Text=TEXT("Installing update..."); Target=1; break;
	case EACEUpdateState::Error: Text=bUpdateDownloadRequested?TEXT("Update download failed"):TEXT("Could not check for updates"); Target=0; break;
	default: Text=TEXT("Updates not checked"); Target=0; break;
	}
	bUpdatePhaseFinished=true;
}

void UACELoginWidget::TickConnectPreview(float Dt)
{
	// Walk the real sequence every 2.5 seconds: connect fills, then the update vial.
	const float Previous=ConnectPreviewTime; ConnectPreviewTime=FMath::Fmod(ConnectPreviewTime+Dt,2.5f);
	if (ConnectPreviewTime<Previous)
	{
		++ConnectPreviewPass; ConnectFill=UpdateVialFill=0; ConnectPhase=0; ConnectPhaseTime=0;
		SetConnectStep(.05f,.45f,TEXT("Connecting to server..."));
	}
	else if (Previous<.15f && ConnectPreviewTime>=.15f) SetConnectStep(.45f,.9f,TEXT("Logging in..."));
	else if (Previous<.3f && ConnectPreviewTime>=.3f) MarkConnected();
#if !UE_BUILD_SHIPPING
	// -ACEConnectScreenPreviewShot: capture (with UI) on the second pass, once the DAT art is in.
	if (ConnectPreviewPass==1 && FParse::Param(FCommandLine::Get(),TEXT("ACEConnectScreenPreviewShot")))
	{
		if (Previous<.7f && ConnectPreviewTime>=.7f) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/ConnectPreview_Checking.png"),true,false);
		if (Previous<2.2f && ConnectPreviewTime>=2.2f) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/ConnectPreview_Done.png"),true,false);
	}
#endif
}

void UACELoginWidget::TickConnectScreen(float Dt)
{
	if (!IsConnectScreenVisible()) return;
	ConnectStepTime+=Dt; ConnectPhaseTime+=Dt;
	if (bConnectScreenPreview) TickConnectPreview(Dt);
	float Target;
	if (ConnectFailTime>=0)
	{
		ConnectFailTime+=Dt; Target=0;
		// Drain, let the reason be read, then return to the lobby (which shows it too).
		if (ConnectFailTime>3.f && ConnectFill<.01f) { EndConnectScreen(); return; }
	}
	else if (ConnectPhase>=1) Target=1; // Connected: fill straight to the top.
	else // Creep toward the next stage while waiting so a slow server still looks alive.
		Target=ConnectStepBase+(ConnectStepCap-ConnectStepBase)*.8f*(1.f-FMath::Exp(-ConnectStepTime/.6f));
	ConnectFill=FMath::FInterpTo(ConnectFill,Target,Dt,ConnectFailTime>=0?2.f:ConnectFillSpeed);
	// Retail order: the update vial stays empty until the connection vial is full.
	if (ConnectPhase==1 && ConnectFailTime<0 && ConnectFill>.98f && ConnectPhaseTime>=ConnectMinPhase) BeginUpdatePhase();
	float UpdateTarget=0; FString UpdateTextValue=TEXT("Update progress");
	if (ConnectPhase>=2) TickUpdatePhase(UpdateTarget,UpdateTextValue);
	UpdateVialFill=FMath::FInterpTo(UpdateVialFill,UpdateTarget,Dt,ConnectFillSpeed); // Downloads still follow real progress.
	if (ConnectPhase==2 && ConnectFailTime<0 && bUpdatePhaseFinished && FMath::Abs(UpdateVialFill-UpdateTarget)<.02f && ConnectPhaseTime>=ConnectMinPhase)
		ConnectPhase=3; // Character select may open now (once the character list is in).
	if (ConnectUpdateLabel && ConnectUpdateLabel->GetText().ToString()!=UpdateTextValue) ConnectUpdateLabel->SetText(FText::FromString(UpdateTextValue));
}

void UACELoginWidget::LoadVialArt()
{
	if (!VialArt) VialArt=MakeShared<FACEVialArt>();
	const bool bLogoReady=!ConnectLogo || ConnectLogo->GetRenderOpacity()>0;
	if (VialArt->IsReady() && bLogoReady) return;
	auto* GI=GetGameInstance(); auto* Dat=GI?GI->GetSubsystem<UACEDatSubsystem>():nullptr;
	if (!Dat || !Dat->IsDatReady() || !Dat->GetUiResources()) return;
	UACEUIResourceResolver* Res=Dat->GetUiResources();
	if (!bLogoReady)
		if (UTexture2D* Logo=Res->ResolveTexture(ACETitleLogoId)) { VialTextures.Add(Logo); ConnectLogo->SetBrushFromTexture(Logo); ConnectLogo->SetRenderOpacity(1); }
	if (VialArt->IsReady()) return;
	TArray<FSlateBrush> Full, Empty;
	for (int32 I=0;I<UE_ARRAY_COUNT(ACEVialFullIds);++I)
	{
		UTexture2D* A=Res->ResolveTexture(ACEVialFullIds[I]); UTexture2D* B=Res->ResolveTexture(ACEVialEmptyIds[I]);
		if (!A || !B) return; // Retry on the next poll; the plain bar is drawn meanwhile.
		VialTextures.AddUnique(A); VialTextures.AddUnique(B);
		FSlateBrush Brush; Brush.DrawAs=ESlateBrushDrawType::Image; Brush.ImageSize=FVector2D(VialW,VialH);
		Brush.SetResourceObject(A); Full.Add(Brush);
		Brush.SetResourceObject(B); Empty.Add(Brush);
	}
	VialArt->Full=MoveTemp(Full); VialArt->Empty=MoveTemp(Empty);
}

