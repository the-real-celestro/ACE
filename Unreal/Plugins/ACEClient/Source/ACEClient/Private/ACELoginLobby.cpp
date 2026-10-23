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
#include "Components/ButtonSlot.h"
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
#include "Components/WidgetSwitcherSlot.h"
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
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
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
	/** sRGB hex as used by the Codex Hall mockups. */
	FLinearColor Hex(uint32 RGB, float A = 1.f)
	{
		FLinearColor C(FColor(uint8(RGB >> 16), uint8(RGB >> 8), uint8(RGB)));
		C.A = A;
		return C;
	}
	/** The retail art each launcher button is drawn with (UACELoginActionButton::LobbyStyle). */
	enum ELobbyStyle : uint8 { LobbyRed = 0, LobbySmall = 1, LobbyRow = 2, LobbyRing = 3, LobbyGlyph = 4, LobbyNav = 5, LobbyPrimary = 6,
		LobbyGhost = 7, LobbyPlaque = 8, LobbySegment = 9, LobbySwatch = 10, LobbyEye = 11, LobbyScrim = 12, LobbyGhostSmall = 13 };
	/** Logical lobby stage (the Codex Hall mockups); the root ScaleBox fits it to the window. */
	constexpr float DesignW = 1600.f, DesignH = 1000.f;
	constexpr float ServerRowHeight = 44.f;
	/** 2px gold bar on the left edge of a selected quiet row (an empty slot otherwise, so text never shifts). */
	UWidget* RowAccentBar(UWidgetTree* Tree, bool bShow)
	{
		auto* Size=Tree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(2); Size->SetVisibility(ESlateVisibility::HitTestInvisible);
		auto* Bar=Tree->ConstructWidget<UBorder>(); Bar->SetBrush(FSlateRoundedBoxBrush(ACELobbyArt::RowAccent,1.f));
		Bar->SetRenderOpacity(bShow?1.f:0.f); Size->SetContent(Bar);
		return Size;
	}
	/** Page tint used by tint-dependent button art (Launch hover glow). */
	int32 GLobbyTint = 0;
	/** -ACELobbyShot only: draw the Launch ring in its hover state for a capture. */
	bool GLobbyForceHover = false;
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
			const auto Color=Hex(0xe7b65a)*Style.GetColorAndOpacityTint();
			for(const auto* Path:{&Outline,&Pupil})FSlateDrawElement::MakeLines(Out,Layer,Art.ToPaintGeometry(),*Path,Effect,Color,true,1.6f);
			if(Revealed.Get())FSlateDrawElement::MakeLines(Out,Layer,Art.ToPaintGeometry(),TArray<FVector2D>{FVector2D(3,2),FVector2D(21,18)},Effect,Color,true,1.8f);
			return Layer;
		}
	private:
		TAttribute<bool> Revealed;
		TArray<FVector2D> Outline,Pupil;
	};
	const TCHAR* const ServerTypeHint=TEXT("Choose the login type used by this server.");
	uint8 LobbyStyleFor(const FString& Action)
	{
		if (Action==TEXT("launch")) return LobbyRing;
		if (Action==TEXT("selectserver") || Action==TEXT("selectaccount") || Action==TEXT("directoryselect")) return LobbyRow;
		if (Action.StartsWith(TEXT("sort"))) return LobbySegment;
		if (Action==TEXT("togglepassword")) return LobbyEye;
		if (Action==TEXT("clearsearch") || Action.StartsWith(TEXT("scrollaccounts"))) return LobbyGlyph;
		if (Action==TEXT("worldmenu")) return LobbyPlaque;
		if (Action==TEXT("closeworld")) return LobbyScrim;
		if (Action==TEXT("tint")) return LobbySwatch;
		if (Action==TEXT("saveaccount") || Action==TEXT("saveserver") || Action==TEXT("import") || Action==TEXT("savedat")) return LobbyPrimary;
		if (Action==TEXT("removeaccount") || Action==TEXT("cancelremove")) return LobbyGhost;
		if (Action==TEXT("removeserver") || Action==TEXT("quit")) return LobbyGhostSmall;
		if (Action==TEXT("website") || Action==TEXT("discord") || Action==TEXT("editserver") || Action==TEXT("newserver") || Action==TEXT("browser")
			|| Action==TEXT("newaccountsmall")) return LobbySmall;
		return LobbyRed;
	}
	FLinearColor CaptionColor(uint8 Kind, bool Accent, const FString& Action)
	{
		if (Kind==LobbyRing) return GLobbyForceHover?Hex(0xfff6d8):Hex(0xf8e6ab);
		if (Kind==LobbyRow) return Accent?Hex(0xfff7d6):Hex(0xe9dcb8);
		if (Kind==LobbySegment) return Accent?Hex(0xfff0c4):Hex(0xa8946c);
		if (Kind==LobbyGhost || Kind==LobbyGhostSmall) return Action.StartsWith(TEXT("remove"))?Hex(0xe7a08a):Hex(0xc9b58a);
		return Accent?Hex(0xfff0c0):Hex(0xf3c76a);
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
		const FSlateNoResource None;
		auto Dim=[](FSlateBrush Brush, float Amount){ Brush.TintColor=FLinearColor(Amount,Amount,Amount,1); return Brush; };
		// Mockup "disabled": saturate(.15) brightness(.6); "ghost": saturate(.35) brightness(.72).
		auto Grey=[](FSlateBrush Brush, float Bright, float Sat)
		{
			const FLinearColor Warm(.62f,.42f,.36f);
			Brush.TintColor=FLinearColor(FMath::Lerp(1.f,Warm.R,1-Sat)*Bright,FMath::Lerp(1.f,Warm.G,1-Sat)*Bright,FMath::Lerp(1.f,Warm.B,1-Sat)*Bright,1);
			return Brush;
		};
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
				Style.SetNormal(Up||Down?FSlateBrush(FSlateRoundedBoxBrush(Tile,2.f,Gold,1.f)):FSlateBrush(None));
				Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.08f),3.f)).SetPressed(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.2f),3.f)).SetDisabled(None);
			}
			Padding=FMargin(0);
		}
		else if (Kind==LobbyNav)
		{
			Style.SetNormal(None).SetHovered(None).SetPressed(None).SetDisabled(None);
			Padding=FMargin(0);
		}
		else if (Kind==LobbyScrim)
		{
			const FSlateBrush Shade=FSlateRoundedBoxBrush(FLinearColor(0,0,0,.38f),0.f);
			Style.SetNormal(Shade).SetHovered(Shade).SetPressed(Shade).SetDisabled(Shade);
			Padding=FMargin(0);
		}
		else if (Kind==LobbySegment)
		{
			Style.SetNormal(Accent?FSlateBrush(FSlateRoundedBoxBrush(Hex(0x7f1a10),0.f,Hex(0xffbe5a,.45f),1.f)):FSlateBrush(None));
			Style.SetHovered(Accent?FSlateBrush(FSlateRoundedBoxBrush(Hex(0x93200f),0.f,Hex(0xffbe5a,.6f),1.f)):FSlateBrush(FSlateRoundedBoxBrush(FLinearColor(1,.85f,.5f,.07f),0.f)));
			Style.SetPressed(FSlateRoundedBoxBrush(Hex(0x5e0c07),0.f,Hex(0xffbe5a,.45f),1.f)).SetDisabled(None);
			Padding=FMargin(10,4,10,5);
		}
		else if (Kind==LobbySwatch)
		{
			Style.SetNormal(Accent?FSlateBrush(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.35f),4.f,Hex(0xffd77a),2.f)):FSlateBrush(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.35f),4.f,Hex(0x5f4a22),1.f)));
			Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1,.85f,.5f,.08f),4.f,Accent?Hex(0xffd77a):Hex(0xc99a46),Accent?2.f:1.f));
			Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.5f),4.f,Hex(0xffd77a),2.f)).SetDisabled(None);
			Padding=FMargin(8,6);
		}
		else if (Kind==LobbyRow)
		{
			// Quiet list row; the content draws its own left accent and inset.
			Padding=FMargin(0,3,10,3);
			Style=ACELobbyArt::QuietRowStyle(Accent,Padding);
		}
		else if (Kind==LobbyEye)
		{
			Style.SetNormal(FSlateRoundedBoxBrush(Hex(0x2a0704),14.f,Hex(0x6d5426),1.f));
			Style.SetHovered(FSlateRoundedBoxBrush(Hex(0x4a0c07),14.f,Hex(0xc9963c),1.f));
			Style.SetPressed(FSlateRoundedBoxBrush(Hex(0x1a0503),14.f,Hex(0xc9963c),1.f)).SetDisabled(None);
			Padding=FMargin(5);
		}
		else if (Art)
		{
			if (Kind==LobbyRing)
			{
				const FSlateBrush* Hot=ACELobbyArt::TintedRingHot(GLobbyTint);
				FSlateBrush Off=Art->Ring[0]; Off.TintColor=FLinearColor(.62f,.55f,.5f,1); // saturate(.2) brightness(.6)
				Style.SetNormal(GLobbyForceHover?(Hot?*Hot:Art->Ring[1]):Art->Ring[0]).SetHovered(Hot?*Hot:Art->Ring[1]).SetPressed(Art->Ring[1]).SetDisabled(Off);
				Padding=FMargin(0);
			}
			else if (Kind==LobbyRow)
			{
				Style.SetNormal(Art->Row[Accent?2:0]).SetHovered(Art->Row[Accent?2:1]).SetPressed(Art->Row[2]).SetDisabled(Dim(Art->Row[0],.6f));
				Padding=FMargin(12,4);
			}
			else if (Kind==LobbyPlaque)
			{
				Style.SetNormal(Art->Plaque[Accent?1:0]).SetHovered(Art->Plaque[1]).SetPressed(Art->Plaque[1]).SetDisabled(Dim(Art->Plaque[0],.5f));
				Padding=FMargin(14,6,14,6);
			}
			else if (Kind==LobbyPrimary)
			{
				Style.SetNormal(Art->Primary[0]).SetHovered(Art->Primary[1]).SetPressed(Dim(Art->Primary[1],.82f)).SetDisabled(Grey(Art->Primary[0],.6f,.15f));
				Padding=FMargin(30,12,34,16);
			}
			else
			{
				const bool bSmall=Kind==LobbySmall || Kind==LobbyGhostSmall;
				const FSlateBrush* Plate=bSmall?Art->SmallButton:Art->Button;
				if (Kind==LobbyGhost || Kind==LobbyGhostSmall)
					Style.SetNormal(Grey(Plate[0],.72f,.35f)).SetHovered(Grey(Plate[1],.85f,.45f)).SetPressed(Grey(Plate[2],.7f,.4f)).SetDisabled(Grey(Plate[0],.6f,.15f));
				else
					Style.SetNormal(Plate[Accent?2:0]).SetHovered(Plate[Accent?2:1]).SetPressed(Dim(Plate[2],.82f)).SetDisabled(Grey(Plate[0],.6f,.15f));
				// The plate art carries a drop shadow on its right and bottom edges.
				Padding=bSmall?FMargin(13,5,15,8):FMargin(22,12,26,16);
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
			Padding=Kind==LobbyRing?FMargin(0):Kind==LobbySmall||Kind==LobbyGhostSmall?FMargin(10,4):Kind==LobbyRow?FMargin(12,4):Kind==LobbyPlaque?FMargin(14,6):FMargin(18,9);
		}
		Style.SetNormalPadding(Padding);
		const bool bShift=Kind==LobbyRed||Kind==LobbySmall||Kind==LobbyPrimary||Kind==LobbyGhost||Kind==LobbyGhostSmall;
		Style.SetPressedPadding(bShift?FMargin(Padding.Left,Padding.Top+1,Padding.Right,Padding.Bottom-1):Padding);
		Button->SetStyle(Style);
		if (auto* Size=Cast<USizeBox>(Button->GetContent()))
			if (auto* Caption=Cast<UTextBlock>(Size->GetContent()))
				Caption->SetColorAndOpacity(CaptionColor(Kind,Accent,Action?Action->Action:FString()));
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
	/** Short "when" for account rows, only from a real launch time: Today, Yesterday, 3d, 2w, Sep 30. */
	FString ShortWhen(int64 LastUsed)
	{
		if (LastUsed<=0) return FString();
		const FDateTime Local=FDateTime::FromUnixTimestamp(LastUsed)+(FDateTime::Now()-FDateTime::UtcNow());
		const int32 Days=(FDateTime::Now().GetDate()-Local.GetDate()).GetDays();
		if (Days<=0) return TEXT("Today");
		if (Days==1) return TEXT("Yesterday");
		if (Days<7) return FString::Printf(TEXT("%dd"),Days);
		if (Days<35) return FString::Printf(TEXT("%dw"),Days/7);
		static const TCHAR* Months[]={TEXT("Jan"),TEXT("Feb"),TEXT("Mar"),TEXT("Apr"),TEXT("May"),TEXT("Jun"),TEXT("Jul"),TEXT("Aug"),TEXT("Sep"),TEXT("Oct"),TEXT("Nov"),TEXT("Dec")};
		if (Local.GetYear()==FDateTime::Now().GetYear()) return FString::Printf(TEXT("%s %d"),Months[Local.GetMonth()-1],Local.GetDay());
		return FString::Printf(TEXT("%s %d"),Months[Local.GetMonth()-1],Local.GetYear());
	}
	/** Engine-font small caps with tracking (the mockups' Cinzel labels). */
	FSlateFontInfo CapsFont(float Px, float TrackingPx, const TCHAR* Weight = TEXT("Bold"))
	{
		FSlateFontInfo F=FCoreStyle::GetDefaultFontStyle(Weight,FMath::Max(6,FMath::RoundToInt(Px*.75f)));
		F.LetterSpacing=FMath::RoundToInt(TrackingPx/Px*1000.f);
		return F;
	}
	FSlateFontInfo BodyFont(float Px, const TCHAR* Weight = TEXT("Regular"))
	{
		return FCoreStyle::GetDefaultFontStyle(Weight,FMath::Max(6,FMath::RoundToInt(Px*.75f)));
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

UWidget* UACELoginWidget::Frame(UWidget* Content, const FMargin& Padding, bool bHairline)
{
	auto* Layers=WidgetTree->ConstructWidget<UOverlay>();
	auto* Host=WidgetTree->ConstructWidget<UNativeWidgetHost>(); Host->SetVisibility(ESlateVisibility::HitTestInvisible);
	TWeakObjectPtr<UACELoginWidget> WeakThis(this);
	TSharedRef<SACELobbyFrame> Painted=SNew(SACELobbyFrame).Hairline(bHairline)
		.Fill_Lambda([WeakThis](){ return ACELobbyArt::Tint(WeakThis.IsValid()?WeakThis->PageTint:0).Pane; })
		.FillTop_Lambda([WeakThis](){ return ACELobbyArt::Tint(WeakThis.IsValid()?WeakThis->PageTint:0).Pane2; });
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
	const uint8 K=B->LobbyStyle;
	UTextBlock* T=nullptr;
	if (K==LobbySegment || K==LobbyRow)
	{
		T=WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(Title)); T->SetFont(K==LobbyRow?BodyFont(14.5f):CapsFont(11.5f,.6f));
		T->SetShadowOffset(FVector2D(1,1)); T->SetShadowColorAndOpacity(FLinearColor(0,0,0,.9f)); T->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		const bool bSmall=K==LobbySmall||K==LobbyGhostSmall;
		T = Label(Title, 16, false, K==LobbyRow?1.1f:1.f); T->SetAutoWrapText(false); T->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		// Character select draws its plates in sansfont20 and its slot rows in sansfont16; the
		// Codex Hall plates are a step smaller so the forms breathe.
		if (auto* Retail=Cast<UACERetailTextBlock>(T))
		{
			Retail->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),bSmall?10:12));
			Retail->SetFontOverride(ACEUIFontStyles::FontIdForStyleName(bSmall?TEXT("sansfont14"):K==LobbyRow?TEXT("sansfont16"):TEXT("sansfont18")));
			Retail->SetRetailElement(UiResources(this),nullptr,Retail->GetBitmapScale(),0,false);
			if (K!=LobbyRow) { Retail->SetShadowOffset(FVector2D(1,1)); Retail->SetShadowColorAndOpacity(FLinearColor(0,0,0,.9f)); }
		}
	}
	T->SetJustification(K==LobbyRow?ETextJustify::Left:ETextJustify::Center);
	if (K==LobbyRow) T->SetClipping(EWidgetClipping::ClipToBounds);
	auto* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetMinDesiredHeight(K==LobbySegment?0:K==LobbySmall||K==LobbyGhostSmall?18:K==LobbyPrimary?28:26);
	Size->SetContent(T); Cast<USizeBoxSlot>(T->Slot)->SetVerticalAlignment(VAlign_Center);
	B->SetContent(Size);
	ButtonStyle(B, bAccent);
	return B;
}

