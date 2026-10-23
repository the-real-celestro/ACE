#include "ACELobbyArt.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatTextureResolver.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogACELobbyArt, Log, All);

namespace ACELobbyArt
{
	const FLinearColor Gold = FLinearColor(FColor(242, 169, 51));
	const FLinearColor Cream = FLinearColor(FColor(239, 227, 196));
	const FLinearColor Muted = FLinearColor(FColor(191, 174, 136));
	const FLinearColor Highlight = FLinearColor(FColor(255, 215, 106));
	const FLinearColor CaptionText = FLinearColor(FColor(255, 178, 25));
	const FLinearColor RowText = FLinearColor(FColor(255, 242, 204));
	const FLinearColor WorldText = FLinearColor(FColor(216, 175, 91));
	const FLinearColor EnterText = FLinearColor(FColor(255, 242, 127));
}

namespace
{
	struct FImg
	{
		int32 W = 0, H = 0;
		TArray<FColor> Px;
		FColor& At(int32 X, int32 Y) { return Px[Y * W + X]; }
		const FColor& At(int32 X, int32 Y) const { return Px[Y * W + X]; }
		bool IsValid() const { return W > 0 && H > 0 && Px.Num() == W * H; }
	};

	FACELobbyArt* GArt = nullptr;
	int32 GAttempts = 0;

	bool Decode(FACEDatTextureResolver* Resolver, uint32 Id, FImg& Out)
	{
		FACEDatTexture Raw; FACEDatDecodedSurface Surface;
		if (!Resolver->LoadTextureForUi(Id, Raw) || !Resolver->DecodeTextureForUi(Raw, Surface)
			|| Surface.Width <= 0 || Surface.Height <= 0 || Surface.Pixels.Num() != Surface.Width * Surface.Height)
		{
			UE_LOG(LogACELobbyArt, Warning, TEXT("Lobby art 0x%08X could not be decoded"), Id);
			return false;
		}
		Out.W = Surface.Width; Out.H = Surface.Height; Out.Px = MoveTemp(Surface.Pixels);
		return true;
	}

	FImg Crop(const FImg& In, int32 X, int32 Y, int32 W, int32 H)
	{
		FImg Out; Out.W = W; Out.H = H; Out.Px.SetNumZeroed(W * H);
		for (int32 J = 0; J < H; ++J) for (int32 I = 0; I < W; ++I)
			if (X + I >= 0 && X + I < In.W && Y + J >= 0 && Y + J < In.H) Out.At(I, J) = In.At(X + I, Y + J);
		return Out;
	}

	/** Stacks equal-width strips top to bottom (a slot row's top, middle and bottom art). */
	FImg StackV(TArrayView<const FImg* const> Parts)
	{
		FImg Out; Out.W = Parts[0]->W;
		for (const FImg* P : Parts) Out.H += P->H;
		Out.Px.Reserve(Out.W * Out.H);
		for (const FImg* P : Parts)
		{
			check(P->W == Out.W);
			Out.Px.Append(P->Px);
		}
		return Out;
	}

	FImg MirrorX(const FImg& In)
	{
		FImg Out = In;
		for (int32 Y = 0; Y < In.H; ++Y) for (int32 X = 0; X < In.W; ++X) Out.At(X, Y) = In.At(In.W - 1 - X, Y);
		return Out;
	}

	void ScaleAlpha(FImg& Img, float Factor)
	{
		for (FColor& C : Img.Px) C.A = uint8(FMath::Clamp(FMath::RoundToInt(C.A * Factor), 0, 255));
	}

