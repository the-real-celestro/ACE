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

namespace
{
	using FPlane3 = TArray<FVector3f>;

	/** Separable box blur, Passes times (three passes approximate a Gaussian), edges clamped. */
	void BlurPlane(FPlane3& V, int32 W, int32 H, int32 R, int32 Passes)
	{
		if (R <= 0) return;
		FPlane3 T; T.SetNumUninitialized(V.Num());
		const float Inv = 1.f / float(2 * R + 1);
		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			for (int32 Y = 0; Y < H; ++Y)
			{
				FVector3f Sum(0);
				for (int32 K = -R; K <= R; ++K) Sum += V[Y * W + FMath::Clamp(K, 0, W - 1)];
				for (int32 X = 0; X < W; ++X)
				{
					T[Y * W + X] = Sum * Inv;
					Sum += V[Y * W + FMath::Min(X + R + 1, W - 1)] - V[Y * W + FMath::Max(X - R, 0)];
				}
			}
			for (int32 X = 0; X < W; ++X)
			{
				FVector3f Sum(0);
				for (int32 K = -R; K <= R; ++K) Sum += T[FMath::Clamp(K, 0, H - 1) * W + X];
				for (int32 Y = 0; Y < H; ++Y)
				{
					V[Y * W + X] = Sum * Inv;
					Sum += T[FMath::Min(Y + R + 1, H - 1) * W + X] - T[FMath::Max(Y - R, 0) * W + X];
				}
			}
		}
	}

	/**
	 * Removes the "Asheron's Call" logo baked into the top right of the backdrop (this lobby draws the
	 * crest itself, top left). Tone is diffused in from the surrounding stone; grain is the art's own
	 * high-pass detail taken from mirrored strips of the plain left half, scaled to the new tone.
	 */
	void PaintOutLogo(FImg& Img)
	{
		const int32 W = Img.W, H = Img.H;
		constexpr int32 X0 = 262, Y0 = 0, X1 = 799, Y1 = 170, F = 16, S = 4, Strip = 268;
		if (W != 800 || H != 600) return;
		auto Weight = [&](int32 X, int32 Y)
		{
			const int32 D = FMath::Max(FMath::Max3(X0 - X, X - X1, 0), FMath::Max3(Y0 - Y, Y - Y1, 0));
			return D == 0 ? 1.f : FMath::Max(0.f, 1.f - float(D) / F);
		};
		FPlane3 Src; Src.SetNumUninitialized(W * H);
		for (int32 I = 0; I < W * H; ++I) Src[I] = FVector3f(Img.Px[I].R, Img.Px[I].G, Img.Px[I].B);
		// Tone: Laplace fill of the masked cells on a 4x downsample, then smoothed back up.
		const int32 SW = W / S, SH = H / S;
		FPlane3 Small; Small.SetNumZeroed(SW * SH);
		TArray<uint8> Masked; Masked.SetNumZeroed(SW * SH);
		for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
		{
			Small[(Y / S) * SW + X / S] += Src[Y * W + X] / float(S * S);
			if (Weight(X, Y) > 0) Masked[(Y / S) * SW + X / S] = 1;
		}
		TArray<int32> Cells;
		for (int32 I = 0; I < SW * SH; ++I) if (Masked[I]) Cells.Add(I);
		for (int32 Iter = 0; Iter < 1500; ++Iter)
			for (int32 I : Cells)
			{
				const int32 X = I % SW, Y = I / SW;
				Small[I] = (Small[Y * SW + FMath::Max(X - 1, 0)] + Small[Y * SW + FMath::Min(X + 1, SW - 1)]
					+ Small[FMath::Max(Y - 1, 0) * SW + X] + Small[FMath::Min(Y + 1, SH - 1) * SW + X]) * .25f;
			}
		FPlane3 Tone; Tone.SetNumUninitialized(W * H);
		for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
		{
			const float FX = FMath::Clamp((X + .5f) / S - .5f, 0.f, SW - 1.f), FY = FMath::Clamp((Y + .5f) / S - .5f, 0.f, SH - 1.f);
			const int32 IX = FMath::Min(int32(FX), SW - 2), IY = FMath::Min(int32(FY), SH - 2);
			const float TX = FX - IX, TY = FY - IY;
			Tone[Y * W + X] = FMath::Lerp(FMath::Lerp(Small[IY * SW + IX], Small[IY * SW + IX + 1], TX),
				FMath::Lerp(Small[(IY + 1) * SW + IX], Small[(IY + 1) * SW + IX + 1], TX), TY);
		}
		BlurPlane(Tone, W, H, 3, 3);
		FPlane3 Soft = Src; BlurPlane(Soft, W, H, 4, 3);
		FPlane3 Broad = Src; BlurPlane(Broad, W, H, 12, 3);
		for (int32 Y = FMath::Max(0, Y0 - F); Y <= FMath::Min(H - 1, Y1 + F); ++Y)
			for (int32 X = FMath::Max(0, X0 - F); X < W; ++X)
			{
				const float Wt = Weight(X, Y);
				if (Wt <= 0) continue;
				// Floor division, so the feather left of the rect continues the first mirrored strip.
				const int32 SX = X - X0, K = SX >= 0 ? SX / Strip : -1, R = ((SX % Strip) + Strip) % Strip;
				const int32 SrcX = FMath::Clamp(K >= 0 && K % 2 == 0 ? Strip - 1 - R : R, 0, W - 1);
				const int32 I = Y * W + X, J = Y * W + SrcX;
				const FVector3f& T = Tone[I];
				const float Mean = (T.X + T.Y + T.Z) / 3.f, SrcMean = FMath::Max(1.f, (Broad[J].X + Broad[J].Y + Broad[J].Z) / 3.f);
				const float Gain = FMath::Clamp(Mean / SrcMean, .6f, 2.2f);
				const FVector3f Out = FMath::Lerp(Src[I], T + (Src[J] - Soft[J]) * Gain, Wt);
				Img.Px[I] = FColor(uint8(FMath::Clamp(FMath::RoundToInt(Out.X), 0, 255)), uint8(FMath::Clamp(FMath::RoundToInt(Out.Y), 0, 255)),
					uint8(FMath::Clamp(FMath::RoundToInt(Out.Z), 0, 255)), 255);
			}
	}

	/** CSS-style hue-rotate x saturate x brightness, in 8-bit sRGB like the mockups. */
	FMatrix44f TintMatrix(const FACELobbyTint& T)
	{
		const float A = FMath::DegreesToRadians(T.Hue), C = FMath::Cos(A), S = FMath::Sin(A), Sa = T.Saturation;
		const float Hm[3][3] = {
			{.213f + C * .787f - S * .213f, .715f - C * .715f - S * .715f, .072f - C * .072f + S * .928f},
			{.213f - C * .213f + S * .143f, .715f + C * .285f + S * .140f, .072f - C * .072f - S * .283f},
			{.213f - C * .213f - S * .787f, .715f - C * .715f + S * .715f, .072f + C * .928f + S * .072f}};
		const float Sm[3][3] = {
			{.213f + .787f * Sa, .715f - .715f * Sa, .072f - .072f * Sa},
			{.213f - .213f * Sa, .715f + .285f * Sa, .072f - .072f * Sa},
			{.213f - .213f * Sa, .715f - .715f * Sa, .072f + .928f * Sa}};
		FMatrix44f M(EForceInit::ForceInitToZero);
		for (int32 R = 0; R < 3; ++R) for (int32 Col = 0; Col < 3; ++Col)
		{
			float V = 0; for (int32 K = 0; K < 3; ++K) V += Sm[R][K] * Hm[K][Col];
			M.M[R][Col] = V * T.Brightness;
		}
		return M;
	}

	/** Applies the tint; bSelective limits it to blue pixels so gold and brown stay put. */
	FImg ApplyTint(const FImg& In, const FACELobbyTint& T, bool bSelective)
	{
		FImg Out = In;
		if (T.Hue == 0 && T.Saturation == 1 && T.Brightness == 1) return Out;
		const FMatrix44f M = TintMatrix(T);
		for (FColor& P : Out.Px)
		{
			const float R = P.R, G = P.G, B = P.B;
			float Wt = 1.f;
			if (bSelective) Wt = FMath::Clamp((B - FMath::Max(R, G)) / 24.f, 0.f, 1.f);
			if (Wt <= 0) continue;
			const float NR = M.M[0][0] * R + M.M[0][1] * G + M.M[0][2] * B;
			const float NG = M.M[1][0] * R + M.M[1][1] * G + M.M[1][2] * B;
			const float NB = M.M[2][0] * R + M.M[2][1] * G + M.M[2][2] * B;
			P.R = uint8(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(R, NR, Wt)), 0, 255));
			P.G = uint8(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(G, NG, Wt)), 0, 255));
			P.B = uint8(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(B, NB, Wt)), 0, 255));
		}
		return Out;
	}

	/** A W x H mask: Alpha(u, v) in [0,1] over normalised coordinates, colour fixed. */
	FImg MakeMask(int32 W, int32 H, FColor Color, TFunctionRef<float(float, float)> Alpha)
	{
		FImg Img; Img.W = W; Img.H = H; Img.Px.SetNumUninitialized(W * H);
		for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
		{
			FColor C = Color; C.A = uint8(FMath::Clamp(FMath::RoundToInt(Alpha((X + .5f) / W, (Y + .5f) / H) * 255.f), 0, 255));
			Img.At(X, Y) = C;
		}
		return Img;
	}

	// Sources kept for the tinted copies (built lazily, one per palette entry actually shown).
	FImg GPatchedBg, GWindowBody, GRingHot;
	TMap<int32, FSlateBrush> GTintedBg, GTintedBody, GTintedRing;

	const FACELobbyTint GTints[] = {
		{TEXT("Retail blue"), 0.f, 1.f, 1.f, FLinearColor(FColor(0x3d, 0x6f, 0xd6)), FLinearColor(FColor(6, 14, 34, 184)), FLinearColor(FColor(10, 22, 48, 140)),
			FLinearColor(FColor(12, 22, 44, 247)), FLinearColor(FColor(5, 10, 22, 250))},
		{TEXT("Reef teal"), -42.f, 1.15f, 1.04f, FLinearColor(FColor(0x2f, 0xb7, 0xa4)), FLinearColor(FColor(3, 24, 28, 189)), FLinearColor(FColor(6, 40, 44, 140)),
			FLinearColor(FColor(6, 36, 40, 247)), FLinearColor(FColor(3, 16, 20, 250))},
		{TEXT("Violet"), 40.f, 1.05f, 1.05f, FLinearColor(FColor(0x9a, 0x6e, 0xe8)), FLinearColor(FColor(18, 10, 34, 189)), FLinearColor(FColor(30, 18, 52, 140)),
			FLinearColor(FColor(26, 14, 46, 247)), FLinearColor(FColor(12, 6, 22, 250))},
		{TEXT("Crimson"), 160.f, 1.f, 1.05f, FLinearColor(FColor(0xd0, 0x46, 0x3c)), FLinearColor(FColor(30, 8, 8, 189)), FLinearColor(FColor(48, 14, 12, 140)),
			FLinearColor(FColor(44, 12, 10, 247)), FLinearColor(FColor(20, 5, 4, 250))},
		{TEXT("Emerald"), -78.f, 1.05f, 1.f, FLinearColor(FColor(0x3f, 0xbf, 0x6a)), FLinearColor(FColor(4, 26, 14, 189)), FLinearColor(FColor(8, 42, 24, 140)),
			FLinearColor(FColor(7, 38, 20, 247)), FLinearColor(FColor(3, 16, 8, 250))},
		{TEXT("Steel"), 0.f, .3f, 1.f, FLinearColor(FColor(0x9a, 0xa8, 0xba)), FLinearColor(FColor(14, 16, 20, 189)), FLinearColor(FColor(24, 28, 34, 140)),
			FLinearColor(FColor(22, 25, 30, 247)), FLinearColor(FColor(9, 10, 13, 250))},
	};

	const FSlateBrush* Tinted(TMap<int32, FSlateBrush>& Cache, const FImg& Src, int32 Index, bool bSelective, bool bMipped,
		ESlateBrushTileType::Type Tiling = ESlateBrushTileType::NoTile)
	{
		if (!GArt || !Src.IsValid()) return nullptr;
		Index = ((Index % ACELobbyArt::NumTints()) + ACELobbyArt::NumTints()) % ACELobbyArt::NumTints();
		if (const FSlateBrush* Found = Cache.Find(Index)) return Found;
		const FImg Out = ApplyTint(Src, ACELobbyArt::Tint(Index), bSelective);
		UTexture2D* Texture = Tiling != ESlateBrushTileType::NoTile ? MakeTiled(Out) : bMipped ? MakeMipped(Out) : MakeUi(Out);
		return &Cache.Add(Index, ImageBrush(Texture, FVector2D(Out.W, Out.H), Tiling));
	}
}