UEditableTextBox* UACELoginWidget::Field(UVerticalBox* Parent, const FString& Title, const FName Name, bool bSecret)
{
	// Codex Hall field: tracked gold caps label (with an italic "optional"), then a 44px dark inset input.
	FString Main=Title, Note;
	if (Title.EndsWith(TEXT(" (optional)"))) { Main=Title.LeftChop(11); Note=TEXT("optional"); }
	auto* LabelRow=WidgetTree->ConstructWidget<UHorizontalBox>();
	auto* Cap=WidgetTree->ConstructWidget<UTextBlock>(); Cap->SetText(FText::FromString(Main.ToUpper()));
	Cap->SetFont(CapsFont(11.f,1.8f)); Cap->SetColorAndOpacity(Hex(0xc99a46));
	Cap->SetShadowOffset(FVector2D(1,1)); Cap->SetShadowColorAndOpacity(FLinearColor(0,0,0,.85f));
	LabelRow->AddChildToHorizontalBox(Cap)->SetVerticalAlignment(VAlign_Bottom);
	if (!Note.IsEmpty())
	{
		auto* Hint=WidgetTree->ConstructWidget<UTextBlock>(); Hint->SetText(FText::FromString(Note));
		Hint->SetFont(BodyFont(12.f,TEXT("Italic"))); Hint->SetColorAndOpacity(Hex(0x8a7a5c));
		LabelRow->AddChildToHorizontalBox(Hint)->SetPadding(FMargin(6,0,0,0));
	}
	AddLine(Parent, LabelRow, 7);
	auto* Box = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), Name);
	FEditableTextBoxStyle Style = Box->GetWidgetStyle();
	Style.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Hex(0x080c15,.96f), 2.f, Hex(0x5f4a22), 1.f));
	Style.SetBackgroundImageHovered(FSlateRoundedBoxBrush(Hex(0x0a0f19,.96f), 2.f, Hex(0x8a6a2e), 1.f));
	Style.SetBackgroundImageFocused(FSlateRoundedBoxBrush(Hex(0x0b111c,.98f), 2.f, Hex(0xe3a740), 1.5f));
	Style.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(Hex(0x080c15,.7f), 2.f, Hex(0x4a3a1c), 1.f));
	const FLinearColor Text=Hex(0xf4e8c8);
	Style.SetForegroundColor(Text); Style.SetPadding(FMargin(14,11));
	Style.SetTextStyle(FTextBlockStyle().SetFont(BodyFont(17.5f)).SetColorAndOpacity(Text)
		.SetSelectedBackgroundColor(Hex(0x8a5a14,.8f)).SetHighlightColor(Hex(0xf2a933)));
	Box->SetWidgetStyle(Style); Box->SetIsPassword(bSecret); Box->SetClearKeyboardFocusOnCommit(true);
	Box->SetSelectAllTextWhenFocused(true);
	AddLine(Parent, Box, 14);
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
	auto FullWindow=[&](UWidget* W){ auto* S=Canvas->AddChildToCanvas(W); S->SetAnchors(FAnchors(0,0,1,1)); S->SetOffsets(FMargin(0)); return S; };
	auto Fill=FSlateChildSize(ESlateSizeRule::Fill);
	TWeakObjectPtr<UACELoginWidget> WeakThis(this);
	auto TintOf=[WeakThis](){ return ACELobbyArt::Tint(WeakThis.IsValid()?WeakThis->PageTint:0); };

	// Retail character-select backdrop (0x06007075), logo painted out at runtime and tinted per
	// server, then the generated vignette and a tint wash low on the left. LayoutLobby sizes them.
	LobbyBackgroundSide=WidgetTree->ConstructWidget<UImage>(); LobbyBackgroundSide->SetVisibility(ESlateVisibility::Collapsed);
	LobbyBackgroundSide->SetRenderOpacity(0); Place(LobbyBackgroundSide);
	LobbyBackground=WidgetTree->ConstructWidget<UImage>(); LobbyBackground->SetVisibility(ESlateVisibility::HitTestInvisible);
	LobbyBackground->SetRenderOpacity(0); Place(LobbyBackground);
	LobbyVignette=WidgetTree->ConstructWidget<UImage>(); LobbyVignette->SetVisibility(ESlateVisibility::HitTestInvisible);
	LobbyVignette->SetRenderOpacity(0); FullWindow(LobbyVignette);
	LobbyWash=WidgetTree->ConstructWidget<UImage>(); LobbyWash->SetVisibility(ESlateVisibility::HitTestInvisible);
	LobbyWash->SetRenderOpacity(0); Place(LobbyWash)->SetAlignment(FVector2D(.5f,.5f));

	// The 1600x1000 stage every mockup coordinate refers to, centred in the window.
	DashboardSize=WidgetTree->ConstructWidget<USizeBox>(); DashboardSize->SetWidthOverride(DesignW); DashboardSize->SetHeightOverride(DesignH);
	Place(DashboardSize)->SetSize(FVector2D(DesignW,DesignH));
	auto* Stage=WidgetTree->ConstructWidget<UCanvasPanel>(); DashboardSize->SetContent(Stage);
	auto At=[&](UWidget* W, float X, float Y, float Wd=-1.f, float Ht=-1.f) -> UCanvasPanelSlot*
	{
		auto* S=Stage->AddChildToCanvas(W); S->SetAnchors(FAnchors(0,0)); S->SetPosition(FVector2D(X,Y));
		if (Wd>=0 && Ht>=0) S->SetSize(FVector2D(Wd,Ht)); else S->SetAutoSize(true);
		return S;
	};

	// ---- small builders ----
	auto HostOf=[&](TSharedRef<SWidget> Slate, float Wd=0.f, float Ht=0.f) -> UWidget*
	{
		auto* N=WidgetTree->ConstructWidget<UNativeWidgetHost>(); N->SetVisibility(ESlateVisibility::HitTestInvisible); N->SetContent(Slate);
		if (Wd<=0 && Ht<=0) return N;
		auto* Box=WidgetTree->ConstructWidget<USizeBox>(); if (Wd>0) Box->SetWidthOverride(Wd); if (Ht>0) Box->SetHeightOverride(Ht);
		Box->SetContent(N); return Box;
	};
	auto Glyph=[&](TAttribute<SACELobbyGlyph::EKind> Kind, float Size, TAttribute<FLinearColor> Color, TAttribute<bool> Filled=false) -> UWidget*
	{
		return HostOf(SNew(SACELobbyGlyph).Kind(Kind).Size(Size).Color(Color).Filled(Filled),Size,Size);
	};
	auto Decor=[&](SACELobbyDecor::EKind Kind, TAttribute<FLinearColor> Color, TAttribute<int32> State, float Wd, float Ht) -> UWidget*
	{
		return HostOf(SNew(SACELobbyDecor).Kind(Kind).Color(Color).State(State),Wd,Ht);
	};
	auto Rule=[&](bool bCentered=false){ return Decor(bCentered?SACELobbyDecor::EKind::CenteredRule:SACELobbyDecor::EKind::Rule,FLinearColor::White,0,0.f,1.f); };
	auto Retail=[&](const FString& Text, const TCHAR* Font, float Scale, const FLinearColor& Color) -> UACERetailTextBlock*
	{
		auto* T=Cast<UACERetailTextBlock>(Label(Text,16,false,Scale));
		const FString Name(Font); const bool bFancy=Name.StartsWith(TEXT("fancy"));
		const int32 Px=FCString::Atoi(*Name.RightChop(bFancy?9:8));
		T->SetFont(FCoreStyle::GetDefaultFontStyle(bFancy?TEXT("Bold"):TEXT("Regular"),FMath::Max(7,FMath::RoundToInt(Px*Scale*.7f))));
		T->SetFontOverride(ACEUIFontStyles::FontIdForStyleName(Name));
		T->SetRetailElement(UiResources(this),nullptr,FVector2D(Scale),0,false);
		T->SetColorAndOpacity(Color); T->SetAutoWrapText(false);
		T->SetShadowOffset(FVector2D(1,1)); T->SetShadowColorAndOpacity(FLinearColor(0,0,0,.7f));
		return T;
	};
	auto Text=[&](const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color, bool bWrap=false) -> UTextBlock*
	{
		auto* T=WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(String)); T->SetFont(Font);
		T->SetColorAndOpacity(Color); T->SetAutoWrapText(bWrap); T->SetVisibility(ESlateVisibility::HitTestInvisible);
		T->SetShadowOffset(FVector2D(1,1)); T->SetShadowColorAndOpacity(FLinearColor(0,0,0,.85f));
		return T;
	};
	auto Caps=[&](const FString& String, float Px=11.5f, float Track=2.4f, FLinearColor Color=FLinearColor(FColor(0xd8,0xa6,0x4a))){ return Text(String.ToUpper(),CapsFont(Px,Track),Color); };
	auto HBox=[&](){ return WidgetTree->ConstructWidget<UHorizontalBox>(); };
	auto VBox=[&](){ return WidgetTree->ConstructWidget<UVerticalBox>(); };
	auto Gap=[&](float Wd, float Ht){ auto* S=WidgetTree->ConstructWidget<USpacer>(); S->SetSize(FVector2D(Wd,Ht)); return S; };
	auto AddH=[&](UHorizontalBox* Row, UWidget* W, FMargin Pad=FMargin(0), bool bFill=false, EVerticalAlignment V=VAlign_Center)
	{
		auto* S=Row->AddChildToHorizontalBox(W); S->SetPadding(Pad); S->SetVerticalAlignment(V); if (bFill) S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); return S;
	};
	/** Caption + rule section heading ("FAVORITES ───"). */
	auto Section=[&](const FString& Title, UWidget* Lead=nullptr, UWidget* Trail=nullptr)
	{
		auto* Row=HBox();
		if (Lead) AddH(Row,Lead,FMargin(0,0,7,0));
		AddH(Row,Caps(Title));
		AddH(Row,Rule(),FMargin(12,1,Trail?12:0,0),true);
		if (Trail) AddH(Row,Trail);
		return Row;
	};
	/** Retail window 0x06004D26 at any size: Uncial title in the header, content below. */
	auto Window=[&](const FString& Title, UWidget* Content, UWidget* HeaderRight=nullptr, FMargin Pad=FMargin(30,22,26,22)) -> UOverlay*
	{
		auto* Layers=WidgetTree->ConstructWidget<UOverlay>();
		TSharedRef<SACELobbyWindow> Painted=SNew(SACELobbyWindow).Tint_Lambda([WeakThis](){ return WeakThis.IsValid()?WeakThis->PageTint:0; });
		LobbyFrames.Add(Painted);
		auto* PaintSlot=Layers->AddChildToOverlay(HostOf(Painted)); PaintSlot->SetHorizontalAlignment(HAlign_Fill); PaintSlot->SetVerticalAlignment(VAlign_Fill);
		auto* Head=HBox(); auto* HeadSize=WidgetTree->ConstructWidget<USizeBox>(); HeadSize->SetHeightOverride(FACELobbyArt::WindowHeaderH); HeadSize->SetContent(Head);
		auto* HeadSlot=Layers->AddChildToOverlay(HeadSize); HeadSlot->SetVerticalAlignment(VAlign_Top); HeadSlot->SetHorizontalAlignment(HAlign_Fill);
		auto* T=Retail(Title,TEXT("fancyfont36"),1.f,ACELobbyArt::Gold); T->SetClipping(EWidgetClipping::ClipToBounds);
		AddH(Head,T,FMargin(84,0,8,6),true);
		if (HeaderRight) AddH(Head,HeaderRight,FMargin(0,0,30,6));
		auto* BodySlot=Layers->AddChildToOverlay(Content);
		BodySlot->SetPadding(FMargin(Pad.Left,FACELobbyArt::WindowHeaderH+Pad.Top,Pad.Right,Pad.Bottom));
		BodySlot->SetHorizontalAlignment(HAlign_Fill); BodySlot->SetVerticalAlignment(VAlign_Fill);
		return Layers;
	};
	auto Buttons=[&](UPanelWidget* Parent, const TArray<TPair<FString,FString>>& Items)
	{
		auto* R=WidgetTree->ConstructWidget<UWrapBox>(); R->SetInnerSlotPadding(FVector2D(10,8));
		if (auto* V=Cast<UVerticalBox>(Parent)) AddLine(V,R,14); else Parent->AddChild(R);
		for (const auto& Item:Items) R->AddChildToWrapBox(ActionButton(Item.Key,Item.Value));
		return R;
	};

	auto Slim=[&](UScrollBox* Scroll){ Scroll->SetWidgetBarStyle(ACELobbyArt::QuietScrollBar(6.f)); Scroll->SetScrollbarThickness(FVector2D(6,6)); Scroll->SetScrollbarPadding(FMargin(8,0,0,0)); return Scroll; };
	/** Side panel for the wide pages: quiet hairline frame, caption + rule, then content. */
	auto SidePanel=[&](const FString& Caption, UVerticalBox*& Content, float Width) -> UWidget*
	{
		Content=VBox();
		AddLine(Content,Section(Caption),14);
		auto* Size=WidgetTree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(Width);
		Size->SetContent(Frame(Content,FMargin(24,20,24,22),true));
		return Size;
	};

	// ---- left column: crest, title, vertical tabs, World plaque, Quit and version ----
	LobbyCrest=WidgetTree->ConstructWidget<UImage>(); LobbyCrest->SetVisibility(ESlateVisibility::HitTestInvisible); LobbyCrest->SetRenderOpacity(0);
	At(LobbyCrest,20,10,380,229);
	LobbyBrand=Heading(TEXT("Asheron's Call"),40); LobbyBrand->SetVisibility(ESlateVisibility::HitTestInvisible);
	At(LobbyBrand,210,100)->SetAlignment(FVector2D(.5f,.5f));
	LobbySubtitle=WidgetTree->ConstructWidget<UTextBlock>();
	LobbySubtitle->SetText(FText::FromString(PLATFORM_ANDROID?TEXT("VR"):TEXT("UNREAL")));
	LobbySubtitle->SetFont(CapsFont(30.f,12.f,TEXT("Regular"))); LobbySubtitle->SetColorAndOpacity(Hex(0xd9dde3));
	LobbySubtitle->SetShadowOffset(FVector2D(0,2)); LobbySubtitle->SetShadowColorAndOpacity(FLinearColor(0,0,0,.9f));
	LobbySubtitle->SetVisibility(ESlateVisibility::HitTestInvisible);
	At(LobbySubtitle,216,232)->SetAlignment(FVector2D(.5f,0));
	At(Rule(true),56,282,308,1);

	auto* Nav=VBox(); At(Nav,40,318,330,5*50+4*5);
	const TPair<FString,FString> Tabs[]={{TEXT("Accounts"),TEXT("accounts")},{TEXT("Servers"),TEXT("browser")},{TEXT("Game Files"),TEXT("files")},{TEXT("Updates"),TEXT("updates")},{TEXT("Plugins"),TEXT("plugins")}};
	for (int32 I=0;I<UE_ARRAY_COUNT(Tabs);++I)
	{
		auto* B=ActionButton(TEXT(""),Tabs[I].Value); B->LobbyStyle=LobbyNav;
		TWeakObjectPtr<UACELoginActionButton> WeakB(B);
		auto NavState=TAttribute<int32>::CreateLambda([WeakB](){ return !WeakB.IsValid()?0:WeakB->bLobbyAccent?2:WeakB->IsHovered()?1:0; });
		auto* Item=WidgetTree->ConstructWidget<UOverlay>();
		auto* Back=Item->AddChildToOverlay(Decor(SACELobbyDecor::EKind::NavItem,FLinearColor::White,NavState,0.f,0.f));
		Back->SetHorizontalAlignment(HAlign_Fill); Back->SetVerticalAlignment(VAlign_Fill);
		auto* Line=HBox(); auto* LineSlot=Item->AddChildToOverlay(Line); LineSlot->SetVerticalAlignment(VAlign_Center); LineSlot->SetHorizontalAlignment(HAlign_Fill); LineSlot->SetPadding(FMargin(47,0,0,0));
		AddH(Line,HostOf(SNew(STextBlock).Text(FText::FromString(Tabs[I].Key.ToUpper())).Font(CapsFont(16.f,3.f,TEXT("Bold")))
			.ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor(0,0,0,.9f))
			.ColorAndOpacity_Lambda([WeakB](){ const int32 S=!WeakB.IsValid()?0:WeakB->bLobbyAccent?2:WeakB->IsHovered()?1:0;
				return FSlateColor(S==2?Hex(0xffe7a6):S==1?Hex(0xf0d79a):Hex(0xc4ab76)); })));
		if (Tabs[I].Value==TEXT("updates"))
		{
			UpdateNavButton=B;
			auto Ready=[WeakThis](){ return WeakThis.IsValid()&&WeakThis->bUpdateAvailable?EVisibility::HitTestInvisible:EVisibility::Collapsed; };
			AddH(Line,HostOf(SNew(SBox).Visibility_Lambda(Ready)[SNew(SACELobbyDecor).Kind(SACELobbyDecor::EKind::Badge)],8,8),FMargin(12,0,0,2));
			AddH(Line,HostOf(SNew(STextBlock).Visibility_Lambda(Ready).Text(FText::FromString(TEXT("Update ready"))).Font(BodyFont(12.f,TEXT("Italic")))
				.ColorAndOpacity(Hex(0xd9a548)).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor(0,0,0,.8f))),FMargin(0,0,14,0),true)
				->SetHorizontalAlignment(HAlign_Right);
		}
		auto* Height=WidgetTree->ConstructWidget<USizeBox>(); Height->SetHeightOverride(50); Height->SetContent(Item);
		Cast<USizeBoxSlot>(Item->Slot)->SetHorizontalAlignment(HAlign_Fill); Cast<USizeBoxSlot>(Item->Slot)->SetVerticalAlignment(VAlign_Fill);
		B->SetContent(Height); ButtonStyle(B,false);
		Cast<UButtonSlot>(Height->Slot)->SetPadding(FMargin(0)); Cast<UButtonSlot>(Height->Slot)->SetHorizontalAlignment(HAlign_Fill); Cast<UButtonSlot>(Height->Slot)->SetVerticalAlignment(VAlign_Fill);
		Nav->AddChildToVerticalBox(B);
		NavigationButtons.Add(B);
		if (I+1<UE_ARRAY_COUNT(Tabs))
		{
			auto* Sep=Rule(); Sep->SetRenderOpacity(.4f);
			Nav->AddChildToVerticalBox(Sep)->SetPadding(FMargin(22,2,0,2));
		}
	}

	// World plaque: the only place the selected server shows on the home page.
	At(Caps(TEXT("World"),11.f,2.4f),62,680);
	WorldPlaque=ActionButton(TEXT(""),TEXT("worldmenu"));
	WorldPlaque->SetToolTipText(FText::FromString(TEXT("Choose your world")));
	{
		TWeakObjectPtr<UACELoginActionButton> WeakP(WorldPlaque);
		auto Hot=[WeakThis,WeakP](){ return (WeakThis.IsValid()&&WeakThis->IsWorldMenuOpen()) || (WeakP.IsValid()&&WeakP->IsHovered()); };
		auto* Inner=WidgetTree->ConstructWidget<UOverlay>();
		auto* Wash=Inner->AddChildToOverlay(Decor(SACELobbyDecor::EKind::Glow,TAttribute<FLinearColor>::CreateLambda([TintOf,Hot](){ FLinearColor C=TintOf().Gem; C.A=Hot()?.42f:.26f; return C; }),0,190.f,0.f));
		Wash->SetHorizontalAlignment(HAlign_Left); Wash->SetVerticalAlignment(VAlign_Fill); Wash->SetPadding(FMargin(-30,-14,0,-14));
		auto* Line=HBox(); { auto* LineSlot=Inner->AddChildToOverlay(Line); LineSlot->SetVerticalAlignment(VAlign_Center); LineSlot->SetHorizontalAlignment(HAlign_Fill); LineSlot->SetPadding(FMargin(4,0,6,0)); }
		AddH(Line,Decor(SACELobbyDecor::EKind::Gem,TAttribute<FLinearColor>::CreateLambda([TintOf](){ return TintOf().Gem; }),
			TAttribute<int32>::CreateLambda([Hot](){ return Hot()?1:0; }),18.f,18.f),FMargin(0,0,11,0));
		auto* Names=VBox(); AddH(Line,Names,FMargin(0),true);
		ServerTitle=Retail(TEXT("Choose your world"),TEXT("fancyfont24"),1.2f,Hex(0xf6dfa0));
		ServerTitle->SetClipping(EWidgetClipping::ClipToBounds); ServerTitle->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Names->AddChildToVerticalBox(ServerTitle);
		ServerDetail=Text(TEXT("Add a server to get started."),BodyFont(12.f),Hex(0xa99878));
		ServerDetail->SetClipping(EWidgetClipping::ClipToBounds); ServerDetail->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Names->AddChildToVerticalBox(ServerDetail)->SetPadding(FMargin(1,0,0,0));
		AddH(Line,Glyph(TAttribute<SACELobbyGlyph::EKind>::CreateLambda([WeakThis](){ return WeakThis.IsValid()&&WeakThis->IsWorldMenuOpen()?SACELobbyGlyph::EKind::ChevronUp:SACELobbyGlyph::EKind::ChevronDown; }),
			11,Hex(0xd9a548)),FMargin(8,0,2,0));
		auto* Height=WidgetTree->ConstructWidget<USizeBox>(); Height->SetHeightOverride(52); Height->SetContent(Inner);
		Cast<USizeBoxSlot>(Inner->Slot)->SetHorizontalAlignment(HAlign_Fill);
		WorldPlaque->SetContent(Height); ButtonStyle(WorldPlaque,false);
		Cast<UButtonSlot>(Height->Slot)->SetHorizontalAlignment(HAlign_Fill);
	}
	At(WorldPlaque,52,697,318,68);

	// Quit and version at the foot of the column (FooterSize keeps the responsive test's name).
	FooterSize=WidgetTree->ConstructWidget<USizeBox>(); FooterSize->SetWidthOverride(298);
	{
		auto* Foot=HBox(); FooterSize->SetContent(Foot);
		AddH(Foot,ActionButton(TEXT("Quit"),TEXT("quit")));
		auto* Version=Text(FString::Printf(TEXT("Version %s"),ACEClientBuild::Version),BodyFont(12.f),Hex(0x8d826b));
		AddH(Foot,Version,FMargin(12,0,0,0),true)->SetHorizontalAlignment(HAlign_Right);
	}
	At(FooterSize,62,904,298,44);

	// ---- Accounts window: search, favourites, sort, the list, footer hint ----
	{
		auto* Body=VBox();
		auto* Search=WidgetTree->ConstructWidget<UOverlay>();
		auto* SearchSize=WidgetTree->ConstructWidget<USizeBox>(); SearchSize->SetHeightOverride(34); SearchSize->SetContent(Search);
		AddLine(Body,SearchSize,16);
		AccountSearchBox=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),TEXT("AccountSearch"));
		{
			FEditableTextBoxStyle Style=AccountSearchBox->GetWidgetStyle();
			const FSlateRoundedBoxBrush Idle(FLinearColor(.004f,.008f,.02f,.85f),4.f,FLinearColor(.45f,.36f,.2f,1),1.f), Focus(FLinearColor(.004f,.008f,.02f,.9f),4.f,ACELobbyArt::Gold,1.f);
			Style.SetBackgroundImageNormal(Idle).SetBackgroundImageHovered(Idle).SetBackgroundImageFocused(Focus).SetBackgroundImageReadOnly(Idle);
			Style.SetForegroundColor(Hex(0xf2e3bd)); Style.SetPadding(FMargin(40,7,40,7));
			Style.SetTextStyle(FTextBlockStyle().SetFont(BodyFont(14.f)).SetColorAndOpacity(Hex(0xf2e3bd)));
			AccountSearchBox->SetWidgetStyle(Style);
		}
		AccountSearchBox->SetHintText(FText::FromString(TEXT("Search accounts")));
		AccountSearchBox->OnTextChanged.AddDynamic(this,&UACELoginWidget::OnAccountSearchChanged);
		AccountSearchBox->OnTextCommitted.AddDynamic(this,&UACELoginWidget::OnAccountSearchCommitted);
		auto* SearchSlot=Search->AddChildToOverlay(AccountSearchBox); SearchSlot->SetHorizontalAlignment(HAlign_Fill); SearchSlot->SetVerticalAlignment(VAlign_Fill);
		auto* MagnifierSlot=Search->AddChildToOverlay(Glyph(SACELobbyGlyph::EKind::Magnifier,15,Hex(0xc9a052)));
		MagnifierSlot->SetHorizontalAlignment(HAlign_Left); MagnifierSlot->SetVerticalAlignment(VAlign_Center); MagnifierSlot->SetPadding(FMargin(16,0,0,0));
		// "/" focuses the search; the key hint shows while the field is idle.
		TWeakObjectPtr<UEditableTextBox> WeakSearch(AccountSearchBox);
		auto* KeyHint=HostOf(SNew(SBorder).Visibility_Lambda([WeakSearch](){ return WeakSearch.IsValid()&&WeakSearch->GetText().IsEmpty()&&!WeakSearch->HasKeyboardFocus()?EVisibility::HitTestInvisible:EVisibility::Collapsed; })
			.BorderImage(new FSlateRoundedBoxBrush(FLinearColor(0,0,0,.4f),4.f,Hex(0x5b4a2c),1.f)).Padding(FMargin(6,0,6,1))
			[SNew(STextBlock).Text(FText::FromString(TEXT("/"))).Font(BodyFont(11.f,TEXT("Bold"))).ColorAndOpacity(Hex(0x9c8a66))]);
		auto* KeySlot=Search->AddChildToOverlay(KeyHint); KeySlot->SetHorizontalAlignment(HAlign_Right); KeySlot->SetVerticalAlignment(VAlign_Center); KeySlot->SetPadding(FMargin(0,0,22,0));
		auto* Clear=ActionButton(TEXT(""),TEXT("clearsearch")); Clear->SetToolTipText(FText::FromString(TEXT("Clear search (Esc)")));
		Clear->SetContent(Glyph(SACELobbyGlyph::EKind::Cross,14,Hex(0xc9a052)));
		auto* ClearSlot=Search->AddChildToOverlay(Clear); ClearSlot->SetHorizontalAlignment(HAlign_Right); ClearSlot->SetVerticalAlignment(VAlign_Center); ClearSlot->SetPadding(FMargin(0,0,18,0));
		AccountSearchClear=Clear; Clear->SetVisibility(ESlateVisibility::Collapsed);

		auto* FavoriteRow=Section(TEXT("Favorites"),Glyph(SACELobbyGlyph::EKind::Star,11,Hex(0xf6c445),true));
		FavoriteHeader=FavoriteRow; AddLine(Body,FavoriteRow,9);
		FavoriteSize=WidgetTree->ConstructWidget<USizeBox>(); AddLine(Body,FavoriteSize,8);
		FavoriteList=WidgetTree->ConstructWidget<UACELobbyAccountListView>(UACELobbyAccountListView::StaticClass(),TEXT("FavoriteList"));
		FavoriteSize->SetContent(FavoriteList);
		auto* Segments=HBox();
		auto* SegFrame=WidgetTree->ConstructWidget<UBorder>(); SegFrame->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.45f),3.f,Hex(0x6d5426),1.f));
		SegFrame->SetPadding(FMargin(1)); SegFrame->SetContent(Segments);
		for (const auto& Sort : TArray<TPair<FString,FString>>{{TEXT("A\u2013Z"),TEXT("sortaz")},{TEXT("Recent"),TEXT("sortrecent")},{TEXT("Custom"),TEXT("sortcustom")}})
		{
			auto* B=ActionButton(Sort.Key,Sort.Value); SortButtons.Add(B);
			if (Segments->GetChildrenCount()) AddH(Segments,HostOf(SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Hex(0x4a3a1c)),1,0),FMargin(0),false,VAlign_Fill);
			AddH(Segments,B,FMargin(0),false,VAlign_Fill);
		}
		SortButtons[2]->SetToolTipText(FText::FromString(TEXT("Your own order. Drag an account by its grip to change it.")));
		AddLine(Body,Section(TEXT("All accounts"),nullptr,SegFrame),9);
		auto* ListArea=WidgetTree->ConstructWidget<UOverlay>();
		auto* ListAreaSlot=Body->AddChildToVerticalBox(ListArea); ListAreaSlot->SetSize(Fill);
		AccountList=WidgetTree->ConstructWidget<UACELobbyAccountListView>(UACELobbyAccountListView::StaticClass(),TEXT("AccountList"));
		auto* ListSlot=ListArea->AddChildToOverlay(AccountList); ListSlot->SetHorizontalAlignment(HAlign_Fill); ListSlot->SetVerticalAlignment(VAlign_Fill);
		AccountsEmpty=Text(TEXT(""),BodyFont(14.f),Hex(0xb9a77f),true); AccountsEmpty->SetVisibility(ESlateVisibility::Collapsed);
		auto* EmptySlot=ListArea->AddChildToOverlay(AccountsEmpty); EmptySlot->SetHorizontalAlignment(HAlign_Fill); EmptySlot->SetVerticalAlignment(VAlign_Top); EmptySlot->SetPadding(FMargin(4,6));
		for (int32 I=0;I<2;++I)
		{
			auto* Arrow=ActionButton(TEXT(""),I?TEXT("scrollaccountsdown"):TEXT("scrollaccountsup"));
			auto* ArrowSize=WidgetTree->ConstructWidget<USizeBox>(); ArrowSize->SetWidthOverride(16); ArrowSize->SetHeightOverride(16); Arrow->SetContent(ArrowSize);
			auto* ArrowSlot=ListArea->AddChildToOverlay(Arrow); ArrowSlot->SetHorizontalAlignment(HAlign_Right); ArrowSlot->SetVerticalAlignment(I?VAlign_Bottom:VAlign_Top);
			Arrow->SetVisibility(ESlateVisibility::Collapsed);
			(I?AccountScrollDown:AccountScrollUp)=Arrow;
		}
		auto* Foot=HBox(); Body->AddChildToVerticalBox(Foot)->SetPadding(FMargin(0,10,0,0));
		auto* Hint=HBox(); AddH(Foot,Hint,FMargin(0),true);
		const FLinearColor HintColor=Hex(0x8f8366);
		AddH(Hint,Text(TEXT("Drag "),BodyFont(12.5f),HintColor));
		AddH(Hint,Glyph(SACELobbyGlyph::EKind::Grip,11,Hex(0xb0905a)));
		AddH(Hint,Text(TEXT(" to reorder  \u00B7  "),BodyFont(12.5f),HintColor));
		AddH(Hint,Glyph(SACELobbyGlyph::EKind::Star,10,Hex(0xb0905a),true));
		AddH(Hint,Text(TEXT(" to pin"),BodyFont(12.5f),HintColor));
		auto* Add=ActionButton(TEXT("+ Add"),TEXT("newaccount")); Add->LobbyStyle=LobbySmall; ButtonStyle(Add,false);
		Add->SetToolTipText(FText::FromString(TEXT("Add another login")));
		AddH(Foot,Add);
		// Count pill in the title bar.
		AccountsHeading=Text(TEXT("0"),BodyFont(14.f,TEXT("Bold")),Hex(0x1a1004)); AccountsHeading->SetShadowOffset(FVector2D(0,0));
		auto* Pill=WidgetTree->ConstructWidget<UBorder>(); Pill->SetBrush(FSlateRoundedBoxBrush(Hex(0xe6b04a),11.f,Hex(0x6b4a12),1.f));
		Pill->SetPadding(FMargin(9,1,9,2)); Pill->SetContent(AccountsHeading); Pill->SetVisibility(ESlateVisibility::HitTestInvisible);
		AccountsWindow=Window(TEXT("Accounts"),Body,Pill,FMargin(30,22,30,20));
		At(AccountsWindow,410,36,440,928);
	}

	// ---- content pages. Home (Accounts) keeps Manage Account + Launch right of the Accounts
	// window; every other tab uses the whole width right of the nav column. ----
	Pages=WidgetTree->ConstructWidget<UWidgetSwitcher>(); At(Pages,410,36,1146,880);
	Pages->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	auto HugTop=[](UWidget* W){ if (auto* S=Cast<UWidgetSwitcherSlot>(W->Slot)) S->SetVerticalAlignment(VAlign_Top); };
	auto PageWindow=[&](const FString& Title, UVerticalBox*& Content, bool bScroll) -> UWidget*
	{
		Content=VBox(); UWidget* Inner=Content;
		if (bScroll)
		{
			// Tall forms scroll inside the window instead of pushing off small screens.
			auto* Scroll=Slim(WidgetTree->ConstructWidget<UScrollBox>()); Scroll->AddChild(Content); Inner=Scroll;
		}
		return Window(Title,Inner);
	};

	// Home: the selected account's form, and the Launch ring.
	{
		auto* Home=WidgetTree->ConstructWidget<UCanvasPanel>(); Pages->AddChild(Home);
		Home->SetVisibility(ESlateVisibility::SelfHitTestInvisible); // The Accounts window shows through on the left.
		constexpr float HomeX=480.f;
		auto* Form=VBox();
		auto* Who=HBox(); AddLine(Form,Who,12);
		SelectedAccountName=Retail(TEXT(""),TEXT("fancyfont48"),1.f,Hex(0xffe2a0));
		SelectedAccountName->SetClipping(EWidgetClipping::ClipToBounds); SelectedAccountName->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		AddH(Who,SelectedAccountName,FMargin(0),false,VAlign_Bottom);
		LastLaunchedText=Text(TEXT(""),BodyFont(14.f),Hex(0xb9a77f));
		LastLaunchedText->SetClipping(EWidgetClipping::ClipToBounds); LastLaunchedText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		AddH(Who,LastLaunchedText,FMargin(14,0,0,7),true,VAlign_Bottom);
		AddLine(Form,Rule(),18);
		auto* Credentials=HBox(); AddLine(Form,Credentials,8);
		auto Column=[&](bool bLast){ auto* C=VBox(); AddH(Credentials,C,FMargin(0,0,bLast?0:18,0),true,VAlign_Top); return C; };
		auto* AliasColumn=Column(false); auto* UserColumn=Column(false); auto* PasswordColumn=Column(true);
		AccountAliasBox=Field(AliasColumn,TEXT("Alias (optional)"),TEXT("AccountAliasBox"));
		AccountBox=Field(UserColumn,TEXT("Username"),TEXT("AccountBox"));
		PasswordBox=Field(PasswordColumn,TEXT("Password"),TEXT("PasswordBox"),true);
		{
			// Round red eye inside the password field.
			const int32 Index=PasswordColumn->GetChildIndex(PasswordBox); PasswordBox->RemoveFromParent();
			auto Style=PasswordBox->GetWidgetStyle(); Style.SetPadding(FMargin(14,11,44,11)); PasswordBox->SetWidgetStyle(Style);
			auto* PasswordLayers=WidgetTree->ConstructWidget<UOverlay>();
			PasswordColumn->InsertChildAt(Index,PasswordLayers);
			Cast<UVerticalBoxSlot>(PasswordLayers->Slot)->SetPadding(FMargin(0,0,0,14));
			auto* BoxSlot=PasswordLayers->AddChildToOverlay(PasswordBox); BoxSlot->SetHorizontalAlignment(HAlign_Fill); BoxSlot->SetVerticalAlignment(VAlign_Fill);
			PasswordVisibilityButton=ActionButton(TEXT(""),TEXT("togglepassword"));
			auto* Eye=WidgetTree->ConstructWidget<UNativeWidgetHost>(); Eye->SetVisibility(ESlateVisibility::HitTestInvisible);
			Eye->SetContent(SNew(SPasswordVisibilityIcon).Revealed_Lambda([Box=TWeakObjectPtr<UEditableTextBox>(PasswordBox)](){return Box.IsValid()&&!Box->GetIsPassword();}));
			auto* EyeSize=WidgetTree->ConstructWidget<USizeBox>(); EyeSize->SetWidthOverride(18); EyeSize->SetHeightOverride(16); EyeSize->SetContent(Eye);
			PasswordVisibilityButton->SetContent(EyeSize);
			auto* EyeSlot=PasswordLayers->AddChildToOverlay(PasswordVisibilityButton); EyeSlot->SetHorizontalAlignment(HAlign_Right); EyeSlot->SetVerticalAlignment(VAlign_Center);
			EyeSlot->SetPadding(FMargin(0,0,8,0));
		}
		SetPasswordVisible(false);
		auto* Actions=HBox(); AddLine(Form,Actions,16);
		AddH(Actions,ActionButton(TEXT("Save Account"),TEXT("saveaccount")),FMargin(0,0,12,0));
		AddH(Actions,ActionButton(TEXT("New Account"),TEXT("newaccount")));
		AddH(Actions,Gap(1,1),FMargin(0),true);
		AddH(Actions,ActionButton(TEXT("Remove"),TEXT("removeaccount")));
		auto* Note=HBox(); AddLine(Form,Note,0);
		AddH(Note,Glyph(SACELobbyGlyph::EKind::Lock,12,Hex(0xb0905a)),FMargin(0,0,8,0));
		AddH(Note,Text(PLATFORM_LINUX ? TEXT("Passwords are kept for this session only. Enter them here after restarting.")
			: PLATFORM_MAC ? TEXT("Password stored in your macOS Keychain \u2014 never in plain text.") : TEXT("Accounts are saved securely on this device."),BodyFont(13.f),Hex(0x8f8366),true),FMargin(0),true);
		auto* Manage=Window(TEXT("Manage Account"),Form,nullptr,FMargin(30,20,30,26));
		auto* ManageSize=WidgetTree->ConstructWidget<USizeBox>(); ManageSize->SetWidthOverride(666); ManageSize->SetContent(Manage);
		auto* ManageSlot=Home->AddChildToCanvas(ManageSize); ManageSlot->SetAutoSize(true); ManageSlot->SetPosition(FVector2D(HomeX,0));

		// Launch: halo in the page tint, retail ring (tinted glow on hover), caption beneath.
		auto* Halo=Decor(SACELobbyDecor::EKind::Halo,TAttribute<FLinearColor>::CreateLambda([TintOf,WeakThis]()
		{
			FLinearColor C=TintOf().Gem; C.A=WeakThis.IsValid()&&WeakThis->LoginButton&&WeakThis->LoginButton->GetIsEnabled()?.5f:.2f; return C;
		}),0,336.f,336.f);
		auto* HaloSlot=Home->AddChildToCanvas(Halo); HaloSlot->SetPosition(FVector2D(HomeX+165,476)); HaloSlot->SetSize(FVector2D(336,336));
		LaunchSize=WidgetTree->ConstructWidget<USizeBox>(); LaunchSize->SetWidthOverride(290); LaunchSize->SetHeightOverride(290);
		auto* LaunchSlot=Home->AddChildToCanvas(LaunchSize); LaunchSlot->SetPosition(FVector2D(HomeX+188,499)); LaunchSlot->SetSize(FVector2D(290,290));
		LoginButton=ActionButton(TEXT("Launch"),TEXT("launch")); LaunchSize->SetContent(LoginButton);
		LoginButton->SetToolTipText(FText::FromString(TEXT("Launch the selected account (Enter)")));
		if (auto* CaptionSize=Cast<USizeBox>(LoginButton->GetContent())) if (auto* Caption=Cast<UACERetailTextBlock>(CaptionSize->GetContent()))
		{
			Caption->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),30));
			Caption->SetFontOverride(ACEUIFontStyles::FontIdForStyleName(TEXT("fancyfont48")));
			Caption->SetRetailElement(UiResources(this),nullptr,FVector2D(1.5f),0,false);
			Caption->SetShadowOffset(FVector2D(1,2)); Caption->SetShadowColorAndOpacity(FLinearColor(0,0,0,.95f));
		}
		LaunchCaption=Retail(TEXT(""),TEXT("sansfont14"),1.f,Hex(0xcdb98c));
		auto* CaptionSlot=Home->AddChildToCanvas(LaunchCaption); CaptionSlot->SetAutoSize(true); CaptionSlot->SetPosition(FVector2D(HomeX+333,805)); CaptionSlot->SetAlignment(FVector2D(.5f,0));
	}

	// Server editor: explicit save/cancel avoids sending credentials to an edited endpoint by accident.
	{
		UVerticalBox* Edit; auto* EditPage=PageWindow(TEXT("Server Details"),Edit,true); Pages->AddChild(EditPage); HugTop(EditPage);
		auto* Cols=HBox(); AddLine(Edit,Cols,4);
		auto* L=VBox(); AddH(Cols,L,FMargin(0,0,40,0),true,VAlign_Top);
		auto* R=VBox(); AddH(Cols,R,FMargin(0),true,VAlign_Top);
		AddLine(L,Section(TEXT("World")),14);
		NameBox=Field(L,TEXT("Server name"),TEXT("ServerName"));
		auto* Address=HBox(); AddLine(L,Address,0);
		auto* HostCol=VBox(); AddH(Address,HostCol,FMargin(0),true,VAlign_Top);
		HostBox=Field(HostCol,TEXT("Hostname or IP address"),TEXT("HostBox"));
		auto* PortWidth=WidgetTree->ConstructWidget<USizeBox>(); PortWidth->SetWidthOverride(130); AddH(Address,PortWidth,FMargin(18,0,0,0),false,VAlign_Top);
		auto* PortCol=VBox(); PortWidth->SetContent(PortCol); PortBox=Field(PortCol,TEXT("Port"),TEXT("PortBox"));
		AddLine(L,Caps(TEXT("Server type"),11.f,1.8f,Hex(0xc99a46)),7);
		auto* Types=HBox(); AddLine(L,Types,4);
		ACETypeButton=ActionButton(TEXT("ACE"),TEXT("ace")); GDLETypeButton=ActionButton(TEXT("GDLE"),TEXT("gdle"));
		AddH(Types,ACETypeButton,FMargin(0,0,10,0)); AddH(Types,GDLETypeButton);
		AddLine(R,Section(TEXT("Community")),14);
		DescriptionBox=Field(R,TEXT("Description (optional)"),TEXT("ServerDescription"));
		WebsiteBox=Field(R,TEXT("Website (optional)"),TEXT("ServerWebsite")); DiscordBox=Field(R,TEXT("Discord invite (optional)"),TEXT("ServerDiscord"));
		AddLine(Edit,Section(TEXT("Page colour")),12);
		auto* Swatches=WidgetTree->ConstructWidget<UWrapBox>(); Swatches->SetInnerSlotPadding(FVector2D(8,8)); AddLine(Edit,Swatches,18);
		for (int32 I=0;I<ACELobbyArt::NumTints();++I)
		{
			auto* B=ActionButton(TEXT(""),TEXT("tint"),FString::FromInt(I));
			const FACELobbyTint& T=ACELobbyArt::Tint(I);
			B->SetToolTipText(FText::FromString(T.Name));
			auto* Swatch=HBox();
			AddH(Swatch,Decor(SACELobbyDecor::EKind::Gem,T.Gem,0,14.f,14.f),FMargin(0,0,7,0));
			AddH(Swatch,Text(T.Name,BodyFont(13.f),Hex(0xe2d2a8)));
			B->SetContent(Swatch); ButtonStyle(B,false);
			Swatches->AddChildToWrapBox(B);
			TintButtons.Add(B);
		}
		AddLine(Edit,Rule(),16);
		Buttons(Edit,{{TEXT("Save Server"),TEXT("saveserver")},{TEXT("Cancel"),TEXT("play")}});
	}

	// Directory stays inside the same widget surface: no OS dropdowns in stereo.
	// The list fills the left; the selected world's details and links sit in a panel on the right.
	{
		UVerticalBox* Browser; Pages->AddChild(PageWindow(TEXT("Find Your Next World"),Browser,false));
		auto* Cols=HBox(); auto* ColsSlot=Browser->AddChildToVerticalBox(Cols); ColsSlot->SetSize(Fill);
		auto* L=VBox(); AddH(Cols,L,FMargin(0,0,28,0),true,VAlign_Fill);
		auto* Lead=HBox(); AddLine(L,Lead,14);
		AddH(Lead,Text(TEXT("Community server directory  \u00B7  ACE + GDLE"),BodyFont(15.f),Hex(0xb9a77f)),FMargin(0),true);
		BrowserCount=Text(TEXT(""),BodyFont(12.5f),Hex(0x8f8166)); AddH(Lead,BrowserCount,FMargin(0,0,14,0));
		auto* RefreshB=ActionButton(TEXT("Refresh"),TEXT("refresh")); RefreshB->LobbyStyle=LobbySmall; ButtonStyle(RefreshB,false); AddH(Lead,RefreshB);
		SearchBox=Field(L,TEXT("Search names, descriptions or addresses"),TEXT("ServerSearch"));
		SearchBox->OnTextChanged.AddDynamic(this,&UACELoginWidget::OnSearchChanged);
		BrowserList=Slim(WidgetTree->ConstructWidget<UScrollBox>());
		auto* BrowserSlot=L->AddChildToVerticalBox(BrowserList); BrowserSlot->SetSize(Fill); BrowserSlot->SetPadding(FMargin(0,0,0,12));
		AddLine(L,Text(TEXT("Directory maintained by acresources / Servers.xml. Adding a server does not connect to it."),BodyFont(12.5f),Hex(0x8f8366),true),0);
		UVerticalBox* Detail; AddH(Cols,SidePanel(TEXT("Selected world"),Detail,430.f),FMargin(0),false,VAlign_Fill);
		BrowserName=Retail(TEXT(""),TEXT("fancyfont30"),1.f,Hex(0xffe2a0));
		BrowserName->SetClipping(EWidgetClipping::ClipToBounds); BrowserName->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		AddLine(Detail,BrowserName,6);
		BrowserMeta=Text(TEXT(""),BodyFont(13.f),Hex(0xa99878)); AddLine(Detail,BrowserMeta,16);
		auto* DescScroll=Slim(WidgetTree->ConstructWidget<UScrollBox>());
		BrowserDetail=Text(TEXT("Refresh to load the public server directory."),BodyFont(14.5f),Hex(0xd9cba6),true); DescScroll->AddChild(BrowserDetail);
		auto* DescSlot=Detail->AddChildToVerticalBox(DescScroll); DescSlot->SetSize(Fill); DescSlot->SetPadding(FMargin(0,0,0,16));
		AddLine(Detail,Rule(),16);
		auto* Act=VBox(); AddLine(Detail,Act,0);
		AddLine(Act,ActionButton(TEXT("Add to My Servers"),TEXT("import")),12);
		auto* Links=HBox(); AddLine(Act,Links,0);
		auto* Web=ActionButton(TEXT("Website"),TEXT("browserwebsite")); Web->LobbyStyle=LobbySmall; ButtonStyle(Web,false); AddH(Links,Web,FMargin(0,0,8,0));
		auto* Disc=ActionButton(TEXT("Discord"),TEXT("browserdiscord")); Disc->LobbyStyle=LobbySmall; ButtonStyle(Disc,false); AddH(Links,Disc);
	}

	{
		UVerticalBox* Files; auto* FilesPage=PageWindow(TEXT("Game Files"),Files,true); Pages->AddChild(FilesPage); HugTop(FilesPage);
		auto* Cols=HBox(); AddLine(Files,Cols,0);
		auto* L=VBox(); AddH(Cols,L,FMargin(0,0,32,0),true,VAlign_Top);
		FileProblem=Text(TEXT(""),BodyFont(16.f),Hex(0xffa05a),true);
		FileProblem->SetVisibility(ESlateVisibility::Collapsed); AddLine(L,FileProblem,16);
		AddLine(L,Text(TEXT("Choose the folder containing your Asheron's Call .dat files. The client reads these files directly."),BodyFont(15.f),Hex(0xe2d2a8),true),20);
		DatBox=Field(L,TEXT("DAT folder"),TEXT("DatDirectory")); DatBox->OnTextChanged.AddDynamic(this,&UACELoginWidget::OnDatChanged);
#if PLATFORM_WINDOWS
		if (!UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled()) AddLine(L,ActionButton(TEXT("Choose folder"),TEXT("browsefolder")));
		Buttons(L,{{TEXT("Save Location"),TEXT("savedat")},{TEXT("Use Default"),TEXT("defaultdat")}});
#else
		Buttons(L,{{TEXT("Save Location"),TEXT("savedat")},{TEXT("Use App DAT Folder"),TEXT("defaultdat")}});
		AddLine(L,Text(PLATFORM_LINUX ? TEXT("Enter an absolute Linux folder path. DAT filenames are case sensitive.") : PLATFORM_MAC ? TEXT("Enter an absolute folder path, for example /Users/you/Games/Asheron's Call.") : TEXT("On Quest, the folder must be accessible to this app. The installer uses the app's DAT folder."),BodyFont(13.5f),Hex(0x8f8366),true),14);
#endif
		AddLine(L,Text(TEXT("Changing an already-loaded installation takes effect after restarting. No game files are moved or copied."),BodyFont(13.5f),Hex(0x8f8366),true),0);
		UVerticalBox* Check; AddH(Cols,SidePanel(TEXT("Installation check"),Check,430.f),FMargin(0),false,VAlign_Top);
		FileRows=VBox(); AddLine(Check,FileRows,12);
		FileStatus=Text(TEXT(""),BodyFont(13.5f),Hex(0x8f8366),true); AddLine(Check,FileStatus,0);
	}

	UVerticalBox* UpdatesPage; auto* UpdatesWindow=PageWindow(TEXT("Client Updates"),UpdatesPage,true); Pages->AddChild(UpdatesWindow); HugTop(UpdatesWindow);
	auto* UpdateCols=HBox(); AddLine(UpdatesPage,UpdateCols,0);
	auto* Updates=VBox(); AddH(UpdateCols,Updates,FMargin(0,0,32,0),true,VAlign_Top);
	AddLine(Updates,Text(TEXT("New versions are checked automatically when the launcher opens. Updates are optional and keep your accounts, settings, and DAT files."),BodyFont(15.f),Hex(0xe2d2a8),true),16);
	AutoUpdateCheckBox=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),TEXT("AutoUpdateCheckBox"));
	// Draw the indicator with Slate primitives and text so it stays legible in
	// packaged clients without relying on the editor's checkbox image resources.
	FCheckBoxStyle AutoUpdateStyle;
	AutoUpdateStyle.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
		.SetUncheckedImage(FSlateNoResource()).SetCheckedImage(FSlateNoResource())
		.SetUncheckedHoveredImage(FSlateRoundedBoxBrush(FLinearColor(1,.85f,.5f,.06f),4.f)).SetCheckedHoveredImage(FSlateRoundedBoxBrush(FLinearColor(1,.85f,.5f,.06f),4.f))
		.SetUncheckedPressedImage(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.3f),4.f)).SetCheckedPressedImage(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.3f),4.f));
	AutoUpdateStyle.SetForegroundColor(LobbyTextColor).SetHoveredForegroundColor(LobbyTextColor).SetPressedForegroundColor(LobbyTextColor)
		.SetCheckedForegroundColor(LobbyTextColor).SetCheckedHoveredForegroundColor(LobbyTextColor).SetCheckedPressedForegroundColor(LobbyTextColor);
	AutoUpdateStyle.SetPadding(FMargin(0,6)); AutoUpdateCheckBox->SetWidgetStyle(AutoUpdateStyle);
	auto* AutoUpdateRow=HBox(); AutoUpdateCheckBox->SetContent(AutoUpdateRow);
	auto* IndicatorSize=WidgetTree->ConstructWidget<USizeBox>(); IndicatorSize->SetWidthOverride(26); IndicatorSize->SetHeightOverride(26);
	AddH(AutoUpdateRow,IndicatorSize);
	auto* Indicator=WidgetTree->ConstructWidget<UBorder>(); Indicator->SetBrush(FSlateRoundedBoxBrush(Hex(0x080c15,.96f),2.f,Hex(0x8a6a2e),1.5f));
	Indicator->SetPadding(FMargin(0)); Indicator->SetVerticalAlignment(VAlign_Center); IndicatorSize->SetContent(Indicator);
	AutoUpdateMark=WidgetTree->ConstructWidget<UTextBlock>(); AutoUpdateMark->SetText(FText::FromString(TEXT("\u2713")));
	AutoUpdateMark->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18));
	AutoUpdateMark->SetAutoWrapText(false); AutoUpdateMark->SetJustification(ETextJustify::Center);
	AutoUpdateMark->SetColorAndOpacity(Hex(0xffd77a)); AutoUpdateMark->SetRenderOpacity(0); Indicator->SetContent(AutoUpdateMark);
	AddH(AutoUpdateRow,Text(TEXT("Automatically download and install updates"),BodyFont(16.f),Hex(0xf2e3bd)),FMargin(12,0,0,0),true);
	AutoUpdateCheckBox->SetIsChecked(false);
	AutoUpdateCheckBox->OnCheckStateChanged.AddDynamic(this,&UACELoginWidget::OnAutoUpdateChanged);
	AddLine(Updates,AutoUpdateCheckBox,6);
	AddLine(Updates,Text(TEXT("Off by default. When enabled, checks every 15 minutes in the launcher and updates before you log in. Never interrupts gameplay. Cancel pauses automatic updates until the next app launch or Check now."),BodyFont(13.5f),Hex(0x8f8366),true),18);
	Buttons(Updates,{{TEXT("Release Notes"),TEXT("updatenotes")}});
	UVerticalBox* UpdateSide; AddH(UpdateCols,SidePanel(FString::Printf(TEXT("Version %s"),ACEClientBuild::Version),UpdateSide,430.f),FMargin(0),false,VAlign_Top);
	UpdateText=Text(TEXT("Checking for updates..."),BodyFont(16.f),Hex(0xf2e3bd),true); AddLine(UpdateSide,UpdateText,18);
	if (!VialArt) VialArt=MakeShared<FACEVialArt>();
	auto* UpdateVial=WidgetTree->ConstructWidget<UNativeWidgetHost>(); UpdateVial->SetVisibility(ESlateVisibility::HitTestInvisible);
	UpdateVial->SetContent(SNew(SACEManaVial).Art(VialArt).Fill_Lambda([WeakThis](){return WeakThis.IsValid()?WeakThis->UpdatePageFill:0.f;}));
	auto* ProgressSize=WidgetTree->ConstructWidget<USizeBox>(); ProgressSize->SetWidthOverride(VialW); ProgressSize->SetHeightOverride(VialH); ProgressSize->SetContent(UpdateVial);
	ProgressSize->SetVisibility(ESlateVisibility::Collapsed); UpdateProgressBox=ProgressSize; // Shown only while the updater works.
	AddLine(UpdateSide,ProgressSize,18); Cast<UVerticalBoxSlot>(ProgressSize->Slot)->SetHorizontalAlignment(HAlign_Left);
	auto* UpdateActions=WidgetTree->ConstructWidget<UWrapBox>(); UpdateActions->SetInnerSlotPadding(FVector2D(10,10)); AddLine(UpdateSide,UpdateActions,14);
	CheckUpdateButton=ActionButton(TEXT("Check Now"),TEXT("checkupdate")); UpdateActions->AddChildToWrapBox(CheckUpdateButton);
	DownloadUpdateButton=ActionButton(TEXT("Download Update"),TEXT("downloadupdate"),FString(),true); UpdateActions->AddChildToWrapBox(DownloadUpdateButton);
	InstallUpdateButton=ActionButton(PLATFORM_ANDROID?TEXT("Install"):TEXT("Install and Restart"),TEXT("installupdate"),FString(),true); UpdateActions->AddChildToWrapBox(InstallUpdateButton);
	CancelUpdateButton=ActionButton(TEXT("Cancel Update"),TEXT("cancelupdate")); UpdateActions->AddChildToWrapBox(CancelUpdateButton);
	UpdateInstallHelp=Text(ACEUpdates::InstallationNotice(PLATFORM_ANDROID),BodyFont(13.5f),Hex(0x8f8366),true); AddLine(UpdateSide,UpdateInstallHelp,0);

	{
		auto* PluginPanel=WidgetTree->ConstructWidget<UACEPluginPanel>();
		Pages->AddChild(Window(TEXT("Plugins"),PluginPanel,nullptr,FMargin(20,14,20,18)));
	}

	// ---- status line: grows upward from the bottom of the right column ----
	{
		auto* StatusBorder=WidgetTree->ConstructWidget<UBorder>();
		StatusBorder->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.42f),2.f,Hex(0xc9963c,.45f),1.f));
		StatusBorder->SetPadding(FMargin(44,10,16,11));
		StatusText=Text(TEXT(""),BodyFont(14.f),Hex(0xe2d2a8),true); StatusBorder->SetContent(StatusText);
		StatusBorder->SetVisibility(ESlateVisibility::Collapsed);
		auto* StatusLayers=WidgetTree->ConstructWidget<UOverlay>();
		{ auto* BorderSlot=StatusLayers->AddChildToOverlay(StatusBorder); BorderSlot->SetHorizontalAlignment(HAlign_Fill); BorderSlot->SetVerticalAlignment(VAlign_Fill); }
		TWeakObjectPtr<UBorder> WeakBorder(StatusBorder);
		auto IconKind=[WeakThis](){ const int32 K=WeakThis.IsValid()?WeakThis->StatusKind:1; return K==2?SACELobbyGlyph::EKind::Bang:K==3?SACELobbyGlyph::EKind::Spinner:SACELobbyGlyph::EKind::Check; };
		auto* Icon=HostOf(SNew(SBorder).Visibility_Lambda([WeakBorder](){ return WeakBorder.IsValid()&&WeakBorder->GetVisibility()!=ESlateVisibility::Collapsed?EVisibility::HitTestInvisible:EVisibility::Collapsed; })
			.BorderImage(new FSlateRoundedBoxBrush(FLinearColor(0,0,0,.45f),11.f,Hex(0xc9963c),1.f)).Padding(FMargin(4)).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[SNew(SACELobbyGlyph).Kind_Lambda(IconKind).Size(12.f).Color_Lambda([WeakThis](){ return WeakThis.IsValid()&&WeakThis->StatusKind==2?Hex(0xff9a6a):Hex(0xffd77a); })],22,22);
		auto* IconSlot=StatusLayers->AddChildToOverlay(Icon); IconSlot->SetHorizontalAlignment(HAlign_Left); IconSlot->SetVerticalAlignment(VAlign_Center); IconSlot->SetPadding(FMargin(12,0,0,0));
		auto* StatusWidth=WidgetTree->ConstructWidget<USizeBox>(); StatusWidth->SetMaxDesiredWidth(666); StatusWidth->SetContent(StatusLayers);
		StatusAnchor=StatusWidth; At(StatusWidth,1223,988)->SetAlignment(FVector2D(.5f,1.f));
	}
	// Inline destructive-action confirmation is also fully usable with VR pointers.
	{
		auto* Confirmation=VBox();
		ConfirmText=Text(TEXT(""),BodyFont(16.f),Hex(0xf2e3bd),true); AddLine(Confirmation,ConfirmText,14);
		Buttons(Confirmation,{{TEXT("Remove"),TEXT("confirmremove")},{TEXT("Keep"),TEXT("cancelremove")}});
		ConfirmPanel=WidgetTree->ConstructWidget<UBorder>(); ConfirmPanel->SetBrush(FSlateNoResource()); ConfirmPanel->SetPadding(FMargin(0));
		auto* ConfirmSize=WidgetTree->ConstructWidget<USizeBox>(); ConfirmSize->SetWidthOverride(520); ConfirmSize->SetContent(Frame(Confirmation,FMargin(26,22)));
		ConfirmPanel->SetContent(ConfirmSize);
		At(ConfirmPanel,1223,330)->SetAlignment(FVector2D(.5f,0));
		ConfirmPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	// ---- World popover: scrim over the whole window, then the panel beside the plaque ----
	{
		auto* Scrim=ActionButton(TEXT(""),TEXT("closeworld")); Scrim->SetContent(nullptr);
		WorldScrim=Scrim; At(Scrim,-4000,-4000,9600,9000); Scrim->SetVisibility(ESlateVisibility::Collapsed);
		auto* Pop=VBox();
		auto* Head=HBox(); AddLine(Pop,Head,9);
		AddH(Head,Caps(TEXT("Choose your world"),12.f,2.6f),FMargin(0),true);
		WorldCount=Text(TEXT(""),BodyFont(12.f),Hex(0x8f8166)); AddH(Head,WorldCount);
		AddLine(Pop,Rule(),12);
		ServerListSize=WidgetTree->ConstructWidget<USizeBox>(); ServerListSize->SetHeightOverride(ServerRowHeight);
		ServerList=Slim(WidgetTree->ConstructWidget<UScrollBox>()); ServerListSize->SetContent(ServerList);
		AddLine(Pop,ServerListSize,8);
		AddLine(Pop,Rule(),14);
		auto* NameRow=HBox(); AddLine(Pop,NameRow,6);
		PopoverName=Retail(TEXT(""),TEXT("fancyfont30"),1.f,Hex(0xffe2a0));
		PopoverName->SetClipping(EWidgetClipping::ClipToBounds); PopoverName->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		AddH(NameRow,PopoverName,FMargin(0),false,VAlign_Bottom);
		PopoverTag=Text(TEXT(""),CapsFont(10.f,1.f),Hex(0xe8cf95)); PopoverTag->SetShadowOffset(FVector2D(0,0));
		auto* Tag=WidgetTree->ConstructWidget<UBorder>(); Tag->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.45f),2.f,Hex(0x7a5d27),1.f));
		Tag->SetPadding(FMargin(5,1,5,2)); Tag->SetContent(PopoverTag); Tag->SetVisibility(ESlateVisibility::HitTestInvisible);
		AddH(NameRow,Tag,FMargin(10,0,0,5),false,VAlign_Bottom);
		AddH(NameRow,Gap(1,1),FMargin(0),true);
		ServerDescription=Text(TEXT(""),BodyFont(13.5f),Hex(0xcbbd98),true); AddLine(Pop,ServerDescription,8);
		auto* AddressRow=HBox(); AddLine(Pop,AddressRow,14);
		PopoverAddress=Text(TEXT(""),BodyFont(12.5f),Hex(0xa99878)); AddH(AddressRow,PopoverAddress);
		AddH(AddressRow,Text(TEXT("  \u00B7  Page tint "),BodyFont(12.5f),Hex(0xa99878)));
		AddH(AddressRow,Decor(SACELobbyDecor::EKind::Gem,TAttribute<FLinearColor>::CreateLambda([TintOf](){ return TintOf().Gem; }),0,12.f,12.f));
		auto* Links=HBox(); AddLine(Pop,Links,14);
		WebsiteButton=ActionButton(TEXT("Website"),TEXT("website")); AddH(Links,WebsiteButton,FMargin(0,0,8,0));
		DiscordButton=ActionButton(TEXT("Discord"),TEXT("discord")); AddH(Links,DiscordButton,FMargin(0,0,8,0));
		AddH(Links,ActionButton(TEXT("Edit"),TEXT("editserver")));
		AddH(Links,Gap(1,1),FMargin(0),true);
		AddH(Links,ActionButton(TEXT("Remove"),TEXT("removeserver")));
		AddLine(Pop,Rule(),12);
		auto* More=HBox(); AddLine(Pop,More,0);
		AddH(More,ActionButton(TEXT("+ Custom Server"),TEXT("newserver")),FMargin(0,0,10,0));
		AddH(More,ActionButton(TEXT("Browse Directory"),TEXT("browser")));
		auto* Panel=WidgetTree->ConstructWidget<UOverlay>();
		TSharedRef<SACELobbyFrame> Painted=SNew(SACELobbyFrame).Shadow(true).Hairline(true)
			.Fill_Lambda([TintOf](){ return TintOf().PopBottom; }).FillTop_Lambda([TintOf](){ return TintOf().PopTop; });
		LobbyFrames.Add(Painted);
		auto* PaintSlot=Panel->AddChildToOverlay(HostOf(Painted)); PaintSlot->SetHorizontalAlignment(HAlign_Fill); PaintSlot->SetVerticalAlignment(VAlign_Fill);
		auto* PopSlot=Panel->AddChildToOverlay(Pop); PopSlot->SetPadding(FMargin(24,22,24,22)); PopSlot->SetHorizontalAlignment(HAlign_Fill);
		auto* PopSize=WidgetTree->ConstructWidget<USizeBox>(); PopSize->SetWidthOverride(460); PopSize->SetContent(Panel);
		WorldPopover=PopSize; At(PopSize,384,770)->SetAlignment(FVector2D(0,1.f));
		PopSize->SetVisibility(ESlateVisibility::Collapsed);
		// Arrow nub pointing back at the plaque.
		auto* Nub=Glyph(SACELobbyGlyph::EKind::Pointer,16,Hex(0xd9a548));
		Nub->SetRenderScale(FVector2D(-1,1)); WorldNub=Nub; At(Nub,370,722,16,16); Nub->SetVisibility(ESlateVisibility::Collapsed);
	}

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
		// Status lines are transient: they clear themselves after eight seconds.
		if (StatusText && StatusKind!=3 && StatusSetTime>0 && FPlatformTime::Seconds()-StatusSetTime>=8.0 && !StatusText->GetText().IsEmpty()) SetStatus(FString());
	}
	TickConnectScreen(Dt);
