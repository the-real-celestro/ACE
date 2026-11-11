#include "UI/ACELootPresentation.h"
#include "UI/ACELootIconOverlay.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEHoverTooltipWidget.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

class SACEQualityIcon : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SACEQualityIcon){}SLATE_END_ARGS()
    void Construct(const FArguments&){}
    bool Rated=false;FLinearColor Tint;FString Mark;FVector2D MarkAt=FVector2D::ZeroVector;
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(32,32);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,
        int32 L,const FWidgetStyle&,bool)const override
    {
        const auto* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));const FVector2D Size=G.GetLocalSize();
        auto Box=[&](FVector2D P,FVector2D S,FLinearColor C)
        {FSlateDrawElement::MakeBox(E,L,G.ToPaintGeometry(S,FSlateLayoutTransform(P)),White,ESlateDrawEffect::None,C);};
        if(Rated)
        {
            Box({0,0},{Size.X,1},Tint);Box({0,Size.Y-1},{Size.X,1},Tint);
            Box({0,1},{1,Size.Y-2},Tint);Box({Size.X-1,1},{1,Size.Y-2},Tint);
        }
        if(!Mark.IsEmpty())
        {
            const FVector2D Scale=Size/32.;const bool Two=Mark==TEXT("2H");
            const FVector2D P=MarkAt*Scale;
            Box(P,FVector2D(Two?10:7,Two?7:9)*Scale,FLinearColor(.015,.02,.03,.94));
            const FVector2D MarkSize=FVector2D(Two?10:7,Two?7:9)*Scale;
            E.PushClip(FSlateClippingZone(G.MakeChild(MarkSize,FSlateLayoutTransform(P)).GetLayoutBoundingRect()));
            FSlateDrawElement::MakeText(E,L+1,G.ToPaintGeometry(Size,FSlateLayoutTransform(P+FVector2D(1,0)*Scale)),Mark,
                FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),Two?5:6),ESlateDrawEffect::None,FLinearColor(.95,.92,.80));
            E.PopClip();
        }
        return L+1;
    }
};
void UACELootIconOverlay::SetReview(const FACEQualityReview& Review,UTexture2D* Foreground)
{
    TakeWidget();Painted->Rated=Review.bRated;Painted->Tint=ACELootQuality::Color(Review.Grade);Painted->Mark.Empty();
    // Test actual composited foreground coverage. Never cover the weapon to
    // force a class badge onto a small inventory icon.
    if(!Review.ClassMark.IsEmpty()&&Foreground&&Foreground->GetPlatformData()&&Foreground->GetPlatformData()->Mips.Num())
    {
        const auto* Platform=Foreground->GetPlatformData();
        if(Platform->SizeX==32&&Platform->SizeY==32&&Platform->PixelFormat==PF_B8G8R8A8)
        {
            auto& Mip=Foreground->GetPlatformData()->Mips[0];const auto* Pixels=static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
            if(Pixels)
            {
                const int32 W=Review.ClassMark==TEXT("2H")?10:7,H=Review.ClassMark==TEXT("2H")?7:9;
                for(const FIntPoint P:{FIntPoint(1,1),FIntPoint(1,31-H)})
                {
                    bool Clear=true;for(int32 Y=P.Y;Y<P.Y+H&&Clear;++Y)for(int32 X=P.X;X<P.X+W;++X)
                        if(Pixels[Y*32+X].A>16){Clear=false;break;}
                    if(Clear){Painted->Mark=Review.ClassMark;Painted->MarkAt=FVector2D(P);break;}
                }
            }
            Mip.BulkData.Unlock();
        }
    }
    Painted->Invalidate(EInvalidateWidgetReason::Paint);
}
TSharedRef<SWidget> UACELootIconOverlay::RebuildWidget(){return SAssignNew(Painted,SACEQualityIcon);}
void UACELootIconOverlay::ReleaseSlateResources(bool Children){Super::ReleaseSlateResources(Children);Painted.Reset();}