int32 ACELobbyArt::NumTints() { return UE_ARRAY_COUNT(GTints); }
const FACELobbyTint& ACELobbyArt::Tint(int32 Index) { return GTints[((Index % NumTints()) + NumTints()) % NumTints()]; }
const FSlateBrush* ACELobbyArt::TintedBackground(int32 Index) { return Tinted(GTintedBg, GPatchedBg, Index, false, true); }
const FSlateBrush* ACELobbyArt::TintedWindowBody(int32 Index) { return Tinted(GTintedBody, GWindowBody, Index, false, false, ESlateBrushTileType::Both); }
const FSlateBrush* ACELobbyArt::TintedRingHot(int32 Index) { return Tinted(GTintedRing, GRingHot, Index, true, true); }

const FACELobbyArt* ACELobbyArt::Get() { return GArt; }

UTexture2D* ACELobbyArt::KeyedOverBlack(UACEDatSubsystem* Dat, uint32 ImageId)
{
	if (!ImageId || !Dat || !Dat->IsDatReady() || !Dat->GetTextureResolver()) return nullptr;
	FImg Img;
	if (!Decode(Dat->GetTextureResolver(), ImageId, Img) || !Img.IsValid()) return nullptr;
	// Flood from the border through dark pixels: that region is the black backing (and the
	// dark anti-aliased fringe against it); enclosed dark glass stays opaque.
	constexpr int32 Lo = 6, Hi = 48;
	auto Level = [](const FColor& C) { return FMath::Max3<int32>(C.R, C.G, C.B); };
	TArray<uint8> Seen; Seen.SetNumZeroed(Img.W * Img.H);
	TArray<int32> Stack;
	auto Push = [&](int32 X, int32 Y)
	{
		if (X < 0 || Y < 0 || X >= Img.W || Y >= Img.H) return;
		const int32 I = Y * Img.W + X;
		if (Seen[I] || Level(Img.Px[I]) >= Hi) return;
		Seen[I] = 1; Stack.Add(I);
	};
	for (int32 X = 0; X < Img.W; ++X) { Push(X, 0); Push(X, Img.H - 1); }
	for (int32 Y = 0; Y < Img.H; ++Y) { Push(0, Y); Push(Img.W - 1, Y); }
	int32 Keyed = 0;
	while (Stack.Num())
	{
		const int32 I = Stack.Pop(EAllowShrinking::No), X = I % Img.W, Y = I / Img.W;
		FColor& C = Img.Px[I];
		const float A = FMath::Clamp(float(Level(C) - Lo) / float(Hi - Lo), 0.f, 1.f) * (C.A / 255.f);
		if (A <= 0.f) C = FColor(0, 0, 0, 0);
		else
		{
			const float K = (C.A / 255.f) / A; // Over black: (C*K)*A == C*(srcA), so nothing shifts.
			C = FColor(uint8(FMath::Min(255.f, C.R * K)), uint8(FMath::Min(255.f, C.G * K)), uint8(FMath::Min(255.f, C.B * K)), uint8(FMath::RoundToInt(A * 255.f)));
		}
		++Keyed;
		Push(X + 1, Y); Push(X - 1, Y); Push(X, Y + 1); Push(X, Y - 1);
	}
	UE_LOG(LogACELobbyArt, Verbose, TEXT("Keyed %d of %d backing pixels in 0x%08X"), Keyed, Img.W * Img.H, ImageId);
	return FACEDatTextureResolver::CreateTransientRgbaUi(Img.W, Img.H, Img.Px);
}