#if !UE_BUILD_SHIPPING
	// -ACELobbyShot: once the DAT art is in, capture the Codex Hall lobby on two worlds (with the
	// world popover and Launch hover), a search, a drag preview and the other tabs, then quit.
	// The original server and account are put back before exit and nothing else is saved.
	if (bLobbyArtApplied && FParse::Param(FCommandLine::Get(),TEXT("ACELobbyShot")))
	{
		if (LobbyShotTime<0) { LobbyShotAccountId=Profile.SelectedAccountId; LobbyShotServerId=Profile.SelectedServerId; }
		const float Previous=LobbyShotTime<0?0.f:LobbyShotTime; LobbyShotTime=Previous+Dt;
		auto At=[&](float T){ return Previous<T && LobbyShotTime>=T; };
		auto Shot=[](const TCHAR* Name){ FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/Name,true,false); };
		auto ServerNamed=[&](const TCHAR* Name){ const auto* S=Profile.Servers.FindByPredicate([&](const FACELoginServer& E){ return E.Name.Contains(Name); }); return S?S->Id:FString(); };
		auto Pick=[&](const TCHAR* Name){ const FString Id=ServerNamed(Name); if (!Id.IsEmpty()) { SelectServer(Id); bLoginEntriesDirty=false; } };
		if (At(.5f)) Pick(TEXT("Coldeve"));
		if (At(3.f)) Shot(TEXT("Codex_Coldeve.png"));
		if (At(3.5f) && LoginButton) { GLobbyForceHover=true; ButtonStyle(LoginButton,false); }
		if (At(4.5f)) Shot(TEXT("Codex_LaunchHover.png"));
		if (At(5.f)) { GLobbyForceHover=false; ButtonStyle(LoginButton,false); RunAction(TEXT("worldmenu")); }
		if (At(6.f)) Shot(TEXT("Codex_Coldeve_Popover.png"));
		if (At(6.5f)) { RunAction(TEXT("closeworld")); Pick(TEXT("Reefcull")); }
		if (At(8.f)) Shot(TEXT("Codex_Reefcull.png"));
		if (At(8.5f)) RunAction(TEXT("worldmenu"));
		if (At(9.5f)) Shot(TEXT("Codex_Reefcull_Popover.png"));
		if (At(10.f)) RunAction(TEXT("closeworld"));
		if (At(10.5f) && AccountSearchBox) { AccountSearchBox->SetText(FText::FromString(TEXT("a"))); OnAccountSearchChanged(AccountSearchBox->GetText()); }
		if (At(11.5f)) Shot(TEXT("Codex_Search.png"));
		if (At(12.f)) { RunAction(TEXT("clearsearch"),FString()); RunAction(TEXT("selectaccount"),LobbyShotAccountId); }
		// Preview only: show where a drop would land; nothing is moved or saved.
		if (At(12.5f) && AccountList && AccountList->GetNumItems()>1)
			if (auto* Target=Cast<UACELobbyAccountItem>(AccountList->GetListItems()[1])) ACESetAccountDropPreview(Target->Id);
		if (At(13.5f)) Shot(TEXT("Codex_Drag.png"));
		if (At(14.f)) { ACESetAccountDropPreview(FString()); Pick(TEXT("Coldeve")); RunAction(TEXT("editserver")); }
		if (At(15.f)) Shot(TEXT("Codex_EditServer.png"));
		if (At(15.5f)) RunAction(TEXT("files"));
		if (At(16.5f)) Shot(TEXT("Codex_Files.png"));
		if (At(17.f)) RunAction(TEXT("updates"));
		if (At(18.f)) Shot(TEXT("Codex_Updates.png"));
		if (At(18.5f))
		{
			Pick(TEXT("Coldeve")); RunAction(TEXT("browser"));
			if (const auto* D=Directory.FindByPredicate([](const FACELoginServer& E){ return E.Name.Contains(TEXT("Coldeve")); })) RunAction(TEXT("directoryselect"),D->Id);
			else if (!Directory.IsEmpty()) RunAction(TEXT("directoryselect"),Directory[0].Id);
		}
		if (At(20.f)) Shot(TEXT("Codex_Browser.png"));
		if (At(20.5f)) RunAction(TEXT("plugins"));
		if (At(21.5f)) Shot(TEXT("Codex_Plugins.png"));
		if (At(22.5f)) { RunAction(TEXT("browser")); Pick(TEXT("Reefcull")); }
		if (At(24.f)) Shot(TEXT("Codex_Browser_Reefcull.png"));
		if (At(25.f))
		{
			// Launch disabled with no account chosen (in memory only; nothing is saved).
			RunAction(TEXT("accounts"));
			Profile.SelectedAccountId.Reset(); RefreshAccountLists(); UpdateSelectedAccountInfo();
		}
		if (At(26.5f)) Shot(TEXT("Codex_NoAccount.png"));
		if (At(27.5f))
		{
			if (!LobbyShotServerId.IsEmpty()) SelectServer(LobbyShotServerId);
			RunAction(TEXT("selectaccount"),LobbyShotAccountId);
			bLoginEntriesDirty=false;
		}
		if (At(28.5f)) FPlatformMisc::RequestExit(false);
	}