namespace
{
class SACELootArt : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SACELootArt){}SLATE_ARGUMENT(UTexture2D*,Background)SLATE_ARGUMENT(UTexture2D*,Foreground)SLATE_END_ARGS()
    void Construct(const FArguments& A)
    {Bg.SetResourceObject(A._Background);Fg.SetResourceObject(A._Foreground);Bg.DrawAs=Fg.DrawAs=ESlateBrushDrawType::Image;Bg.ImageSize=Fg.ImageSize=FVector2D(32,32);}
    virtual FVector2D ComputeDesiredSize(float)const override{return {32,32};}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,int32 L,const FWidgetStyle&,bool)const override
    {FSlateDrawElement::MakeBox(E,L,G.ToPaintGeometry(),&Bg);FSlateDrawElement::MakeBox(E,L+1,G.ToPaintGeometry(),&Fg);return L+1;}
    FSlateBrush Bg,Fg;
};
class SACELootBar : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SACELootBar){}SLATE_ARGUMENT(FACEQualityStat,Stat)SLATE_END_ARGS()
    void Construct(const FArguments& A){Stat=A._Stat;}
    virtual FVector2D ComputeDesiredSize(float)const override{return {200,18};}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,
        int32 L,const FWidgetStyle&,bool)const override
    {
        if(!Stat.Bounds.Variable())return L;
        const auto* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));const double W=G.GetLocalSize().X;
        auto Position=[&](double V){const double P=(V-Stat.Bounds.Low)/(Stat.Bounds.High-Stat.Bounds.Low);return Stat.bLowerBetter?1-P:P;};
        const double Original=Position(Stat.Original),Current=Position(Stat.Permanent),Magic=Position(Stat.WithCantrip);
        const double Max=FMath::Max(1.,FMath::Max(Current,Magic));
        const double Span=Stat.Bounds.High-Stat.Bounds.Low;
        const double Best=Stat.bLowerBetter?Stat.Bounds.Low:Stat.Bounds.High;
        const double ExtraRatio=Max>1?(Max-1)*Span/FMath::Max(FMath::Abs(Best),Span):0;
        const double NaturalWidth=Max>1?FMath::Min((W-6)/(1+ExtraRatio),W-26):W-6;
        const double Tail=W-6-NaturalWidth;
        auto X=[&](double P)
        {
            P=FMath::Clamp(P,0.,Max);
            return 3+(P<=1?P*NaturalWidth:NaturalWidth+(P-1)/(Max-1)*Tail);
        };
        auto Box=[&](double PX,double Y,double SX,double SY,FLinearColor C,int32 Depth=0)
        {FSlateDrawElement::MakeBox(E,L+Depth,G.ToPaintGeometry(FVector2D(SX,SY),FSlateLayoutTransform(FVector2D(PX,Y))),White,ESlateDrawEffect::None,C);};
        const double Cuts[]={0,.2,.5,.75,.9,.97,1};
        for(int32 I=0;I<6;++I)Box(X(Cuts[I]),7,X(Cuts[I+1])-X(Cuts[I]),4,ACELootQuality::Color(static_cast<ACELootScoring::Grade>(I)).CopyWithNewOpacity(.65));
        if(Max>1)Box(X(1),7,X(Max)-X(1),4,FLinearColor(.12,.14,.18));
        if(FMath::Abs(Current-Magic)>1.e-5)Box(X(FMath::Min(Current,Magic)),6,FMath::Abs(X(Magic)-X(Current)),6,FLinearColor(.15,.63,1),1);
        if(Max>1)
        {
            const double Break=X(1)+7;
            for(double Offset:{0.,4.})
            {
                TArray<FVector2D> Slash{{Break+Offset-2,12},{Break+Offset+2,4}};
                FSlateDrawElement::MakeLines(E,L+2,G.ToPaintGeometry(),Slash,ESlateDrawEffect::None,FLinearColor(.018,.022,.033),true,2);
            }
        }
        if(Stat.bOriginalKnown)
        {Box(X(Original)-4,1,8,16,FLinearColor(.02,.02,.02),2);Box(X(Original)-2,3,4,12,FLinearColor(1,.97,.83),3);}
        if(!Stat.bOriginalKnown||FMath::Abs(Original-Current)>1.e-5)Box(X(Current)-1,0,2,18,FLinearColor(1,.64,.18),4);
        if(FMath::Abs(Current-Magic)>1.e-5)Box(X(Magic)-1,3,2,12,FLinearColor(.25,.75,1),4);
        return L+4;
    }
    FACEQualityStat Stat;
};
TSharedRef<STextBlock> Text(const FString& Value,int32 Size,FLinearColor Color,float Wrap)
{
    return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size))
        .ColorAndOpacity(Color).AutoWrapText(false).WrapTextAt(Wrap);
}
}
void ACELootPresentation::Tooltip(UWidget* Owner,const FString& Name,const FACEWorldObject& Item,
    const FACEQualityReview& R,UACEUIResourceResolver* Resources)
{
    if(!R.bEquipment){UACEHoverTooltipWidget::SetWidgetTooltip(Owner,FText::FromString(Name),UACEHoverTooltipWidget::ObjectTemplate);return;}
    const FVector2D View=Owner&&Owner->GetWorld()?UWidgetLayoutLibrary::GetViewportSize(Owner):FVector2D::ZeroVector;
    const float Dpi=Owner&&Owner->GetWorld()?UWidgetLayoutLibrary::GetViewportScale(Owner):1.f;
    const float Width=View.X>1?FMath::Clamp(float(View.X/FMath::Max(Dpi,.1f)-24),180.f,360.f):360.f;
    auto T=[&](const FString& Value,int32 Size=11,FLinearColor Color=FLinearColor(.84,.85,.88))
        {return Text(Value,Size,Color,Width-22);};
    TSharedRef<SVerticalBox> Rows=SNew(SVerticalBox);
    TSharedRef<SHorizontalBox> Header=SNew(SHorizontalBox);
    if(Resources)Header->AddSlot().AutoWidth().Padding(0,0,8,0)
        [SNew(SBox).WidthOverride(32).HeightOverride(32)[SNew(SACELootArt)
        .Background(Resources->ResolveItemBackground(Item.ItemType,Item.IconUnderlayId))
        .Foreground(Resources->ResolveItemForeground(Item.IconId,Item.IconOverlayId,Item.UiEffects,false,Item.Structure,Item.MaxStructure))]];
    Header->AddSlot().FillWidth(1)[Text(Name,15,R.bRated?ACELootQuality::Color(R.Grade):FLinearColor(.95,.92,.8),Width-(Resources?62:22))];
    Rows->AddSlot().AutoHeight()[Header];
    if(R.DamageType.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,3)[T(R.Class,11)];
    else
    {
        const uint32 Effect=R.DamageType==TEXT("Cold")?0x80:R.DamageType==TEXT("Fire")?0x20:
            R.DamageType==TEXT("Acid")?0x100:R.DamageType==TEXT("Electric")?0x40:
            R.DamageType==TEXT("Slashing")?0x400:R.DamageType==TEXT("Piercing")?0x800:
            R.DamageType==TEXT("Bludgeoning")?0x200:R.DamageType==TEXT("Nether")?0x1000:0;
        TSharedRef<SHorizontalBox> TypeLine=SNew(SHorizontalBox);
        TypeLine->AddSlot().FillWidth(1)[Text(R.Class,11,FLinearColor(.84,.85,.88),Width-132)];
        TypeLine->AddSlot().AutoWidth().Padding(8,0,0,0)[Text(R.DamageType+TEXT(" damage"),11,
            Resources?Resources->ResolveItemEffectColor(Effect):FLinearColor(.7,.8,.95),110)];
        Rows->AddSlot().AutoHeight().Padding(0,3)[TypeLine];
    }
    Rows->AddSlot().AutoHeight().Padding(0,4)[T(R.bRated?FString::Printf(TEXT("%s · %.1f quality points"),*ACELootQuality::Name(R.Grade),R.Points):R.Reason,13,
        R.bRated?ACELootQuality::Color(R.Grade):FLinearColor(.8,.8,.8))];
    if(R.bRated&&!R.Class.Contains(TEXT("spell package")))Rows->AddSlot().AutoHeight()[T(FString::Printf(TEXT("Original %.1f + cantrips %.1f%s"),R.OriginalPoints,R.CantripPoints,
        R.PropertyPoints>0?*FString::Printf(TEXT(" + gear %.1f"),R.PropertyPoints):TEXT("")))];
    if(R.Stats.Num()>=2&&R.Stats[0].Label==TEXT("Maximum damage")&&R.Stats[1].Label==TEXT("Damage variance"))
    {
        const auto& D=R.Stats[0];const auto& V=R.Stats[1];
        if(D.bOriginalKnown&&R.Tinkers>0)Rows->AddSlot().AutoHeight()[T(FString::Printf(TEXT("Original per hit: %.1f–%.1f"),D.Original*(1-V.Original),D.Original),11)];
        FString Hit=FString::Printf(TEXT("Per hit: %.1f–%.1f → %.1f–%.1f with cantrip"),D.Permanent*(1-V.Permanent),D.Permanent,D.WithCantrip*(1-V.Permanent),D.WithCantrip);
        Rows->AddSlot().AutoHeight().Padding(0,4)[T(Hit,12,FLinearColor(.55,.78,1))];
    }
    for(const auto& S:R.Stats)
    {
        TSharedRef<SHorizontalBox> Heading=SNew(SHorizontalBox);
        Heading->AddSlot().FillWidth(1)[Text(S.Label,11,FLinearColor(.95,.92,.8),Width-132)];
        if(S.Bounds.Variable())Heading->AddSlot().AutoWidth().Padding(6,0,0,0)
            [Text(TEXT("Original ")+ACELootQuality::FormatStat(S.Bounds.Low,S.bPercent)+TEXT("–")+ACELootQuality::FormatStat(S.Bounds.High,S.bPercent),10,FLinearColor(.72,.74,.8),110)];
        Rows->AddSlot().AutoHeight().Padding(0,7,0,0)[Heading];
        const FString Original=S.bOriginalKnown?ACELootQuality::FormatStat(S.Original,S.bPercent):TEXT("unknown");
        FString Stages=TEXT("Original ")+Original;
        if(!S.bPermanentKnown)Stages=TEXT("Appraised ")+ACELootQuality::FormatStat(S.Permanent,S.bPercent)+TEXT(" · effects unresolved");
        else if(R.Tinkers>0)Stages+=TEXT(" · Tinkered ")+ACELootQuality::FormatStat(S.Permanent,S.bPercent);
        else Stages+=TEXT(" · No tinkering");
        if(S.bOriginalKnown&&S.bPermanentKnown&&FMath::Abs(S.Permanent-S.Original)>1.e-5)
            Stages+=S.bPercent?FString::Printf(TEXT(" (%+.1f pp)"),(S.Permanent-S.Original)*100):FString::Printf(TEXT(" (%+.1f)"),S.Permanent-S.Original);
        Rows->AddSlot().AutoHeight()[T(Stages)];
        if(FMath::Abs(S.WithCantrip-S.Permanent)>1.e-5)
            Rows->AddSlot().AutoHeight()[T(TEXT("With cantrip ")+ACELootQuality::FormatStat(S.WithCantrip,S.bPercent)+(S.bPotential?TEXT(" · potential"):TEXT(" · active")),11,FLinearColor(.45,.75,1))];
        Rows->AddSlot().AutoHeight()[SNew(SACELootBar).Stat(S)];
    }
    if(!R.Stats.IsEmpty())
    {
        Rows->AddSlot().AutoHeight()[T(TEXT("Ivory: original · Amber: tinkered · Blue: cantrip"),10)];
        if(R.Stats.ContainsByPredicate([](const FACEQualityStat& S)
            {return S.Bounds.Variable()&&(S.bLowerBetter?FMath::Min(S.Permanent,S.WithCantrip)<S.Bounds.Low:FMath::Max(S.Permanent,S.WithCantrip)>S.Bounds.High);}))
            Rows->AddSlot().AutoHeight()[T(TEXT("// marks a compressed extension beyond original limits"),10)];
    }
    for(const auto& S:R.Spells)
    {
        Rows->AddSlot().AutoHeight().Padding(0,4,0,0)[T(FString::Printf(TEXT("%s  +%.1f pts"),*S.Name,S.Points),11,FLinearColor(.66,.7,1))];
        Rows->AddSlot().AutoHeight()[T(S.Effect+TEXT(" · ")+S.Status,10)];
    }
    if(!R.Requirements.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,8,0,0)[T(R.Requirements,10,R.bCannotWield?FLinearColor(1,.5,.65):FLinearColor(.7,.85,.7))];
    if(!R.Activation.IsEmpty())Rows->AddSlot().AutoHeight()[T(R.Activation,10,R.bActivationBlocked?FLinearColor(1,.65,.4):FLinearColor(.7,.85,.7))];
    if(R.bRated)Rows->AddSlot().AutoHeight().Padding(0,5)[T(R.Reason,10)];
    UACEHoverTooltipWidget::SetWidgetContentTooltip(Owner,FText::FromString(Name),
        SNew(SBox).WidthOverride(Width)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(R.bRated?ACELootQuality::Color(R.Grade):FLinearColor(.35,.35,.4)).Padding(1)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.018,.022,.033,.98)).Padding(10)[Rows]]]);
}