UTexture2D* ACELobbyArt::ExtendedBackdrop(UACEDatSubsystem* Dat, uint32 ImageId, int32 ExtX, int32 ExtY, int32 Spread, int32 SplitX)
{
	static uint32 SrcId = 0;
	static FImg Src;
	if (!ImageId || !Dat || !Dat->IsDatReady() || !Dat->GetTextureResolver()) return nullptr;
	if (SrcId != ImageId)
	{
		FImg Img;
		if (!Decode(Dat->GetTextureResolver(), ImageId, Img)) return nullptr;
		Src = MoveTemp(Img);
		SrcId = ImageId;
	}
	const int32 AW = Src.W, AH = Src.H;
	ExtX = FMath::Clamp(ExtX, 0, 2048);
	ExtY = FMath::Clamp(ExtY, 0, 2048);
	// Plain stone used for texture: the 68 columns between the "Characters" title and the logo
	// (0x06007576). Narrower art (never expected) just mirrors its edges.
	constexpr int32 PatchX = 214, PatchW = 68, Step = 52, Overlap = PatchW - Step;
	const bool bPatch = AW >= PatchX + PatchW && AH >= 32;

	auto V3 = [](const FColor& C) { return FVector3f(C.R, C.G, C.B); };
	auto Reflect = [](int32 V, int32 N) { const int32 M = ((V % (2 * N)) + 2 * N) % (2 * N); return M < N ? M : 2 * N - 1 - M; };
	auto BoxBlur = [](TArray<FVector3f>& V, int32 W, int32 H, int32 R, bool bX, bool bY)
	{
		TArray<FVector3f> T; T.SetNumUninitialized(V.Num());
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			if (bX)
			{
				for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
				{
					FVector3f S(0);
					for (int32 K = -R; K <= R; ++K) S += V[Y * W + FMath::Clamp(X + K, 0, W - 1)];
					T[Y * W + X] = S / float(2 * R + 1);
				}
				V = T;
			}
			if (bY)
			{
				for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
				{
					FVector3f S(0);
					for (int32 K = -R; K <= R; ++K) S += V[FMath::Clamp(Y + K, 0, H - 1) * W + X];
					T[Y * W + X] = S / float(2 * R + 1);
				}
				V = T;
			}
		}
	};

	// High-pass detail of the art (art minus a soft blur): stone grain without its tone.
	TArray<FVector3f> Detail;
	Detail.SetNumUninitialized(AW * AH);
	for (int32 I = 0; I < AW * AH; ++I) Detail[I] = V3(Src.Px[I]);
	{
		TArray<FVector3f> Soft = Detail;
		BoxBlur(Soft, AW, AH, 4, true, true);
		for (int32 I = 0; I < AW * AH; ++I) Detail[I] -= Soft[I];
	}
	auto Rms = [&](int32 X0, int32 Y0, int32 X1, int32 Y1)
	{
		double S = 0; int32 N = 0;
		for (int32 Y = FMath::Max(0, Y0); Y < FMath::Min(AH, Y1); ++Y) for (int32 X = FMath::Max(0, X0); X < FMath::Min(AW, X1); ++X)
		{
			S += Detail[Y * AW + X].SizeSquared(); ++N;
		}
		return N ? float(FMath::Sqrt(S / N)) : 0.f;
	};
	const float PatchRms = bPatch ? FMath::Max(1.f, Rms(PatchX, 0, PatchX + PatchW, AH)) : 1.f;
	auto Gain = [&](float EdgeRms) { return bPatch ? FMath::Clamp(EdgeRms / PatchRms, .4f, 1.6f) : 0.f; };
	const float GainL = Gain(Rms(0, 0, 20, AH)), GainR = Gain(Rms(AW - 10, 0, AW, AH));
	const float GainT = Gain(Rms(0, 0, AW, 10)), GainB = Gain(Rms(0, AH - 10, AW, AH));

	// Each edge's own colour, row by row (or column by column), softened so it reads as tone.
	auto EdgeTone = [&](bool bRows, int32 From, int32 To)
	{
		const int32 N = bRows ? AH : AW;
		TArray<FVector3f> Tone; Tone.SetNumZeroed(N);
		for (int32 I = 0; I < N; ++I)
		{
			for (int32 K = From; K < To; ++K) Tone[I] += V3(bRows ? Src.At(K, I) : Src.At(I, K));
			Tone[I] /= float(To - From);
		}
		BoxBlur(Tone, bRows ? 1 : N, bRows ? N : 1, 8, !bRows, bRows);
		return Tone;
	};
	const TArray<FVector3f> ToneL = EdgeTone(true, 0, 8), ToneR = EdgeTone(true, AW - 8, AW);
	const TArray<FVector3f> ToneT = EdgeTone(false, 0, 6), ToneB = EdgeTone(false, AH - 6, AH);
	// Asheron's robe runs off the bottom edge; below the art its colours fade into the stone
	// tone of the plain bottom-left so the figure does not appear to continue.
	FVector3f StoneB(0);
	{
		const int32 N = FMath::Clamp(AW * 13 / 20, 1, AW);
		for (int32 X = 0; X < N; ++X) StoneB += ToneB[X];
		StoneB /= float(N);
	}

	// Patch grain laid in overlapping, cross-faded tiles, each from a different height and
	// some flipped, so no repeat or mirror axis shows.
	auto TileGrain = [&](int32 K, int32 U, int32 Y)
	{
		const uint32 H = ::GetTypeHash(K * 2654435761u + 0x9E3779B9u) * 2246822519u;
		const int32 SY = Reflect(Y + int32(H % uint32(AH)), AH);
		const int32 SX = (H >> 20) & 1 ? PatchW - 1 - U : U;
		return Detail[SY * AW + PatchX + SX];
	};
	auto Grain = [&](int32 X, int32 Y)
	{
		if (!bPatch) return FVector3f(0);
		const int32 K = X / Step, L = X - K * Step;
		FVector3f G = TileGrain(K, L, Y);
		if (L < Overlap && K > 0) G = FMath::Lerp(TileGrain(K - 1, L + Step, Y), G, (L + .5f) / Overlap);
		return G;
	};
	auto Smooth = [](float T) { T = FMath::Clamp(T, 0.f, 1.f); return T * T * (3.f - 2.f * T); };
	// How far the mirrored edge reaches before it has fully turned into stone: only as far as
	// each edge stays clear of the titles, logo and Asheron's sleeve.
	constexpr float BlendL = 24.f, BlendR = 10.f, BlendT = 10.f, BlendB = 8.f;

	// The composition may open up at Split: the left part stays, the right part moves Spread
	// columns right, and the opening continues the plain art on both sides into stone toned
	// like the art just right of the split.
	const int32 Split = FMath::Clamp(SplitX, 1, AW - 1);
	Spread = bPatch ? FMath::Clamp(Spread, 0, 1024) : 0;
	const int32 SW = AW + Spread;
	constexpr float BlendM = 30.f;
	const TArray<FVector3f> ToneM = EdgeTone(true, Split, FMath::Min(Split + 60, AW));
	auto Composite = [&](int32 SX, int32 SY, int32 GX, int32 GY)
	{
		if (SX < Split) return V3(Src.At(SX, SY));
		if (SX >= Split + Spread) return V3(Src.At(SX - Spread, SY));
		const int32 Near = FMath::Min(SX - Split, Split + Spread - 1 - SX);
		const FVector3f Cont = V3(Src.At(FMath::Min(Split + Near, AW - 1), SY));
		return FMath::Lerp(Cont, ToneM[SY] + Grain(GX, GY), Smooth(Near / BlendM));
	};
	auto ArtColumn = [&](int32 SX) { return SX < Split ? SX : SX >= Split + Spread ? SX - Spread : Split; };

	FImg Out; Out.W = SW + 2 * ExtX; Out.H = AH + 2 * ExtY;
	Out.Px.SetNumUninitialized(Out.W * Out.H);
	for (int32 Y = 0; Y < Out.H; ++Y) for (int32 X = 0; X < Out.W; ++X)
	{
		const int32 AX = X - ExtX, AY = Y - ExtY;
		FVector3f C;
		if (AX >= 0 && AX < SW && AY >= 0 && AY < AH)
		{
			if (AX < Split || AX >= Split + Spread) { Out.At(X, Y) = Src.At(ArtColumn(AX), AY); continue; }
			C = Composite(AX, AY, X, Y);
		}
		else
		{
			FVector3f Tone; float G;
			if (AX < 0 || AX >= SW)
			{
				Tone = (AX < 0 ? ToneL : ToneR)[FMath::Clamp(AY, 0, AH - 1)];
				G = AX < 0 ? GainL : GainR;
			}
			else
			{
				const int32 CX = ArtColumn(AX);
				Tone = AY < 0 ? ToneT[CX] : FMath::Lerp(ToneB[CX], StoneB, FMath::Clamp((AY - AH + 1) / 48.f, 0.f, 1.f));
				G = AY < 0 ? GainT : GainB;
			}
			const FVector3f Stone = Tone + Grain(X, Y) * G;
			const float WX = AX < 0 ? Smooth(-AX / BlendL) : AX >= SW ? Smooth((AX - SW + 1) / BlendR) : 0.f;
			const float WY = AY < 0 ? Smooth(-AY / BlendT) : AY >= AH ? Smooth((AY - AH + 1) / BlendB) : 0.f;
			const FVector3f Mirror = Composite(Reflect(AX, SW), Reflect(AY, AH), X, Y);
			C = FMath::Lerp(Mirror, Stone, FMath::Max(WX, WY));
		}
		Out.At(X, Y) = FColor(uint8(FMath::Clamp(FMath::RoundToInt(C.X), 0, 255)), uint8(FMath::Clamp(FMath::RoundToInt(C.Y), 0, 255)),
			uint8(FMath::Clamp(FMath::RoundToInt(C.Z), 0, 255)), 255);
	}
	return FACEDatTextureResolver::CreateTransientRgbaUi(Out.W, Out.H, Out.Px);
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
		0x06004C66u, 0x06004C67u, 0x06004C6Au, 0x06004C6Bu, 0x06004C6Du, 0x06004C6Eu,
		0x06004CA3u, 0x06004CA4u, 0x06004D64u};
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
	// The lobby draws the crest at the top left, so the baked logo goes too.
	PaintOutLogo(Patched);
	GPatchedBg = Patched;
	Art->Background = ImageBrush(MakeMipped(Patched), FVector2D(800, 600));

	const FMargin ButtonSlice(24, 24, 24, 24);
	for (int32 I = 0; I < 3; ++I)
	{
		UTexture2D* Plate = MakeUi(Crop(Src[0x06004C9Eu + I], 20, 19, 131, 60));
		Art->Button[I] = BoxBrush(Plate, FVector2D(131, 60), ButtonSlice);
		Art->SmallButton[I] = BoxBrush(Plate, FVector2D(131, 60), ButtonSlice, .62f);
	}
	for (int32 I = 0; I < 2; ++I)
		Art->Primary[I] = BoxBrush(MakeUi(Crop(Src[0x06004CA3u + I], 20, 19, 215, 74)), FVector2D(215, 74), ButtonSlice);
	{
		const FImg& Frame = Src[0x06004D64u];
		FImg Plate = Crop(Frame, 20, 19, 154, 71), Hot = Plate;
		for (FColor& C : Hot.Px)
		{
			C.R = uint8(FMath::Min(255, C.R * 5 / 4)); C.G = uint8(FMath::Min(255, C.G * 5 / 4)); C.B = uint8(FMath::Min(255, C.B * 9 / 8));
		}
		Art->Plaque[0] = BoxBrush(MakeUi(Plate), FVector2D(154, 71), FMargin(22, 22, 22, 22));
		Art->Plaque[1] = BoxBrush(MakeUi(Hot), FVector2D(154, 71), FMargin(22, 22, 22, 22));
	}
	if (UTexture2D* Crest = Keep(KeyedOverBlack(Dat, 0x06001343u)))
		Art->Crest = ImageBrush(Crest, FVector2D(Crest->GetSizeX(), Crest->GetSizeY()));
	GRingHot = Src[0x06004CB3u];
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
	// Codex Hall rows keep the DAT gold top and bottom edges but swap the olive middle for a
	// dark leather wash (darker to the right) with faint gold sides.
	auto DarkMiddle = [&](FImg Img, FColor From, FColor To, float Opacity)
	{
		const int32 Top = Src[0x06005EA7u].H, Bottom = Src[0x06005EABu].H;
		for (int32 Y = Top; Y < Img.H - Bottom; ++Y)
			for (int32 X = 0; X < Img.W; ++X)
			{
				const float T = Img.W > 1 ? float(X) / float(Img.W - 1) : 0.f;
				FColor C(uint8(FMath::Lerp(float(From.R), float(To.R), T)), uint8(FMath::Lerp(float(From.G), float(To.G), T)),
					uint8(FMath::Lerp(float(From.B), float(To.B), T)), uint8(FMath::Lerp(float(From.A), float(To.A), T)));
				if (X == 0 || X == Img.W - 1) C = FColor(160, 120, 50, 90);
				Img.At(X, Y) = C;
			}
		ScaleAlpha(Img, Opacity);
		return Img;
	};
	FImg Idle = DarkMiddle(RowIdle, FColor(60, 48, 22, 140), FColor(30, 24, 12, 90), .8f);
	FImg Hover = DarkMiddle(RowIdle, FColor(96, 74, 30, 153), FColor(48, 36, 16, 102), 1.f);
	FImg Selected = RowSelected; AddSelectionGlow(Selected);
	Art->Row[0] = BoxBrush(MakeUi(Idle), RowSize, RowSlice);
	Art->Row[1] = BoxBrush(MakeUi(Hover), RowSize, RowSlice);
	Art->Row[2] = BoxBrush(MakeUi(Selected), FVector2D(Selected.W, Selected.H), RowSlice);

	const FImg& Window = Src[0x06004D26u];
	Art->Window = BoxBrush(MakeUi(Window), FVector2D(Window.W, Window.H), FMargin(80, FACELobbyArt::WindowHeaderH, 60, 4));
	if (Window.W >= 400 && Window.H >= 200)
	{
		const int32 HH = int32(FACELobbyArt::WindowHeaderH);
		Art->WindowHeader[0] = ImageBrush(MakeUi(Crop(Window, 0, 0, 80, HH)), FVector2D(80, HH));
		Art->WindowHeader[1] = ImageBrush(MakeUi(Crop(Window, 80, 0, Window.W - 140, HH)), FVector2D(Window.W - 140, HH));
		Art->WindowHeader[2] = ImageBrush(MakeUi(Crop(Window, Window.W - 60, 0, 60, HH)), FVector2D(60, HH));
		GWindowBody = Crop(Window, 0, HH, Window.W, FMath::Min(530, Window.H - HH - 4));
	}
	{
		// Generated (no DAT equivalent): screen vignette, glow and halo masks, soft shadows.
		const FImg Vig = MakeMask(160, 100, FColor::Black, [](float U, float V)
		{
			const float T = FMath::Sqrt(FMath::Square((U - .62f) / .85f) + FMath::Square((V - .45f) / .75f));
			const float A = FMath::Clamp((T - .55f) / .45f, 0.f, 1.f) * .55f, B = FMath::Clamp((V - .8f) / .2f, 0.f, 1.f) * .35f;
			return 1.f - (1.f - A) * (1.f - B);
		});
		Art->Vignette = ImageBrush(MakeMipped(Vig), FVector2D(160, 100));
		const FImg Glow = MakeMask(64, 64, FColor::White, [](float U, float V)
		{
			const float T = FMath::Clamp(FMath::Sqrt(FMath::Square(U - .5f) + FMath::Square(V - .5f)) * 2.f, 0.f, 1.f);
			return FMath::Square(1.f - T);
		});
		Art->Glow = ImageBrush(MakeMipped(Glow), FVector2D(64, 64));
		const FImg Halo = MakeMask(128, 128, FColor::White, [](float U, float V)
		{
			const float T = FMath::Sqrt(FMath::Square(U - .5f) + FMath::Square(V - .5f)) * 2.f;
			return T < .6f || T > .74f ? 0.f : T < .66f ? (T - .6f) / .06f : 1.f - (T - .66f) / .08f;
		});
		Art->Halo = ImageBrush(MakeMipped(Halo), FVector2D(128, 128));
		auto Soft = [](float D) { const float T = FMath::Clamp(D, 0.f, 1.f); return T * T * (3.f - 2.f * T); };
		const FImg Shadow = MakeMask(96, 96, FColor::Black, [&](float U, float V)
		{
			const float D = FMath::Min(FMath::Min(U, 1.f - U), FMath::Min(V, 1.f - V)) * 96.f / 32.f;
			return Soft(D);
		});
		Art->Shadow = BoxBrush(MakeUi(Shadow), FVector2D(96, 96), FMargin(32, 32, 32, 32));
		const FImg Inner = MakeMask(128, 128, FColor::Black, [&](float U, float V)
		{
			const float D = FMath::Min(FMath::Min(U, 1.f - U), FMath::Min(V, 1.f - V)) * 128.f / 60.f;
			return FMath::Square(1.f - FMath::Clamp(D, 0.f, 1.f));
		});
		Art->InnerShadow = BoxBrush(MakeUi(Inner), FVector2D(128, 128), FMargin(60, 60, 60, 60));
	}
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
	Fill = Args._Fill; FillTop = Args._FillTop; bShadow = Args._Shadow;
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
	const FACELobbyArt* Art = ACELobbyArt::Get();
	if (bShadow && Art)
		Box(&Art->Shadow, FVector2D(-30, -12), S + FVector2D(60, 60), FLinearColor(1, 1, 1, .85f), Layer);
	const FLinearColor Bottom = Fill.Get();
	if (FillTop.IsSet())
	{
		const FLinearColor Top = FillTop.Get();
		FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(S - FVector2D(6, 6), FSlateLayoutTransform(FVector2D(3, 3))),
			{FSlateGradientStop(FVector2D(0, 0), Top * Tint), FSlateGradientStop(S - FVector2D(6, 6), Bottom * Tint)}, Orient_Horizontal);
	}
	else Box(FCoreStyle::Get().GetBrush("WhiteBrush"), FVector2D(4, 4), S - FVector2D(8, 8), Bottom, Layer);
	if (Art) Box(&Art->InnerShadow, FVector2D(3, 3), S - FVector2D(6, 6), FLinearColor(1, 1, 1, .55f), Layer);
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