#endif
	if (UpdateProgressBox)
	{
		// The Updates-page vial appears only while checking/downloading/verifying, then shows
		// full for a moment when a download completes; otherwise the status text stands alone.
		const EACEUpdateState S=Updater?Updater->State:EACEUpdateState::Idle;
		const bool bActive=S==EACEUpdateState::Checking || S==EACEUpdateState::Downloading || S==EACEUpdateState::Verifying;
		const double Now=FPlatformTime::Seconds();
		if (bActive) bUpdateVialWasActive=true;
		else if (bUpdateVialWasActive)
		{
			bUpdateVialWasActive=false;
			UpdateVialHoldUntil=S==EACEUpdateState::Ready || S==EACEUpdateState::InstallNotice || S==EACEUpdateState::Installing?Now+1.0:0.0;
		}
		const bool bShow=bActive || Now<UpdateVialHoldUntil;
		const ESlateVisibility Wanted=bShow?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed;
		if (UpdateProgressBox->GetVisibility()!=Wanted) UpdateProgressBox->SetVisibility(Wanted);
		if (!bShow) UpdatePageFill=0; // Start from empty the next time it appears.
	}
	UpdatePageFill=FMath::FInterpTo(UpdatePageFill,UpdatePageTarget,Dt,6.f);
	if(StyledPage!=Pages->GetActiveWidgetIndex())
	{
		StyledPage=Pages->GetActiveWidgetIndex();
		// The Accounts window (and the Launch ring on the home page) belong to the Accounts tab only;
		// the other tabs use the whole width right of the nav column.
		const bool bHome=StyledPage==0;
		if (AccountsWindow) AccountsWindow->SetVisibility(bHome?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
		const float StatusX=bHome?1223.f:983.f;
		if (StatusAnchor) if (auto* C=Cast<UCanvasPanelSlot>(StatusAnchor->Slot)) C->SetPosition(FVector2D(StatusX,C->GetPosition().Y));
		if (ConfirmPanel) if (auto* C=Cast<UCanvasPanelSlot>(ConfirmPanel->Slot)) C->SetPosition(FVector2D(StatusX,C->GetPosition().Y));
		// Accounts (home), Servers (directory, and the server editor), Game Files, Updates, Plugins.
		const int32 PageForTab[]={0,2,3,4,5};
		for(int32 I=0;I<NavigationButtons.Num();++I)
			ButtonStyle(NavigationButtons[I],I<UE_ARRAY_COUNT(PageForTab) && (PageForTab[I]==StyledPage || (I==1 && StyledPage==1)));
	}
	const FVector2D View=Geometry.GetLocalSize();
	if(View.X<=0 || View.Y<=0) return;
	LayoutLobby(View);
}