	/** Gold rim and soft inner glow for the selected slot row, so the bevelled art reads as chosen. */
	void AddSelectionGlow(FImg& Img)
	{
		const FColor Rim(255, 214, 110, 255);
		for (int32 Y = 0; Y < Img.H; ++Y) for (int32 X = 0; X < Img.W; ++X)
		{
			const int32 D = FMath::Min(FMath::Min(X, Img.W - 1 - X), FMath::Min(Y, Img.H - 1 - Y));
			const float W = D == 0 ? .95f : D == 1 ? .4f : D == 2 ? .18f : 0.f;
			if (W <= 0) continue;
			FColor& C = Img.At(X, Y);
			C.R = uint8(FMath::Lerp(float(C.R), float(Rim.R), W));
			C.G = uint8(FMath::Lerp(float(C.G), float(Rim.G), W));
			C.B = uint8(FMath::Lerp(float(C.B), float(Rim.B), W));
			C.A = uint8(FMath::Max(float(C.A), 255.f * W));
		}
	}

	/** Paints over a rectangle by copying the texture from DY rows below it, feathering the edges. */
	void PaintOut(FImg& Img, const FImg& Source, int32 X0, int32 Y0, int32 X1, int32 Y1, int32 DY, int32 Feather = 8)
	{
		for (int32 Y = FMath::Max(0, Y0 - Feather); Y < FMath::Min(Img.H, Y1 + Feather); ++Y)
			for (int32 X = FMath::Max(0, X0 - Feather); X < FMath::Min(Img.W, X1 + Feather); ++X)
			{
				const int32 SY = Y + DY;
				if (SY < 0 || SY >= Img.H) continue;
				const int32 D = FMath::Max3(X0 - X, X - X1 + 1, 0) > 0 || FMath::Max3(Y0 - Y, Y - Y1 + 1, 0) > 0
					? FMath::Max(FMath::Max3(X0 - X, X - X1 + 1, 0), FMath::Max3(Y0 - Y, Y - Y1 + 1, 0)) : 0;
				const float W = D == 0 ? 1.f : FMath::Max(0.f, 1.f - float(D) / Feather);
				if (W <= 0) continue;
				const FColor A = Source.At(X, Y), B = Source.At(X, SY);
				Img.At(X, Y) = FColor(uint8(FMath::Lerp(float(A.R), float(B.R), W)), uint8(FMath::Lerp(float(A.G), float(B.G), W)),
					uint8(FMath::Lerp(float(A.B), float(B.B), W)), 255);
			}
	}

	UTexture2D* Keep(UTexture2D* Texture)
	{
		// Shared by every launcher instance for the life of the process (the DAT folder
		// cannot change without a restart), so root it rather than tie it to one widget.
		if (Texture) Texture->AddToRoot();
		return Texture;
	}

	UTexture2D* MakeUi(const FImg& Img) { return Keep(FACEDatTextureResolver::CreateTransientRgbaUi(Img.W, Img.H, Img.Px)); }
	UTexture2D* MakeTiled(const FImg& Img)
	{
		return Keep(FACEDatTextureResolver::CreateTransientRgbaWithMips(Img.W, Img.H, Img.Px, true, TA_Wrap, TA_Wrap, false, false, 1, TF_Bilinear, false));
	}
	UTexture2D* MakeMipped(const FImg& Img)
	{
		return Keep(FACEDatTextureResolver::CreateTransientRgbaWithMips(Img.W, Img.H, Img.Px, false, TA_Clamp, TA_Clamp, false, false, 0, TF_Trilinear, false));
	}

	FSlateBrush ImageBrush(UTexture2D* Texture, FVector2D Size, ESlateBrushTileType::Type Tiling = ESlateBrushTileType::NoTile)
	{
		FSlateBrush Brush; Brush.DrawAs = ESlateBrushDrawType::Image; Brush.Tiling = Tiling;
		Brush.SetResourceObject(Texture); Brush.ImageSize = Size;
		return Brush;
	}

	/** Box (9-slice) brush; Margin is in source pixels and is drawn at ImageSize scale. */
	FSlateBrush BoxBrush(UTexture2D* Texture, FVector2D Size, FMargin Pixels, float Scale = 1.f)
	{
		FSlateBrush Brush; Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.SetResourceObject(Texture); Brush.ImageSize = Size * Scale;
		Brush.Margin = FMargin(Pixels.Left / Size.X, Pixels.Top / Size.Y, Pixels.Right / Size.X, Pixels.Bottom / Size.Y);
		return Brush;
	}
}

