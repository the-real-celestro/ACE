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
    bool Rated=false,Unavailable=false;FLinearColor Tint;FString Mark;FVector2D MarkAt=FVector2D::ZeroVector;
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(32,32);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,
        int32 L,const FWidgetStyle&,bool)const override
    {
        const auto* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));const FVector2D Size=G.GetLocalSize();
        auto Box=[&](FVector2D P,FVector2D S,FLinearColor C)
        {FSlateDrawElement::MakeBox(E,L,G.ToPaintGeometry(S,FSlateLayoutTransform(P)),White,ESlateDrawEffect::None,C);};
        if(Rated)
        {
            const float Scale=FMath::Min(Size.X,Size.Y)/32.f;
            auto Frame=[&](float Width,FLinearColor Color)
            {
                Box({0,0},{Size.X,Width},Color);Box({0,Size.Y-Width},{Size.X,Width},Color);
                Box({0,Width},{Width,Size.Y-2*Width},Color);Box({Size.X-Width,Width},{Width,Size.Y-2*Width},Color);
            };
            Frame(4*Scale,FLinearColor(.005,.008,.012,.95));
            Frame(3*Scale,Tint);
        }
        // A small corner marker replaces the unusable background tint on imbued
        // items. Keep the quality edge and authored effect artwork intact.
        if(Unavailable)
        {
            const float Scale=FMath::Min(Size.X,Size.Y)/32.f;
            Box(Size-FVector2D(8,8)*Scale,FVector2D(4,2)*Scale,FLinearColor(1,.35,.7));
            Box(Size-FVector2D(6,8)*Scale,FVector2D(2,4)*Scale,FLinearColor(1,.35,.7));
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
void UACELootIconOverlay::SetReview(const FACEQualityReview& Review,UTexture2D* Foreground,bool PreserveBackground)
{
    TakeWidget();Painted->Rated=Review.bRated;Painted->Tint=ACELootQuality::Color(Review.Grade);Painted->Mark.Empty();
    Painted->Unavailable=PreserveBackground&&Review.bCannotWield;
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
                for(const FIntPoint P:{FIntPoint(4,4),FIntPoint(4,28-H)})
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
        if(!Stat.bOriginalKnown||FMath::Abs(Original-Current)>1.e-5)
        {Box(X(Current)-3,0,6,18,FLinearColor(.01,.015,.02),4);Box(X(Current)-1.5,1,3,16,FLinearColor(1,.64,.18),5);}
        if(FMath::Abs(Current-Magic)>1.e-5)
        {Box(X(Magic)-3,1,6,16,FLinearColor(.01,.015,.02),4);Box(X(Magic)-1.5,2,3,14,FLinearColor(.25,.75,1),5);}
        return L+5;
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
    const float Width=View.X>1?FMath::Clamp(float(View.X/FMath::Max(Dpi,.1f)-24),220.f,360.f):360.f;
    const float ContentWidth=Width-20,ValueWidth=ContentWidth*.20f,LabelWidth=ContentWidth-3*ValueWidth;
    const FLinearColor BaseColor(1,.97,.83),TinkerColor(1,.68,.25),CantripColor(.45,.78,1);
    auto T=[&](const FString& Value,int32 Size=11,FLinearColor Color=FLinearColor(.84,.85,.88))
        {return Text(Value,Size,Color,ContentWidth);};
    // Build aligned numeric columns once; source labels do not repeat per stat.
    auto AddValues=[&](TSharedRef<SVerticalBox> Rows,const FString& Label,const FString& Base,const FString& Current,const FString& Magic)
    {
        TSharedRef<SHorizontalBox> Row=SNew(SHorizontalBox);
        Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)[Text(Label,11,FLinearColor(.95,.92,.8),LabelWidth)];
        for(const auto& Entry:TArray<TPair<FString,FLinearColor>>{{Base,BaseColor},{Current,TinkerColor},{Magic,CantripColor}})
        {
            auto Value=Text(Entry.Key,10,Entry.Value,ValueWidth);Value->SetJustification(ETextJustify::Right);
            Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(ValueWidth)[Value]];
        }
        Rows->AddSlot().AutoHeight()[Row];
    };
    TSharedRef<SVerticalBox> Rows=SNew(SVerticalBox);
    TSharedRef<SHorizontalBox> Header=SNew(SHorizontalBox);
    if(Resources)Header->AddSlot().AutoWidth().Padding(0,0,8,0)
        [SNew(SBox).WidthOverride(32).HeightOverride(32)[SNew(SACELootArt)
        .Background(Resources->ResolveItemBackground(Item.ItemType,Item.IconUnderlayId))
        .Foreground(Resources->ResolveItemForeground(Item.IconId,Item.IconOverlayId,Item.UiEffects,false,Item.Structure,Item.MaxStructure))]];
    Header->AddSlot().FillWidth(1)[Text(Name,15,R.bRated?ACELootQuality::Color(R.Grade):FLinearColor(.95,.92,.8),ContentWidth-(Resources?40:0))];
    Rows->AddSlot().AutoHeight()[Header];
    FString Class=R.Class.Replace(TEXT(" Weapons"),TEXT("")).Replace(TEXT(" / Elemental caster"),TEXT(""))
        .Replace(TEXT(" / combat spell package"),TEXT("")).Replace(TEXT(" / spell package"),TEXT(""));
    TSharedRef<SHorizontalBox> TypeLine=SNew(SHorizontalBox);
    TypeLine->AddSlot().FillWidth(1)[Text(Class,10,FLinearColor(.8,.82,.87),ContentWidth-90)];
    if(!R.DamageType.IsEmpty())
    {
        const uint32 Effect=R.DamageType==TEXT("Cold")?0x80:R.DamageType==TEXT("Fire")?0x20:
            R.DamageType==TEXT("Acid")?0x100:R.DamageType==TEXT("Electric")?0x40:
            R.DamageType==TEXT("Slashing")?0x400:R.DamageType==TEXT("Piercing")?0x800:
            R.DamageType==TEXT("Bludgeoning")?0x200:R.DamageType==TEXT("Nether")?0x1000:0;
        TypeLine->AddSlot().AutoWidth()[Text(R.DamageType,10,Resources?Resources->ResolveItemEffectColor(Effect):CantripColor,85)];
    }
    Rows->AddSlot().AutoHeight().Padding(0,3)[TypeLine];
    FString Status=R.bRated?FString::Printf(TEXT("%s · %.1f"),*ACELootQuality::Name(R.Grade),R.Points):
        (!R.bOriginalKnown&&R.Tinkers>0?TEXT("Base unknown"):TEXT("Unrated"));
    if(R.bRated&&R.Class.Contains(TEXT("spell package"))&&R.Class.Contains(TEXT("Armor")))Status+=TEXT(" · spells");
    if(R.Stats.ContainsByPredicate([](const FACEQualityStat& S){return !S.bPermanentKnown;}))Status=TEXT("Unverified");
    Rows->AddSlot().AutoHeight().Padding(0,2,0,5)[T(Status,12,R.bRated?ACELootQuality::Color(R.Grade):FLinearColor(.72,.74,.8))];
    if(!R.Stats.IsEmpty())AddValues(Rows,TEXT(""),TEXT("Base"),R.Tinkers>0?TEXT("Tinkered"):TEXT("Current"),TEXT("Cantrip"));
    if(R.Stats.Num()>=2&&R.Stats[0].Label==TEXT("Maximum damage")&&R.Stats[1].Label==TEXT("Damage variance"))
    {
        const auto& D=R.Stats[0];const auto& V=R.Stats[1];
        auto Hit=[](double Damage,double Variance){return FString::Printf(TEXT("%.1f–%.1f"),Damage*(1-Variance),Damage);};
        AddValues(Rows,TEXT("Per hit"),D.bOriginalKnown&&V.bOriginalKnown?Hit(D.Original,V.Original):TEXT("—"),
            Hit(D.Permanent,V.Permanent),FMath::Abs(D.WithCantrip-D.Permanent)>1.e-5?Hit(D.WithCantrip,V.Permanent):TEXT("—"));
    }
    for(const auto& S:R.Stats)
    {
        FString Label=S.Label;
        if(Label==TEXT("Maximum damage"))Label=TEXT("Max damage");
        else if(Label==TEXT("Damage variance"))Label=TEXT("Variance");
        else if(Label==TEXT("Melee attack bonus"))Label=TEXT("Attack");
        else if(Label==TEXT("Weapon melee-defense bonus"))Label=TEXT("Melee defense");
        else if(Label==TEXT("Matching spell-damage bonus"))Label=TEXT("Spell damage");
        else if(Label==TEXT("Item mana-conversion bonus"))Label=TEXT("Mana conversion");
        if(S.Bounds.Variable())Label+=TEXT("\n")+ACELootQuality::FormatStat(S.Bounds.Low,S.bPercent)+TEXT("–")+ACELootQuality::FormatStat(S.Bounds.High,S.bPercent);
        auto Stage=[&](double Value,double From,bool Delta)
        {
            FString Result=ACELootQuality::FormatStat(Value,S.bPercent);
            if(Delta&&FMath::Abs(Value-From)>1.e-5)Result+=S.bPercent?FString::Printf(TEXT("\n%+.1f pp"),(Value-From)*100):FString::Printf(TEXT("\n%+.1f"),Value-From);
            return Result;
        };
        Rows->AddSlot().AutoHeight().Padding(0,5,0,0)[SNullWidget::NullWidget];
        AddValues(Rows,Label,S.bOriginalKnown?Stage(S.Original,0,false):TEXT("—"),Stage(S.Permanent,S.Original,S.bOriginalKnown&&S.bPermanentKnown),
            S.bPermanentKnown&&FMath::Abs(S.WithCantrip-S.Permanent)>1.e-5?Stage(S.WithCantrip,S.Permanent,true):TEXT("—"));
        if(S.Bounds.Variable())Rows->AddSlot().AutoHeight()[SNew(SACELootBar).Stat(S)];
    }
    if(!R.Spells.IsEmpty())
    {
        TArray<FString> Names;for(const auto& Spell:R.Spells)Names.Add(Spell.Name);
        Rows->AddSlot().AutoHeight().Padding(0,4,0,0)[T(FString::Join(Names,TEXT(" · ")),10,CantripColor)];
    }
    TArray<FString> Checks;R.Requirements.ParseIntoArrayLines(Checks,true);
    if(Checks.Num()>1&&Checks[0].Contains(TEXT("skill ")))Checks.RemoveAt(0);
    for(auto& Check:Checks)
    {
        if(Check==TEXT("Wield requirements met")){Check.Empty();continue;}
        Check=Check.Replace(TEXT("Wield requirements: server check needed"),TEXT("Server check"))
            .Replace(TEXT(" base"),TEXT("")).Replace(TEXT("War Magic"),TEXT("War"))
            .Replace(TEXT("Heavy Weapons"),TEXT("Heavy")).Replace(TEXT("Light Weapons"),TEXT("Light"))
            .Replace(TEXT("Finesse Weapons"),TEXT("Finesse")).Replace(TEXT("Two Handed Combat"),TEXT("Two-handed"));
    }
    Checks.RemoveAll([](const FString& S){return S.IsEmpty();});
    FString Requirements=FString::Join(Checks,TEXT(" · "));
    if(!R.Activation.IsEmpty()&&!R.Activation.StartsWith(TEXT("No Arcane Lore")))
    {
        if(!Requirements.IsEmpty())Requirements+=TEXT(" · ");
        Requirements+=R.Activation.Replace(TEXT("Arcane Lore"),TEXT("Lore"));
    }
    if(!Requirements.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,5,0,0)[T(Requirements,10,
        R.bCannotWield||R.bActivationBlocked?FLinearColor(1,.55,.55):FLinearColor(.7,.82,.7))];
    UACEHoverTooltipWidget::SetWidgetContentTooltip(Owner,FText::FromString(Name),
        SNew(SBox).WidthOverride(Width)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(R.bRated?ACELootQuality::Color(R.Grade):FLinearColor(.35,.35,.4)).Padding(1)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.018,.022,.033,.98)).Padding(9)[Rows]]]);
}