void UACELoginWidget::LayoutLobby(const FVector2D& View)
{
	// One scale for the whole lobby, fitting the 1600x1000 Codex Hall stage inside the window.
	const float Scale=FMath::Clamp(float(FMath::Min(View.X/DesignW,View.Y/DesignH)),.1f,2.5f);
	if (ResponsiveScale && !FMath::IsNearlyEqual(ResponsiveScale->GetUserSpecifiedScale(),Scale)) ResponsiveScale->SetUserSpecifiedScale(Scale);
	const FVector2D L=View/Scale;
	if (!L.Equals(LayoutView,.5f))
	{
		LayoutView=L;
		// The 800x600 backdrop covers the window, kept to the right edge like character select;
		// most of any extra height is cropped from the sky.
		const float S=FMath::Max(float(L.X/800.),float(L.Y/600.));
		LayoutBgScale=S;
		const float BgX=float(L.X-800.f*S), BgY=float(L.Y-600.f*S)*.2f;
		auto SlotOf=[](UWidget* W){ return W?Cast<UCanvasPanelSlot>(W->Slot):nullptr; };
		if (auto* Bg=SlotOf(LobbyBackground)) { Bg->SetPosition(FVector2D(BgX,BgY)); Bg->SetSize(FVector2D(800.f*S,600.f*S)); }
		if (LobbyBackgroundSide) LobbyBackgroundSide->SetVisibility(ESlateVisibility::Collapsed);
		// Tint wash low on the left (mockup: ellipse 50%x40% at 18%,70%).
		if (auto* Wash=SlotOf(LobbyWash)) { Wash->SetPosition(FVector2D(L.X*.18f,L.Y*.7f)); Wash->SetSize(FVector2D(L.X*.7f,L.Y*.56f)); }
		if (auto* Stage=SlotOf(DashboardSize)) Stage->SetPosition(FVector2D((L.X-DesignW)*.5f,(L.Y-DesignH)*.5f));
	}
	// The quiet list uses a thin scrollbar only; the retail arrow buttons stay collapsed (they remain
	// as actions for keyboard and gamepad scrolling).
	if (AccountScrollUp && AccountScrollUp->GetVisibility()!=ESlateVisibility::Collapsed) { AccountScrollUp->SetVisibility(ESlateVisibility::Collapsed); AccountScrollDown->SetVisibility(ESlateVisibility::Collapsed); }
}

