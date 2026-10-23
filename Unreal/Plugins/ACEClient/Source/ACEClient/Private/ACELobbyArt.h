#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class UACEDatSubsystem;
class UTexture2D;

/**
 * Retail launcher artwork, decoded once from client_portal.dat after it loads and shared by
 * every lobby widget. Until it is ready the lobby draws flat fallbacks (see ACELoginLobby.cpp).
 */
struct FACELobbyArt
{
	/** 0x06007075 (800x600) with the baked "World"/"Characters" labels painted out. */
	FSlateBrush Background;
	/** 0x06004C9E/9F/A0 idle/hover/pressed red plates, cropped to the plate plus a 7px shadow, 9-slice. */
	FSlateBrush Button[3];
	FSlateBrush SmallButton[3];
	/** 0x06004CB2/CB3 round Enter plate (211x211). */
	FSlateBrush Ring[2];
	/** Gold list frame: corners 0x06004CC3..CC6 (TL, TR, BL, BR), edges CC7 (top/bottom) and CB8 (sides). */
	FSlateBrush Corner[4];
	FSlateBrush EdgeTop, EdgeBottom, EdgeLeft, EdgeRight;
	/** Slot rows 0x06005EA7/EA9/EAB (idle, hover) and 0x06005EB4/EB5/EB6 (selected), stacked at load. */
	FSlateBrush Row[3];
	/** 0x06004D26 window: brown title bar with the silver ring emblem over a blue body. */
	FSlateBrush Window;
	/** 0x060060F6 idle / 0x060060F9 focused search field. */
	FSlateBrush Search[2];
	/** Retail scrollbar: chain track 0x06004C5F, thumb C60/C63/C66 (hover C61/C64/C67), arrows C6A/C6D (hover C6B/C6E). */
	FSlateBrush Track, Thumb[2], ArrowUp[2], ArrowDown[2];

	static constexpr float ButtonShadow = 7.f;
	static constexpr float WindowHeader = 66.f;
};

namespace ACELobbyArt
{
	/** The shared art, or null until the DAT has loaded and decoding has succeeded. */
	const FACELobbyArt* Get();
	/** Decodes the art once the DAT is ready (cheap no-op afterwards). Returns Get(). */
	const FACELobbyArt* Load(UACEDatSubsystem* Dat);
	/**
	 * A new transient texture holding the 800x600 image ImageId with ExtX columns added on each
	 * side and ExtY rows above and below, painted to look like the art simply continues: each
	 * edge is mirrored for a few pixels and cross-faded into stone texture taken from a plain
	 * part of the art, tinted to that edge's own colours. Nothing of the art's figures, logo or
	 * titles is repeated. Spread columns are opened at SplitX (between the left and right
	 * halves of the composition) and filled the same way. Null if the DAT is not ready. The
	 * caller keeps the texture alive.
	 */
	UTexture2D* ExtendedBackdrop(UACEDatSubsystem* Dat, uint32 ImageId, int32 ExtX, int32 ExtY, int32 Spread = 0, int32 SplitX = 212);

	extern const FLinearColor Gold;      // headings, retail orange-gold
	extern const FLinearColor Cream;     // body text
	extern const FLinearColor Muted;     // secondary text
	extern const FLinearColor Highlight; // search matches
	// Character-select text colors (layout 0x21000004), shared so the lobby reads as the same screen.
	extern const FLinearColor CaptionText; // 0xFFFFB219 red-plate button captions
	extern const FLinearColor RowText;     // 0xFFFFF2CC character / account rows
	extern const FLinearColor WorldText;   // 0xFFD8AF5B world name
	extern const FLinearColor EnterText;   // 0xFFFFF27F round Enter caption
}

/** Gold DAT list frame around a translucent panel; a flat gold outline before the art loads. */
class SACELobbyFrame : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SACELobbyFrame) : _Fill(FLinearColor(.008f, .02f, .06f, .55f)) {}
		SLATE_ARGUMENT(FLinearColor, Fill)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(80, 80); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
private:
	FLinearColor Fill;
};

/** Small vector glyphs drawn with Slate lines so they never depend on a font: favourite star,
 *  magnifier, and clear cross. */
class SACELobbyGlyph : public SLeafWidget
{
public:
	enum class EKind : uint8 { Star, Magnifier, Cross };
	SLATE_BEGIN_ARGS(SACELobbyGlyph) : _Kind(EKind::Star), _Size(16.f) {}
		SLATE_ARGUMENT(EKind, Kind)
		SLATE_ARGUMENT(float, Size)
		SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_ATTRIBUTE(bool, Filled)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
private:
	EKind Kind = EKind::Star;
	float Size = 16.f;
	TAttribute<FLinearColor> Color;
	TAttribute<bool> Filled;
};