const FACELobbyArt* ACELobbyArt::Get() { return GArt; }

UTexture2D* ACELobbyArt::BlurredBackdrop(UACEDatSubsystem* Dat, uint32 ImageId)
{
	static TMap<uint32, UTexture2D*> Cache;
	if (UTexture2D** Found = Cache.Find(ImageId)) return *Found;
	if (!ImageId || !Dat || !Dat->IsDatReady() || !Dat->GetTextureResolver()) return nullptr;
	FImg Src;
	if (!Decode(Dat->GetTextureResolver(), ImageId, Src)) { Cache.Add(ImageId, nullptr); return nullptr; }
	// Average 4x4 blocks, then three box-blur passes (close to a Gaussian). The result keeps
	// the art's colours but none of its detail, so a stretched copy never reads as a seam.
	constexpr int32 Down = 4, Radius = 3, Passes = 3;
	const int32 W = FMath::Max(1, Src.W / Down), H = FMath::Max(1, Src.H / Down);
	TArray<FVector3f> A, B; A.SetNumZeroed(W * H); B.SetNumZeroed(W * H);
	for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
	{
		FVector3f Sum(0);
		for (int32 J = 0; J < Down; ++J) for (int32 I = 0; I < Down; ++I)
		{
			const FColor& C = Src.At(FMath::Min(Src.W - 1, X * Down + I), FMath::Min(Src.H - 1, Y * Down + J));
			Sum += FVector3f(C.R, C.G, C.B);
		}
		A[Y * W + X] = Sum / float(Down * Down);
	}
	auto Blur = [&](const TArray<FVector3f>& In, TArray<FVector3f>& Out, bool bHorizontal)
	{
		const int32 N = bHorizontal ? W : H, Lines = bHorizontal ? H : W;
		for (int32 L = 0; L < Lines; ++L) for (int32 P = 0; P < N; ++P)
		{
			FVector3f Sum(0);
			for (int32 K = -Radius; K <= Radius; ++K)
			{
				const int32 Q = FMath::Clamp(P + K, 0, N - 1);
				Sum += In[bHorizontal ? L * W + Q : Q * W + L];
			}
			Out[bHorizontal ? L * W + P : P * W + L] = Sum / float(2 * Radius + 1);
		}
	};
	for (int32 Pass = 0; Pass < Passes; ++Pass) { Blur(A, B, true); Blur(B, A, false); }
	// Darken toward the night blue behind the panel so the live screen stays the focus.
	const FVector3f Night(2.f, 5.f, 14.f);
	FImg Out; Out.W = W; Out.H = H; Out.Px.SetNumUninitialized(W * H);
	for (int32 I = 0; I < W * H; ++I)
	{
		const FVector3f C = A[I] * .5f + Night * .5f;
		Out.Px[I] = FColor(uint8(FMath::Clamp(FMath::RoundToInt(C.X), 0, 255)), uint8(FMath::Clamp(FMath::RoundToInt(C.Y), 0, 255)),
			uint8(FMath::Clamp(FMath::RoundToInt(C.Z), 0, 255)), 255);
	}
	UTexture2D* Texture = MakeUi(Out);
	Cache.Add(ImageId, Texture);
	return Texture;
}