void UACELoginWidget::ApplyLobbyArt()
{
	const FACELobbyArt* Art=ACELobbyArt::Get();
	if (!Art) return;
	bLobbyArtApplied=true;
	if (LobbyVignette) { LobbyVignette->SetBrush(Art->Vignette); LobbyVignette->SetRenderOpacity(1); }
	if (LobbyCrest) { LobbyCrest->SetBrush(Art->Crest); LobbyCrest->SetRenderOpacity(1); }
	if (LobbyBrand) LobbyBrand->SetVisibility(ESlateVisibility::Collapsed); // The crest carries the retail logo.
	if (AccountSearchBox)
	{
		FEditableTextBoxStyle Style=AccountSearchBox->GetWidgetStyle();
		FSlateBrush Focus=Art->Search[1]; Focus.TintColor=FLinearColor(1.25f,1.25f,1.25f,1);
		Style.SetBackgroundImageNormal(Art->Search[0]).SetBackgroundImageHovered(Art->Search[0]).SetBackgroundImageFocused(Focus).SetBackgroundImageReadOnly(Art->Search[0]);
		AccountSearchBox->SetWidgetStyle(Style);
		// UEditableTextBox::SetWidgetStyle hands Slate a pointer to its argument; re-point it at the
		// widget's own copy so the live text box never reads the (now gone) local above.
		AccountSearchBox->SetWidgetStyle(AccountSearchBox->GetWidgetStyle());
	}
	ApplyPageTint();
	WidgetTree->ForEachWidget([](UWidget* Widget){ if (auto* B=Cast<UACELoginActionButton>(Widget)) ButtonStyle(B,B->bLobbyAccent); });
	for (auto* List:{AccountList.Get(),FavoriteList.Get()}) if (List) List->ApplyArt();
	for (const auto& Frame:LobbyFrames) if (const TSharedPtr<SWidget> Pinned=Frame.Pin()) Pinned->Invalidate(EInvalidateWidgetReason::Paint);
	StyledPage=INDEX_NONE;
}