void SACELobbyWindow::Construct(const FArguments& Args)
{
	Tint = Args._Tint;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SACELobbyWindow::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool) const
{
	const FVector2D S = G.GetLocalSize();
	const float HH = FACELobbyArt::WindowHeaderH;
	if (S.X < 160 || S.Y < HH + 4) return Layer;
	const FLinearColor Mod = Style.GetColorAndOpacityTint();
	auto Box = [&](const FSlateBrush* Brush, FVector2D Pos, FVector2D Size, FLinearColor Color, int32 L)
	{
		if (Brush && Size.X > 0 && Size.Y > 0)
			FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), Brush, ESlateDrawEffect::None, Color * Mod);
	};
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FACELobbyArt* Art = ACELobbyArt::Get();
	const int32 T = Tint.Get(0);
	if (!Art)
	{
		Box(White, FVector2D::ZeroVector, FVector2D(S.X, HH), FLinearColor(.12f, .07f, .03f, .95f), Layer);
		Box(White, FVector2D(0, HH), FVector2D(S.X, S.Y - HH), ACELobbyArt::Tint(T).Pane, Layer);
		return Layer;
	}
	Box(&Art->Shadow, FVector2D(-26, -12), S + FVector2D(52, 52), FLinearColor(1, 1, 1, .9f), Layer);
	// Body: the retail texture at native scale, tiled down long windows, then the mockup's inset shading.
	if (const FSlateBrush* Body = ACELobbyArt::TintedWindowBody(T))
		Box(Body, FVector2D(0, HH), FVector2D(S.X, S.Y - HH), FLinearColor::White, Layer);
	Box(&Art->InnerShadow, FVector2D(0, HH), FVector2D(S.X, S.Y - HH), FLinearColor(1, 1, 1, .6f), Layer);
	const FLinearColor Border(FColor(0x6b, 0x52, 0x26));
	const TArray<FVector2D> Edge = {FVector2D(.5, HH), FVector2D(.5, S.Y - .5), FVector2D(S.X - .5, S.Y - .5), FVector2D(S.X - .5, HH)};
	FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), Edge, ESlateDrawEffect::None, Border * Mod, false, 1.f);
	Box(&Art->WindowHeader[0], FVector2D::ZeroVector, FVector2D(80, HH), FLinearColor::White, Layer + 1);
	Box(&Art->WindowHeader[1], FVector2D(80, 0), FVector2D(S.X - 140, HH), FLinearColor::White, Layer + 1);
	Box(&Art->WindowHeader[2], FVector2D(S.X - 60, 0), FVector2D(60, HH), FLinearColor::White, Layer + 1);
	return Layer + 1;
}