const FACELobbyArt* ACELobbyArt::Load(UACEDatSubsystem* Dat)
{
	if (GArt || !Dat || !Dat->IsDatReady() || GAttempts >= 3) return GArt;
	FACEDatTextureResolver* Resolver = Dat->GetTextureResolver();
	if (!Resolver) return nullptr;
	++GAttempts;
	TMap<uint32, FImg> Src;
	const uint32 Ids[] = {0x06007075u, 0x06004C9Eu, 0x06004C9Fu, 0x06004CA0u, 0x06004CB2u, 0x06004CB3u,
		0x06004CC3u, 0x06004CC4u, 0x06004CC5u, 0x06004CC6u, 0x06004CC7u, 0x06004CB8u,
		0x06005EA7u, 0x06005EA9u, 0x06005EABu, 0x06005EB4u, 0x06005EB5u, 0x06005EB6u, 0x06004D26u,
		0x060060F6u, 0x060060F9u, 0x06004C5Fu, 0x06004C60u, 0x06004C61u, 0x06004C63u, 0x06004C64u,
		0x06004C66u, 0x06004C67u, 0x06004C6Au, 0x06004C6Bu, 0x06004C6Du, 0x06004C6Eu};
	for (uint32 Id : Ids) if (!Decode(Resolver, Id, Src.Add(Id))) return nullptr;
	auto Rows = [&](std::initializer_list<uint32> Parts, bool& bOk)
	{
		TArray<const FImg*> P; for (uint32 Id : Parts) P.Add(&Src[Id]);
		for (const FImg* I : P) bOk &= I->W == P[0]->W;
		return bOk ? StackV(P) : FImg();
	};
	bool bOk = true;
	FImg RowIdle = Rows({0x06005EA7u, 0x06005EA9u, 0x06005EABu}, bOk), RowSelected = Rows({0x06005EB4u, 0x06005EB5u, 0x06005EB6u}, bOk);
	FImg Thumb = Rows({0x06004C60u, 0x06004C63u, 0x06004C66u}, bOk), ThumbHover = Rows({0x06004C61u, 0x06004C64u, 0x06004C67u}, bOk);
	const FImg& Bg = Src[0x06007075u];
	if (!bOk || Bg.W != 800 || Bg.H != 600 || Src[0x06004C9Eu].W < 151 || Src[0x06004C9Eu].H < 79)
	{
		UE_LOG(LogACELobbyArt, Warning, TEXT("Lobby art has unexpected dimensions; using the flat launcher style"));
		GAttempts = 3;
		return nullptr;
	}

	auto* Art = new FACELobbyArt();
	// Retail bakes its own "World"/"Characters" captions into this backdrop; this lobby
	// draws live controls there, so paint the captions out with the texture just below.
	FImg Patched = Bg;
	PaintOut(Patched, Bg, 65, 19, 171, 58, 42);
	PaintOut(Patched, Bg, 29, 155, 202, 194, 45);
	Art->Background = ImageBrush(MakeMipped(Patched), FVector2D(800, 600));

	const FMargin ButtonSlice(24, 24, 24, 24);
	for (int32 I = 0; I < 3; ++I)
	{
		UTexture2D* Plate = MakeUi(Crop(Src[0x06004C9Eu + I], 20, 19, 131, 60));
		Art->Button[I] = BoxBrush(Plate, FVector2D(131, 60), ButtonSlice);
		Art->SmallButton[I] = BoxBrush(Plate, FVector2D(131, 60), ButtonSlice, .62f);
	}
	for (int32 I = 0; I < 2; ++I)
	{
		const FImg& Ring = Src[I ? 0x06004CB3u : 0x06004CB2u];
		Art->Ring[I] = ImageBrush(MakeMipped(Ring), FVector2D(Ring.W, Ring.H));
	}
	for (int32 I = 0; I < 4; ++I)
	{
		const FImg& Corner = Src[0x06004CC3u + I];
		Art->Corner[I] = ImageBrush(MakeUi(Corner), FVector2D(Corner.W, Corner.H));
	}
	const FImg& EdgeH = Src[0x06004CC7u]; const FImg& EdgeV = Src[0x06004CB8u];
	Art->EdgeTop = Art->EdgeBottom = ImageBrush(MakeTiled(EdgeH), FVector2D(EdgeH.W, EdgeH.H), ESlateBrushTileType::Horizontal);
	Art->EdgeLeft = ImageBrush(MakeTiled(EdgeV), FVector2D(EdgeV.W, EdgeV.H), ESlateBrushTileType::Vertical);
	Art->EdgeRight = ImageBrush(MakeTiled(MirrorX(EdgeV)), FVector2D(EdgeV.W, EdgeV.H), ESlateBrushTileType::Vertical);

	const FMargin RowSlice(3, 4, 3, 4);
	const FVector2D RowSize(RowIdle.W, RowIdle.H);
	FImg Idle = RowIdle; ScaleAlpha(Idle, .55f);
	FImg Hover = RowIdle; ScaleAlpha(Hover, .85f);
	FImg Selected = RowSelected; AddSelectionGlow(Selected);
	Art->Row[0] = BoxBrush(MakeUi(Idle), RowSize, RowSlice);
	Art->Row[1] = BoxBrush(MakeUi(Hover), RowSize, RowSlice);
	Art->Row[2] = BoxBrush(MakeUi(Selected), FVector2D(Selected.W, Selected.H), RowSlice);

	const FImg& Window = Src[0x06004D26u];
	Art->Window = BoxBrush(MakeUi(Window), FVector2D(Window.W, Window.H), FMargin(80, FACELobbyArt::WindowHeader, 60, 4));
	for (int32 I = 0; I < 2; ++I)
	{
		const FImg& Field = Src[I ? 0x060060F9u : 0x060060F6u];
		Art->Search[I] = BoxBrush(MakeUi(Field), FVector2D(Field.W, Field.H), FMargin(34, 14, 34, 14));
	}
	const FImg& Track = Src[0x06004C5Fu];
	Art->Track = ImageBrush(MakeTiled(Track), FVector2D(Track.W, Track.H), ESlateBrushTileType::Vertical);
	Art->Thumb[0] = BoxBrush(MakeUi(Thumb), FVector2D(Thumb.W, Thumb.H), FMargin(0, 3, 0, 3));
	Art->Thumb[1] = BoxBrush(MakeUi(ThumbHover), FVector2D(ThumbHover.W, ThumbHover.H), FMargin(0, 3, 0, 3));
	for (int32 I = 0; I < 2; ++I)
	{
		const FImg& Up = Src[0x06004C6Au + I]; const FImg& Down = Src[0x06004C6Du + I];
		Art->ArrowUp[I] = ImageBrush(MakeUi(Up), FVector2D(Up.W, Up.H));
		Art->ArrowDown[I] = ImageBrush(MakeUi(Down), FVector2D(Down.W, Down.H));
	}
	GArt = Art;
	UE_LOG(LogACELobbyArt, Log, TEXT("Retail lobby art ready"));
	return GArt;
}