int32 UACELoginWidget::TintFor(const FString& ServerId) const
{
	// Saved choice first; otherwise Coldeve keeps the retail blue, Reefcull is teal, and every
	// other world gets the first colour no other server is using (in list order).
	const int32 N=ACELobbyArt::NumTints();
	auto Default=[](const FACELoginServer& S)
	{
		const FString Name=S.Name.ToLower();
		return Name.Contains(TEXT("coldeve"))?0:Name.Contains(TEXT("reefcull"))?1:INDEX_NONE;
	};
	TArray<bool> Used; Used.Init(false,N);
	for (const auto& S:Profile.Servers)
	{
		const int32 T=S.Tint>=0?S.Tint:Default(S);
		if (T>=0 && T<N) Used[T]=true;
	}
	for (const auto& S:Profile.Servers)
	{
		int32 T=S.Tint>=0?S.Tint:Default(S);
		if (T<0)
		{
			T=Used.IndexOfByKey(false); if (T==INDEX_NONE) T=0;
			Used[T]=true;
		}
		if (S.Id==ServerId) return FMath::Clamp(T,0,N-1);
	}
	// A server not saved yet (New server): the next unused colour.
	const int32 Free=Used.IndexOfByKey(false);
	return Free==INDEX_NONE?0:Free;
}

void UACELoginWidget::ApplyPageTint()
{
	const FACELoginServer* S=Profile.SelectedServer();
	PageTint=S?TintFor(S->Id):0;
	GLobbyTint=PageTint;
	if (LobbyBackground)
		if (const FSlateBrush* Bg=ACELobbyArt::TintedBackground(PageTint)) { LobbyBackground->SetBrush(*Bg); LobbyBackground->SetRenderOpacity(1); }
	if (LobbyWash)
		if (const FACELobbyArt* Art=ACELobbyArt::Get())
		{
			FSlateBrush Wash=Art->Glow; Wash.TintColor=FSlateColor(FLinearColor::White);
			LobbyWash->SetBrush(Wash); FLinearColor C=ACELobbyArt::Tint(PageTint).Gem; C.A=.22f; LobbyWash->SetColorAndOpacity(C);
			LobbyWash->SetRenderOpacity(1);
		}
	if (auto* Launch=Cast<UACELoginActionButton>(LoginButton)) ButtonStyle(Launch,Launch->bLobbyAccent);
	for (const auto& Frame:LobbyFrames) if (const TSharedPtr<SWidget> Pinned=Frame.Pin()) Pinned->Invalidate(EInvalidateWidgetReason::Paint);
}

void UACELoginWidget::RefreshTintButtons()
{
	// An unset tint shows the automatic default it will keep unless a swatch is picked.
	const int32 Shown=EditingTint>=0?EditingTint:TintFor(EditingServerId);
	for (UACELoginActionButton* B:TintButtons) if (B) ButtonStyle(B,FCString::Atoi(*B->Value)==Shown);
}

bool UACELoginWidget::IsWorldMenuOpen() const
{
	return WorldPopover && WorldPopover->GetVisibility()!=ESlateVisibility::Collapsed;
}