void SACELobbyDecor::Construct(const FArguments& Args)
{
	Kind = Args._Kind; Color = Args._Color; State = Args._State;
	SetVisibility(EVisibility::HitTestInvisible);
}

FVector2D SACELobbyDecor::ComputeDesiredSize(float) const
{
	switch (Kind)
	{
	case EKind::Rule: case EKind::CenteredRule: return FVector2D(40, 1);
	case EKind::Gem: return FVector2D(18, 18);
	case EKind::Badge: return FVector2D(8, 8);
	default: return FVector2D(16, 16);
	}
}

int32 SACELobbyDecor::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2D S = G.GetLocalSize();
	if (S.X <= 0 || S.Y <= 0) return Layer;
	const FLinearColor Mod = Style.GetColorAndOpacityTint();
	const FLinearColor Gold(FColor(242, 169, 51));
	const FACELobbyArt* Art = ACELobbyArt::Get();
	auto Fade = [](FLinearColor C, float A) { C.A *= A; return C; };
	auto HGradient = [&](FVector2D Pos, FVector2D Size, std::initializer_list<TPair<float, FLinearColor>> Stops, int32 L)
	{
		TArray<FSlateGradientStop> Out2;
		for (const auto& P : Stops) Out2.Add(FSlateGradientStop(FVector2D(P.Key * Size.X, 0), P.Value * Mod));
		FSlateDrawElement::MakeGradient(Out, L, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), Out2, Orient_Vertical);
	};
	auto Circle = [&](FVector2D Center, float Radius, FLinearColor Fill, FLinearColor Outline, float Width, int32 L)
	{
		const FSlateRoundedBoxBrush Brush(FLinearColor::White, Radius, Outline * Mod, Width);
		FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(FVector2D(Radius * 2, Radius * 2), FSlateLayoutTransform(Center - FVector2D(Radius, Radius))),
			&Brush, ESlateDrawEffect::None, Fill * Mod);
	};
	switch (Kind)
	{
	case EKind::Rule:
		HGradient(FVector2D(0, 0), FVector2D(S.X, 1), {{0.f, Fade(Gold, .75f)}, {.7f, Fade(Gold, .15f)}, {1.f, Fade(Gold, 0)}}, Layer);
		return Layer;
	case EKind::CenteredRule:
		HGradient(FVector2D(0, 0), FVector2D(S.X, 1), {{0.f, Fade(Gold, 0)}, {.2f, Fade(Gold, .7f)}, {.8f, Fade(Gold, .7f)}, {1.f, Fade(Gold, 0)}}, Layer);
		return Layer;
	case EKind::NavItem:
	{
		const int32 St = State.Get(0);
		if (St == 2)
		{
			HGradient(FVector2D(0, 0), S, {{0.f, Fade(Gold, .22f)}, {.7f, Fade(Gold, .05f)}, {1.f, Fade(Gold, 0)}}, Layer);
			const FLinearColor Bar(FColor(255, 215, 122));
			FSlateDrawElement::MakeGradient(Out, Layer + 1, G.ToPaintGeometry(FVector2D(3, S.Y - 12), FSlateLayoutTransform(FVector2D(0, 6))),
				{FSlateGradientStop(FVector2D(0, 0), Fade(Bar, 0) * Mod), FSlateGradientStop(FVector2D(0, (S.Y - 12) * .5f), Bar * Mod),
				 FSlateGradientStop(FVector2D(0, S.Y - 12), Fade(Bar, 0) * Mod)}, Orient_Horizontal);
			if (Art) FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(16, S.Y), FSlateLayoutTransform(FVector2D(-6, 0))),
				&Art->Glow, ESlateDrawEffect::None, Fade(Gold, .45f) * Mod);
		}
		else if (St == 1)
			HGradient(FVector2D(0, 0), S, {{0.f, Fade(Gold, .08f)}, {.7f, Fade(Gold, 0)}, {1.f, Fade(Gold, 0)}}, Layer);
		// Diamond bullet at x 22.
		const FVector2D C(25.5, S.Y * .5);
		const bool bOn = St == 2;
		if (bOn && Art)
			FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2D(22, 22), FSlateLayoutTransform(C - FVector2D(11, 11))),
				&Art->Glow, ESlateDrawEffect::None, Fade(Gold, .8f) * Mod);
		const FLinearColor Edge = (bOn ? FLinearColor(FColor(255, 215, 122)) : FLinearColor(FColor(138, 106, 46))) * Mod;
		const FLinearColor Inside = (bOn ? FLinearColor(FColor(255, 215, 122)) : FLinearColor(0, 0, 0, .4f)) * Mod;
		const float H = 5.f;
		for (float Y = -H + .5f; Y < H; Y += .5f)
		{
			const float Wd = H - FMath::Abs(Y);
			FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), {C + FVector2D(-Wd, Y), C + FVector2D(Wd, Y)}, ESlateDrawEffect::None, Inside, true, .8f);
		}
		FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), {C + FVector2D(0, -H), C + FVector2D(H, 0), C + FVector2D(0, H), C + FVector2D(-H, 0), C + FVector2D(0, -H)},
			ESlateDrawEffect::None, Edge, true, 1.f);
		return Layer + 2;
	}
	case EKind::Gem:
	{
		const FLinearColor C = Color.Get(FLinearColor::White);
		const float R = FMath::Min(S.X, S.Y) * .5f - 1.5f;
		const FVector2D Mid = S * .5;
		if (Art) FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(R * 5, R * 5), FSlateLayoutTransform(Mid - FVector2D(R * 2.5f, R * 2.5f))),
			&Art->Glow, ESlateDrawEffect::None, Fade(C, State.Get(0) ? .95f : .7f) * Mod);
		Circle(Mid, R + 1.5f, FLinearColor(FColor(6, 16, 32)), FLinearColor(FColor(200, 150, 60)), 1.5f, Layer + 1);
		Circle(Mid - FVector2D(R * .1f, R * .15f), R * .82f, C, FLinearColor::Transparent, 0.f, Layer + 2);
		Circle(Mid - FVector2D(R * .3f, R * .4f), R * .3f, FLinearColor(1, 1, 1, .85f), FLinearColor::Transparent, 0.f, Layer + 3);
		return Layer + 3;
	}
	case EKind::Badge:
	{
		const float R = FMath::Min(S.X, S.Y) * .5f;
		if (Art) FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(R * 5, R * 5), FSlateLayoutTransform(S * .5 - FVector2D(R * 2.5f, R * 2.5f))),
			&Art->Glow, ESlateDrawEffect::None, Fade(Gold, .9f) * Mod);
		Circle(S * .5, R, FLinearColor(FColor(224, 138, 26)), FLinearColor(FColor(255, 233, 176)), 1.f, Layer + 1);
		return Layer + 1;
	}
	case EKind::Halo:
		if (Art) FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), &Art->Halo, ESlateDrawEffect::None, Color.Get(FLinearColor::White) * Mod);
		return Layer;
	case EKind::Glow:
		if (Art) FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), &Art->Glow, ESlateDrawEffect::None, Color.Get(FLinearColor::White) * Mod);
		return Layer;
	}
	return Layer;
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
	const EKind K = Kind.Get(EKind::Star);
	auto Line = [&](std::initializer_list<FVector2D> P, float Width) { FSlateDrawElement::MakeLines(Out, Layer, Geo, TArray<FVector2D>(P), Effect, Tint, true, Width); };
	auto Ring = [&](FVector2D C, double R, float Width, double From = 0, double To = 2 * PI)
	{
		TArray<FVector2D> P;
		for (int32 I = 0; I <= 28; ++I) { const double A = From + (To - From) * I / 28; P.Add(C + FVector2D(R * FMath::Cos(A), R * FMath::Sin(A))); }
		FSlateDrawElement::MakeLines(Out, Layer, Geo, P, Effect, Tint, true, Width);
	};
	switch (K)
	{
	case EKind::ChevronDown: { const FVector2D A(4, 6), B(8, 10.5), C(12, 6); FSlateDrawElement::MakeLines(Out, Layer, Geo, {A, B, C}, Effect, Tint, true, 2.f); return Layer; }
	case EKind::ChevronUp: { const FVector2D A(4, 10), B(8, 5.5), C(12, 10); FSlateDrawElement::MakeLines(Out, Layer, Geo, {A, B, C}, Effect, Tint, true, 2.f); return Layer; }
	case EKind::Pointer:
	{
		for (double Y = 3; Y <= 13; Y += .5) Line({FVector2D(5, Y), FVector2D(5 + (5 - FMath::Abs(Y - 8)) * 1.2, Y)}, .9f);
		return Layer;
	}
	case EKind::Diamond:
	{
		for (double Y = 3; Y <= 13; Y += .5) { const double H = 5 - FMath::Abs(Y - 8); Line({FVector2D(8 - H, Y), FVector2D(8 + H, Y)}, .9f); }
		return Layer;
	}
	case EKind::Check: Line({FVector2D(3.5, 8.5), FVector2D(6.5, 11.5), FVector2D(12.5, 4.5)}, 2.f); return Layer;
	case EKind::Plus: Line({FVector2D(8, 3), FVector2D(8, 13)}, 2.f); Line({FVector2D(3, 8), FVector2D(13, 8)}, 2.f); return Layer;
	case EKind::Bang: Line({FVector2D(8, 3), FVector2D(8, 10)}, 2.2f); Line({FVector2D(8, 12), FVector2D(8, 14)}, 2.2f); return Layer;
	case EKind::Spinner: Ring(FVector2D(8, 8), 5.5, 2.f, 0, PI * 1.4); return Layer;
	case EKind::Grip:
	{
		const FSlateRoundedBoxBrush Dot(FLinearColor::White, 1.2f);
		for (int32 Y = 0; Y < 3; ++Y) for (int32 X = 0; X < 2; ++X)
			FSlateDrawElement::MakeBox(Out, Layer, Art.ToPaintGeometry(FVector2D(2.4, 2.4), FSlateLayoutTransform(FVector2D(5.3 + X * 5, 2.8 + Y * 5))), &Dot, Effect, Tint);
		return Layer;
	}
	case EKind::Eye:
		Ring(FVector2D(8, 13), 7.5, 1.5f, -PI * .82, -PI * .18); Ring(FVector2D(8, 3), 7.5, 1.5f, PI * .18, PI * .82); Ring(FVector2D(8, 8), 2.2, 1.6f);
		if (Filled.Get(false)) Line({FVector2D(3, 13), FVector2D(13, 3)}, 1.6f);
		return Layer;
	case EKind::Lock:
		Ring(FVector2D(8, 7), 3.2, 1.5f, PI, 2 * PI); Line({FVector2D(4.8, 7), FVector2D(4.8, 8)}, 1.5f); Line({FVector2D(11.2, 7), FVector2D(11.2, 8)}, 1.5f);
		for (double Y = 8; Y <= 13.5; Y += .5) Line({FVector2D(3.5, Y), FVector2D(12.5, Y)}, .9f);
		return Layer;
	default: break;
	}
	if (K == EKind::Star)
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
	if (K == EKind::Magnifier)
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