void SACELobbyFrame::Construct(const FArguments& Args)
{
	Fill = Args._Fill;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SACELobbyFrame::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool) const
{
	const FVector2D S = G.GetLocalSize();
	if (S.X < 2 || S.Y < 2) return Layer;
	const FLinearColor Tint = Style.GetColorAndOpacityTint();
	auto Box = [&](const FSlateBrush* Brush, FVector2D Pos, FVector2D Size, FLinearColor Color, int32 L)
	{
		if (Size.X > 0 && Size.Y > 0)
			FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), Brush, ESlateDrawEffect::None, Color * Tint);
	};
	Box(FCoreStyle::Get().GetBrush("WhiteBrush"), FVector2D(4, 4), S - FVector2D(8, 8), Fill, Layer);
	const FACELobbyArt* Art = ACELobbyArt::Get();
	if (!Art || S.X < 70 || S.Y < 70)
	{
		static const FSlateRoundedBoxBrush Outline(FLinearColor::Transparent, 0.f, FLinearColor(.62f, .43f, .16f, 1), 1.f);
		Box(&Outline, FVector2D::ZeroVector, S, FLinearColor::White, Layer + 1);
		return Layer + 1;
	}
	const int32 L = Layer + 1;
	Box(&Art->EdgeTop, FVector2D(34, 0), FVector2D(S.X - 68, 7), FLinearColor::White, L);
	Box(&Art->EdgeBottom, FVector2D(34, S.Y - 7), FVector2D(S.X - 68, 7), FLinearColor::White, L);
	Box(&Art->EdgeLeft, FVector2D(0, 34), FVector2D(5, S.Y - 69), FLinearColor::White, L);
	Box(&Art->EdgeRight, FVector2D(S.X - 5, 34), FVector2D(5, S.Y - 69), FLinearColor::White, L);
	Box(&Art->Corner[0], FVector2D(0, 0), Art->Corner[0].ImageSize, FLinearColor::White, L);
	Box(&Art->Corner[1], FVector2D(S.X - Art->Corner[1].ImageSize.X, 0), Art->Corner[1].ImageSize, FLinearColor::White, L);
	Box(&Art->Corner[2], FVector2D(0, S.Y - Art->Corner[2].ImageSize.Y), Art->Corner[2].ImageSize, FLinearColor::White, L);
	Box(&Art->Corner[3], S - Art->Corner[3].ImageSize, Art->Corner[3].ImageSize, FLinearColor::White, L);
	return L;
}