void UACELoginWidget::SetWorldMenuOpen(bool bOpen)
{
	const ESlateVisibility Panel=bOpen?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed;
	if (WorldPopover) WorldPopover->SetVisibility(Panel);
	if (WorldNub) WorldNub->SetVisibility(bOpen?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	if (WorldScrim) WorldScrim->SetVisibility(bOpen?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
	if (WorldPlaque) ButtonStyle(WorldPlaque,bOpen);
	if (bOpen) RefreshServers();
}

bool UACELoginWidget::IsLobbyKeyboardActive() const
{
	// Typing in the account form or with the world menu open must not drive the list or Launch.
	auto Focused=[](const UWidget* W){ return W && W->HasKeyboardFocus(); };
	return Pages && Pages->GetActiveWidgetIndex()==0 && PendingRemoval.IsEmpty()
		&& !IsConnectScreenVisible() && !IsWorldMenuOpen()
		&& !Focused(AccountAliasBox) && !Focused(AccountBox) && !Focused(PasswordBox);
}

FReply UACELoginWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key=InKeyEvent.GetKey();
	if (Key==EKeys::Escape && IsWorldMenuOpen()) { RunAction(TEXT("closeworld")); return FReply::Handled(); }
	if (IsLobbyKeyboardActive() && AccountSearchBox)
	{
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
		// "/" focuses the search; other printable keys go straight into it (type-to-jump).
		if (C==TEXT('/')) { AccountSearchBox->SetKeyboardFocus(); return FReply::Handled(); }
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

void UACELoginWidget::RefreshLaunchState()
{
	const bool Busy=Updater && Updater->IsBusy();
	const bool bAccount=Profile.Accounts.ContainsByPredicate([&](const FACELoginAccount& E){ return E.Id==Profile.SelectedAccountId; });
	const bool bEnable=!Busy && Profile.SelectedServer()!=nullptr && bAccount;
	if (LoginButton && LoginButton->GetIsEnabled()!=bEnable) LoginButton->SetIsEnabled(bEnable);
}

void UACELoginWidget::RefreshUpdateControls()
{
	if (AutoUpdateMark) AutoUpdateMark->SetRenderOpacity(Profile.bAutoUpdate?1.f:0.f);
	RefreshLaunchState();
	if (!Updater || !UpdateText) return;
	const auto State=Updater->State;
	const bool Busy=Updater->IsBusy();
	const bool Available=Updater->Release.Number>ACEClientBuild::ReleaseNumber;
	bUpdateAvailable=Available;
	FString Text=Updater->Message;
	if (State==EACEUpdateState::Downloading) Text+=FString::Printf(TEXT(" %.0f%%"),Updater->Progress()*100);
	if (UpdateText->GetText().ToString()!=Text) UpdateText->SetText(FText::FromString(Text));
	UpdateInstallHelp->SetVisibility(State==EACEUpdateState::Ready || State==EACEUpdateState::InstallNotice || State==EACEUpdateState::Installing?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
	UpdatePageTarget=State==EACEUpdateState::Ready || State==EACEUpdateState::InstallNotice || State==EACEUpdateState::Installing?1.f:Updater->Progress();
	CheckUpdateButton->SetIsEnabled(!Busy && State!=EACEUpdateState::Checking);
	DownloadUpdateButton->SetVisibility(Available && !Busy && State!=EACEUpdateState::Ready?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
	InstallUpdateButton->SetVisibility(!PLATFORM_MAC && State==EACEUpdateState::Ready?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
	CancelUpdateButton->SetVisibility(State==EACEUpdateState::Downloading || State==EACEUpdateState::Verifying || State==EACEUpdateState::InstallNotice?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
	AutoUpdateCheckBox->SetIsEnabled(State!=EACEUpdateState::Installing);
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
	auto MakeText=[&](const FString& String, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		auto* T=WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(String)); T->SetFont(Font); T->SetColorAndOpacity(Color);
		T->SetShadowOffset(FVector2D(1,1)); T->SetShadowColorAndOpacity(FLinearColor(0,0,0,.6f));
		T->SetVisibility(ESlateVisibility::HitTestInvisible); T->SetClipping(EWidgetClipping::ClipToBounds); T->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		return T;
	};
	if (Profile.Servers.IsEmpty()) ServerList->AddChild(MakeText(TEXT("No saved worlds yet. Browse the directory or add your own."),BodyFont(14.f),Hex(0xb9a77f)));
	float RowPitch=ServerRowHeight;
	for (const auto& S:Profile.Servers)
	{
		const bool bSelected=S.Id==Profile.SelectedServerId;
		auto* B=ActionButton(S.Name,TEXT("selectserver"),S.Id,bSelected);
		B->SetToolTipText(FText::FromString(S.Name));
		// Gem in the world's page colour, name, then type and address; a check on the current world.
		auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
		auto* GemHost=WidgetTree->ConstructWidget<UNativeWidgetHost>(); GemHost->SetVisibility(ESlateVisibility::HitTestInvisible);
		GemHost->SetContent(SNew(SACELobbyDecor).Kind(SACELobbyDecor::EKind::Gem).Color(ACELobbyArt::Tint(TintFor(S.Id)).Gem).State(bSelected?1:0));
		auto* GemSize=WidgetTree->ConstructWidget<USizeBox>(); GemSize->SetWidthOverride(16); GemSize->SetHeightOverride(16); GemSize->SetContent(GemHost);
		auto* AccentSlot=Line->AddChildToHorizontalBox(RowAccentBar(WidgetTree,bSelected)); AccentSlot->SetVerticalAlignment(VAlign_Fill); AccentSlot->SetPadding(FMargin(0,2,0,2));
		auto* GemSlot=Line->AddChildToHorizontalBox(GemSize); GemSlot->SetVerticalAlignment(VAlign_Center); GemSlot->SetPadding(FMargin(10,0,12,0));
		auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();
		auto* LinesSlot=Line->AddChildToHorizontalBox(Lines); LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LinesSlot->SetVerticalAlignment(VAlign_Center);
		Lines->AddChildToVerticalBox(MakeText(S.Name,BodyFont(14.5f),bSelected?Hex(0xfff2cc):Hex(0xdccfab)));
		Lines->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("%s  \u00B7  %s:%d"),S.bGDLE?TEXT("GDLE"):TEXT("ACE"),*S.Host,S.Port),BodyFont(11.5f),Hex(0x8f8166)));
		auto* Check=WidgetTree->ConstructWidget<UNativeWidgetHost>(); Check->SetVisibility(ESlateVisibility::HitTestInvisible);
		Check->SetContent(SNew(SACELobbyGlyph).Kind(SACELobbyGlyph::EKind::Check).Size(12.f).Color(Hex(0xe0b86a)));
		auto* CheckSize=WidgetTree->ConstructWidget<USizeBox>(); CheckSize->SetWidthOverride(14); CheckSize->SetHeightOverride(14); CheckSize->SetContent(Check);
		CheckSize->SetRenderOpacity(bSelected?1.f:0.f);
		auto* CheckSlot=Line->AddChildToHorizontalBox(CheckSize); CheckSlot->SetVerticalAlignment(VAlign_Center); CheckSlot->SetPadding(FMargin(8,0,4,0));
		auto* Height=WidgetTree->ConstructWidget<USizeBox>(); Height->SetHeightOverride(ServerRowHeight-8.f); Height->SetContent(Line);
		Cast<USizeBoxSlot>(Line->Slot)->SetVerticalAlignment(VAlign_Fill);
		B->SetContent(Height);
		if (auto* ContentSlot=Cast<UButtonSlot>(Height->Slot)) { ContentSlot->SetHorizontalAlignment(HAlign_Fill); ContentSlot->SetVerticalAlignment(VAlign_Fill); }
		RowPitch=ServerRowHeight-8.f+B->GetStyle().NormalPadding.Top+B->GetStyle().NormalPadding.Bottom+4.f;
		Cast<UScrollBoxSlot>(ServerList->AddChild(B))->SetPadding(FMargin(0,0,0,4));
	}
	// Up to four worlds show without scrolling; the bar appears only past that.
	if (ServerListSize) ServerListSize->SetHeightOverride(Profile.Servers.IsEmpty()?40.f:FMath::Min(Profile.Servers.Num(),4)*RowPitch+6.f);
	ServerList->SetScrollBarVisibility(Profile.Servers.Num()>4?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
	if (WorldCount) WorldCount->SetText(FText::FromString(FString::Printf(TEXT("%d saved"),Profile.Servers.Num())));
}

void UACELoginWidget::SelectServer(const FString& Id)
{
	SetPasswordVisible(false);
	const FString AccountId = Profile.SelectedAccountId;
	Profile.SelectedServerId=Id; Profile.SelectedAccountId=AccountId;
	const auto* S=Profile.SelectedServer();
	HostBox->SetText(FText::FromString(S?S->Host:TEXT(""))); PortBox->SetText(FText::FromString(FString::FromInt(S?S->Port:9000)));
	const FString Type=S?(S->bGDLE?TEXT("GDLE"):TEXT("ACE")):FString();
	ServerTitle->SetText(FText::FromString(S?S->Name:TEXT("Choose your world")));
	ServerDetail->SetText(FText::FromString(S?FString::Printf(TEXT("%s  \u00B7  %s"),*Type,*S->Host):TEXT("Add a server to get started.")));
	if (PopoverName) PopoverName->SetText(FText::FromString(S?S->Name:TEXT("No world selected")));
	if (PopoverTag) { PopoverTag->SetText(FText::FromString(Type)); if (UWidget* Tag=PopoverTag->GetParent()) Tag->SetVisibility(S?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed); }
	if (PopoverAddress) PopoverAddress->SetText(FText::FromString(S?FString::Printf(TEXT("%s:%d"),*S->Host,S->Port):FString()));
	ServerDescription->SetText(FText::FromString(S?S->Description.Left(240):TEXT("")));
	ServerDescription->SetVisibility(S && !S->Description.IsEmpty()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	WebsiteButton->SetIsEnabled(S && ACELoginProfile::IsWebLink(S->Website)); DiscordButton->SetIsEnabled(S && ACELoginProfile::IsWebLink(S->Discord));
	const auto* A=Profile.Accounts.FindByPredicate([&](const auto& E){return E.Id==AccountId;});
	if (!A && !Profile.Accounts.IsEmpty()) A=&Profile.Accounts[0];
	Profile.SelectedAccountId=A?A->Id:FString();
	AccountBox->SetText(FText::FromString(A?A->Username:TEXT(""))); PasswordBox->SetText(FText::FromString(A?A->Password:TEXT("")));
	AccountAliasBox->SetText(FText::FromString(A?A->Alias:TEXT("")));
	Profile.Host=S?S->Host:FString(); Profile.Port=S?FString::FromInt(S->Port):TEXT("9000");
	Profile.Account=A?A->Username:FString(); Profile.Password=A?A->Password:FString();
	ApplyPageTint();
	RefreshServers(); RefreshAccounts(); RefreshLaunchState(); bLoginEntriesDirty=true;
}

void UACELoginWidget::RefreshAccounts()
{
	bAccountOrderDirty=false;
	RefreshAccountLists();
}

void UACELoginWidget::RefreshAccountLists()
{
	if (!AccountList || !FavoriteList) return;
	const FString Search=AccountSearchBox?AccountSearchBox->GetText().ToString().TrimStartAndEnd():FString();
	const FString Sort=Profile.AccountSort;
	TArray<const FACELoginAccount*> Favorites, Others;
	for (const auto& A:Profile.Accounts)
		if (Search.IsEmpty() || A.DisplayName().Contains(Search,ESearchCase::IgnoreCase) || A.Username.Contains(Search,ESearchCase::IgnoreCase))
			(A.bFavorite?Favorites:Others).Add(&A);
	// Custom keeps the saved order; the others are stable so ties keep it too.
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
			Item->Id=A->Id; Item->Title=A->Alias.TrimStartAndEnd().IsEmpty()?A->Username:A->Alias; Item->bFavorite=A->bFavorite; Item->bSelected=A->Id==Profile.SelectedAccountId;
			// Two lines when an alias differs from the login; "when" only from a real launch.
			Item->Username=A->Alias.IsEmpty()||A->Alias==A->Username?FString():A->Username;
			Item->When=ShortWhen(A->LastUsed);
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
	const float RowPitch=UACELobbyAccountRow::RowHeight+UACELobbyAccountRow::RowGap;
	if (FavoriteSize) FavoriteSize->SetHeightOverride(FMath::Max(1,FMath::Min(FavoriteItems.Num(),4))*RowPitch);
	const ESlateVisibility FavoriteVisibility=FavoriteItems.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible;
	if (FavoriteHeader) FavoriteHeader->SetVisibility(FavoriteVisibility);
	if (FavoriteSize) FavoriteSize->SetVisibility(FavoriteVisibility);
	const int32 Total=Profile.Accounts.Num(), Shown=VisibleAccountOrder.Num();
	if (AccountsHeading) AccountsHeading->SetText(FText::FromString(Search.IsEmpty()||Shown==Total?FString::FromInt(Total):FString::Printf(TEXT("%d / %d"),Shown,Total)));
	if (AccountsEmpty)
	{
		const FString Empty=Total==0?TEXT("No accounts yet. Use + Add to save a login."):OtherItems.IsEmpty()&&Shown==0?FString::Printf(TEXT("No accounts match \"%s\". Press Esc to clear."),*Search):OtherItems.IsEmpty()?TEXT("All matching accounts are favorites."):FString();
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
	if (SelectedAccountName) SelectedAccountName->SetText(FText::FromString(A?(A->Alias.TrimStartAndEnd().IsEmpty()?A->Username:A->Alias):TEXT("No account selected")));
	if (LaunchCaption)
	{
		const FACELoginServer* S=Profile.SelectedServer();
		LaunchCaption->SetText(FText::FromString(A&&S?FString::Printf(TEXT("%s  on  %s"),*(A->Alias.TrimStartAndEnd().IsEmpty()?A->Username:A->Alias),*S->Name):!S?TEXT("Choose a world first"):TEXT("Select an account to launch")));
	}
	RefreshLaunchState();
	if (!LastLaunchedText) return;
	FString When;
	if (!A) When=Profile.Accounts.IsEmpty()?TEXT("Use + Add to save a login."):TEXT("Pick an account from the list.");
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
	// The left account list is always on screen; drags are off while a search filter
	// hides rows (the drop position would be ambiguous) or a removal awaits confirmation.
	return Pages && PendingRemoval.IsEmpty() && !IsConnectScreenVisible()
		&& (!AccountSearchBox || AccountSearchBox->GetText().ToString().TrimStartAndEnd().IsEmpty());
}

bool UACELoginWidget::CanMoveAccount(const FString& Id, const FString& TargetId) const
{
	if (!CanReorderAccounts() || Id==TargetId) return false;
	const auto* A=Profile.Accounts.FindByPredicate([&](const auto& E){return E.Id==Id;});
	const auto* T=Profile.Accounts.FindByPredicate([&](const auto& E){return E.Id==TargetId;});
	// Favorites and All accounts are separate groups; the star moves between them.
	return A && T && A->bFavorite==T->bFavorite;
}

bool UACELoginWidget::MoveAccountInOrder(const FString& Id, const FString& TargetId, bool bAfter)
{
	if (!CanMoveAccount(Id,TargetId)) return false;
	// A drag starts from what is on screen: under A-Z or Recent, adopt that order
	// first, then switch the list to Custom so the dropped position sticks.
	TArray<FACELoginAccount> Order=Profile.Accounts;
	const bool bCustom=Profile.AccountSort==TEXT("custom");
	if (!bCustom)
	{
		auto ByName=[](const FACELoginAccount& X, const FACELoginAccount& Y){ return X.DisplayName().Compare(Y.DisplayName(),ESearchCase::IgnoreCase)<0; };
		if (Profile.AccountSort==TEXT("recent")) Algo::StableSort(Order,[ByName](const FACELoginAccount& X, const FACELoginAccount& Y){ return X.LastUsed!=Y.LastUsed?X.LastUsed>Y.LastUsed:ByName(X,Y); });
		else Algo::StableSort(Order,ByName);
	}
	const int32 From=Order.IndexOfByPredicate([&](const auto& A){return A.Id==Id;});
	const int32 Target=Order.IndexOfByPredicate([&](const auto& A){return A.Id==TargetId;});
	if (From==INDEX_NONE || Target==INDEX_NONE) return false;
	int32 To=Target+(bAfter?1:0);
	if (From<To) --To;
	if (From==To) return false;
	// Move the entire identity; usernames need not be unique. Leave selection
	// and any unsaved form edits alone, and use the existing profile format.
	FACELoginAccount Account=MoveTemp(Order[From]);
	Order.RemoveAt(From);
	Order.Insert(MoveTemp(Account),To);
	Profile.Accounts=MoveTemp(Order);
	Profile.AccountSort=TEXT("custom");
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
	ButtonStyle(ACETypeButton,!bEditingGDLE); ButtonStyle(GDLETypeButton,bEditingGDLE);
	EditingTint=S?S->Tint:-1; RefreshTintButtons();
	Pages->SetActiveWidgetIndex(1);
	SetStatus(ServerTypeHint);
}

void UACELoginWidget::DoLogin()
{
	SetPasswordVisible(false);
	if (Updater && Updater->IsBusy()) { SetStatus(TEXT("Finish or cancel the update before logging in.")); return; }
	// Check both files before authenticating: the cell index is deliberately
	// deferred, so portal readiness alone can still lead to an empty world.
	if (!ValidateGameFiles()) return;
	if (!Profile.SelectedServer()) { SetStatus(TEXT("Choose a server first.")); return; }
	if (!StoreAccount()) { Pages->SetActiveWidgetIndex(0); return; }
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
	auto MakeText=[&](const FString& String, float Px, const FLinearColor& Color, const TCHAR* Weight=TEXT("Regular"))
	{
		auto* T=WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(String)); T->SetFont(FCoreStyle::GetDefaultFontStyle(Weight,Px));
		T->SetColorAndOpacity(Color); T->SetShadowOffset(FVector2D(1,1)); T->SetShadowColorAndOpacity(FLinearColor(0,0,0,.6f));
		T->SetVisibility(ESlateVisibility::HitTestInvisible); T->SetClipping(EWidgetClipping::ClipToBounds); T->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		return T;
	};
	int32 Shown=0;
	for (const auto& S:Directory)
	{
		if (!Search.IsEmpty() && !S.Name.Contains(Search) && !S.Description.Contains(Search) && !S.Host.Contains(Search)) continue;
		++Shown;
		const bool Saved=Profile.Servers.ContainsByPredicate([&](const auto& E){return E.Host.Equals(S.Host,ESearchCase::IgnoreCase) && E.Port==S.Port;});
		const bool bSelected=S.Id==BrowserServerId;
		// One quiet line: accent, type tag, name, then "Saved" when it is already in My Servers.
		auto* B=ActionButton(S.Name,TEXT("directoryselect"),S.Id,bSelected);
		B->SetToolTipText(FText::FromString(S.Name));
		auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
		auto* AccentSlot=Line->AddChildToHorizontalBox(RowAccentBar(WidgetTree,bSelected)); AccentSlot->SetVerticalAlignment(VAlign_Fill); AccentSlot->SetPadding(FMargin(0,1,0,1));
		auto* TagSize=WidgetTree->ConstructWidget<USizeBox>(); TagSize->SetWidthOverride(44); TagSize->SetContent(MakeText(S.bGDLE?TEXT("GDLE"):TEXT("ACE"),10.5f,S.bGDLE?Hex(0x8fa6c4):Hex(0xb79a62),TEXT("Bold")));
		auto* TagSlot=Line->AddChildToHorizontalBox(TagSize); TagSlot->SetVerticalAlignment(VAlign_Center); TagSlot->SetPadding(FMargin(12,0,4,0));
		auto* NameSlot=Line->AddChildToHorizontalBox(MakeText(S.Name,14.5f,bSelected?Hex(0xfff2cc):Hex(0xdccfab)));
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NameSlot->SetVerticalAlignment(VAlign_Center);
		if (Saved) { auto* SavedSlot=Line->AddChildToHorizontalBox(MakeText(TEXT("Saved"),11.5f,Hex(0x8f8166))); SavedSlot->SetVerticalAlignment(VAlign_Center); SavedSlot->SetPadding(FMargin(10,0,2,0)); }
		auto* Height=WidgetTree->ConstructWidget<USizeBox>(); Height->SetHeightOverride(26); Height->SetContent(Line);
		Cast<USizeBoxSlot>(Line->Slot)->SetVerticalAlignment(VAlign_Fill);
		B->SetContent(Height);
		if (auto* ContentSlot=Cast<UButtonSlot>(Height->Slot)) { ContentSlot->SetHorizontalAlignment(HAlign_Fill); ContentSlot->SetVerticalAlignment(VAlign_Fill); }
		Cast<UScrollBoxSlot>(BrowserList->AddChild(B))->SetPadding(FMargin(0,0,0,4));
	}
	if (!BrowserList->GetChildrenCount()) BrowserList->AddChild(MakeText(Directory.IsEmpty()?TEXT("No cached directory. Refresh when connected to the internet."):TEXT("No servers match your search."),14.f,Hex(0xb9a77f)));
	if (BrowserCount) BrowserCount->SetText(FText::FromString(Directory.IsEmpty()?FString():Search.IsEmpty()?FString::Printf(TEXT("%d worlds"),Shown):FString::Printf(TEXT("%d of %d"),Shown,Directory.Num())));
	const auto* S=Directory.FindByPredicate([&](const auto& E){return E.Id==BrowserServerId;});
	if (BrowserName) BrowserName->SetText(FText::FromString(S?S->Name:TEXT("No world selected")));
	if (BrowserMeta) BrowserMeta->SetText(FText::FromString(S?FString::Printf(TEXT("%s  \u00B7  %s:%d"),S->bGDLE?TEXT("GDLE"):TEXT("ACE"),*S->Host,S->Port):FString()));
	BrowserDetail->SetText(FText::FromString(S?(S->Description.IsEmpty()?FString(TEXT("No description provided.")):S->Description.Left(1200)):TEXT("Select a world on the left to see its description and community links.")));
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
	int32 Required=0;
	if (FileRows) FileRows->ClearChildren();
	for (const TCHAR* File:{TEXT("client_portal.dat"),TEXT("client_cell_1.dat"),TEXT("client_highres.dat")})
	{
		const bool bFound=!Dir.IsEmpty() && IFileManager::Get().FileSize(*(Dir/File))>0;
		const bool bOptional=FCString::Strcmp(File,TEXT("client_highres.dat"))==0;
		if (bFound && !bOptional) ++Required;
		if (!FileRows) continue;
		// Quiet row: file name, then Found / Missing (Optional for high-res textures).
		auto* Row=WidgetTree->ConstructWidget<UBorder>(); Row->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.006f,.008f,.016f,.42f),2.f,FLinearColor(FColor(0x6b,0x56,0x30,0x80)),1.f));
		Row->SetPadding(FMargin(12,7,12,7));
		auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>(); Row->SetContent(Line);
		auto Txt=[&](const FString& String, float Px, const FLinearColor& Color, const TCHAR* Weight)
		{
			auto* T=WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(String)); T->SetFont(FCoreStyle::GetDefaultFontStyle(Weight,Px));
			T->SetColorAndOpacity(Color); T->SetShadowOffset(FVector2D(1,1)); T->SetShadowColorAndOpacity(FLinearColor(0,0,0,.6f)); return T;
		};
		auto* NameSlot=Line->AddChildToHorizontalBox(Txt(File,14.f,Hex(0xdccfab),TEXT("Regular"))); NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NameSlot->SetVerticalAlignment(VAlign_Center);
		auto* StateSlot=Line->AddChildToHorizontalBox(Txt(bFound?TEXT("Found"):bOptional?TEXT("Optional"):TEXT("Missing"),12.f,bFound?Hex(0x9fc48a):bOptional?Hex(0x8f8166):Hex(0xffa05a),TEXT("Bold")));
		StateSlot->SetVerticalAlignment(VAlign_Center);
		FileRows->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,0,0,4));
	}
	FileStatus->SetText(FText::FromString(FileRows
		?FString::Printf(TEXT("%s Portal and cell files are required. High-resolution textures are used when available."),Required==2?TEXT("Ready to play."):TEXT("Required files missing."))
		:FString()));
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
	// Moving to another tab starts with a clean status line.
	if(Action==TEXT("play")||Action==TEXT("accounts")||Action==TEXT("plugins")||Action==TEXT("updates")||Action==TEXT("files")||Action==TEXT("browser"))
		SetStatus(FString());
	// World popover: opened from the plaque; picking a world keeps it open, anything that leaves it closes it.
	if (Action==TEXT("worldmenu")) { SetWorldMenuOpen(!IsWorldMenuOpen()); return; }
	if (Action==TEXT("closeworld")) { SetWorldMenuOpen(false); return; }
	if (Action==TEXT("editserver")||Action==TEXT("newserver")||Action==TEXT("removeserver")||Action==TEXT("browser")||Action==TEXT("website")||Action==TEXT("discord")
		||Action==TEXT("play")||Action==TEXT("accounts")||Action==TEXT("plugins")||Action==TEXT("updates")||Action==TEXT("files")||Action==TEXT("launch"))
		if (IsWorldMenuOpen()) SetWorldMenuOpen(false);
	if (Action==TEXT("tint")) { EditingTint=FMath::Clamp(FCString::Atoi(*Value),0,ACELobbyArt::NumTints()-1); RefreshTintButtons(); return; }
	if (Action==TEXT("quit"))
	{
		SaveLoginEntries();
		UKismetSystemLibrary::QuitGame(this,GetOwningPlayer(),EQuitPreference::Quit,false);
		return;
	}
	if (Action==TEXT("plugins")) { Pages->SetActiveWidgetIndex(5); return; }
	if (Action==TEXT("accounts")) { Pages->SetActiveWidgetIndex(0); return; }
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
	else if (Action==TEXT("selectserver")) SelectServer(Value);
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
		S.Tint=EditingTint;
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
		// The retail frames sit on opaque black (invisible on the black connect screen); key it
		// out so the vial also sits cleanly on the lobby's Updates panel.
		UTexture2D* A=ACELobbyArt::KeyedOverBlack(Dat,ACEVialFullIds[I]); UTexture2D* B=ACELobbyArt::KeyedOverBlack(Dat,ACEVialEmptyIds[I]);
		if (!A) A=Res->ResolveTexture(ACEVialFullIds[I]);
		if (!B) B=Res->ResolveTexture(ACEVialEmptyIds[I]);
		if (!A || !B) return; // Retry on the next poll; the plain bar is drawn meanwhile.
		VialTextures.AddUnique(A); VialTextures.AddUnique(B);
		FSlateBrush Brush; Brush.DrawAs=ESlateBrushDrawType::Image; Brush.ImageSize=FVector2D(VialW,VialH);
		Brush.SetResourceObject(A); Full.Add(Brush);
		Brush.SetResourceObject(B); Empty.Add(Brush);
	}
	VialArt->Full=MoveTemp(Full); VialArt->Empty=MoveTemp(Empty);
}