void SACELobbyGlyph::Construct(const FArguments& Args)
{
	Kind = Args._Kind; Size = Args._Size; Color = Args._Color; Filled = Args._Filled;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SACELobbyGlyph::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const double Scale = FMath::Min(G.GetLocalSize().X, G.GetLocalSize().Y) / 16.0;
	if (Scale <= 0) return Layer;
	const FGeometry Art = G.MakeChild(FVector2D(16, 16), FSlateLayoutTransform(Scale, (G.GetLocalSize() - FVector2D(16, 16) * Scale) * .5));
	const auto Geo = Art.ToPaintGeometry();
	const auto Effect = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FLinearColor Tint = Color.Get(FLinearColor::White) * Style.GetColorAndOpacityTint();
	if (Kind == EKind::Star)
	{
		TArray<FVector2D> Points;
		for (int32 I = 0; I <= 10; ++I)
		{
			const double A = -HALF_PI + PI * I / 5.0, R = (I % 2) ? 3.1 : 7.4;
			Points.Add(FVector2D(8 + R * FMath::Cos(A), 8.6 + R * FMath::Sin(A)));
		}
		// Scanline fill keeps this font-free; outline on top keeps the points crisp.
		if (Filled.Get(false))
			for (double Y = 1.0; Y <= 15.5; Y += .5)
			{
				TArray<double> Xs;
				for (int32 I = 0; I < 10; ++I)
				{
					const FVector2D& P = Points[I]; const FVector2D& Q = Points[I + 1];
					if ((P.Y <= Y && Q.Y > Y) || (Q.Y <= Y && P.Y > Y)) Xs.Add(P.X + (Y - P.Y) * (Q.X - P.X) / (Q.Y - P.Y));
				}
				Xs.Sort();
				for (int32 I = 0; I + 1 < Xs.Num(); I += 2)
					FSlateDrawElement::MakeLines(Out, Layer, Geo, {FVector2D(Xs[I], Y), FVector2D(Xs[I + 1], Y)}, Effect, Tint, true, .8f);
			}
		FSlateDrawElement::MakeLines(Out, Layer + 1, Geo, Points, Effect, Tint, true, 1.f);
		return Layer + 1;
	}
	if (Kind == EKind::Magnifier)
	{
		TArray<FVector2D> Circle;
		for (int32 I = 0; I <= 24; ++I) { const double A = 2 * PI * I / 24; Circle.Add(FVector2D(6.5 + 4.5 * FMath::Cos(A), 6.5 + 4.5 * FMath::Sin(A))); }
		FSlateDrawElement::MakeLines(Out, Layer, Geo, Circle, Effect, Tint, true, 1.6f);
		FSlateDrawElement::MakeLines(Out, Layer, Geo, {FVector2D(9.9, 9.9), FVector2D(14.5, 14.5)}, Effect, Tint, true, 2.2f);
		return Layer;
	}
	FSlateDrawElement::MakeLines(Out, Layer, Geo, {FVector2D(3.5, 3.5), FVector2D(12.5, 12.5)}, Effect, Tint, true, 1.8f);
	FSlateDrawElement::MakeLines(Out, Layer, Geo, {FVector2D(12.5, 3.5), FVector2D(3.5, 12.5)}, Effect, Tint, true, 1.8f);
	return Layer;
}
