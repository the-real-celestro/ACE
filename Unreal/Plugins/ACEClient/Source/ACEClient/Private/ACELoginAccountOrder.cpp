#include "ACELoginWidget.h"
#include "ACELobbyAccountList.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/DragAndDrop.h"
#include "Rendering/DrawElements.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SOverlay.h"
#include "ACELobbyArt.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
#if !UE_BUILD_SHIPPING
    FString GDropPreviewId; // -ACELobbyShot: draw the insertion line without a live drag.
#endif
    // Only this launcher's account payload can change the order. The drag
    // contains an identity and display label, never account credentials.
    class FAccountOrderDrag : public FDragDropOperation
    {
    public:
        DRAG_DROP_OPERATOR_TYPE(FAccountOrderDrag,FDragDropOperation)
        TWeakObjectPtr<UACELoginWidget> Owner;
        FString Id,Title;
        FVector2D Position,GrabOffset;
        TSharedPtr<SWidget> Preview;

        static TSharedRef<FAccountOrderDrag> New(UACELoginWidget* Owner,const FString& Id,
            const FString& Title,const FVector2D& Position,const FGeometry& Geometry,const FVector2D& PressPosition)
        {
            auto Op=MakeShared<FAccountOrderDrag>();
            Op->Owner=Owner; Op->Id=Id; Op->Title=Title; Op->Position=Position;
            Op->GrabOffset=PressPosition-Geometry.GetAbsolutePosition();
            // Snapshot display-only fields, never the login credentials or form contents.
            const auto* Item=Owner?Owner->AccountDisplayItem(Id):nullptr;
            const FString Name=Item?Item->Title:Title, User=Item?Item->Username:FString(), When=Item?Item->When:FString();
            const bool Favorite=Item&&Item->bFavorite;
            static const FSlateRoundedBoxBrush Face(FLinearColor(FColor(0x18,0x22,0x30)),0.f,FLinearColor(FColor(0xa5,0x87,0x4e)),1.f);
            const auto Label=[](const FString& Text,int32 Size,FLinearColor Color,const TCHAR* Tag)
            { return SNew(STextBlock).Tag(FName(Tag)).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size))
                .ColorAndOpacity(Color).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor(0,0,0,.7f)).OverflowPolicy(ETextOverflowPolicy::Ellipsis); };
            auto UserLabel=Label(User,9,FLinearColor(FColor(0x8f,0x81,0x66)),TEXT("AccountDragUsername"));
            UserLabel->SetVisibility(User.IsEmpty()?EVisibility::Collapsed:EVisibility::Visible);
            auto Names=SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[Label(Name,11,FLinearColor(FColor(0xff,0xf2,0xcc)),TEXT("AccountDragTitle"))]
                +SVerticalBox::Slot().AutoHeight()[UserLabel];
            auto Card=SNew(SBox).WidthOverride(Geometry.GetLocalSize().X).HeightOverride(UACELobbyAccountRow::RowHeight)
                [SNew(SBorder).BorderImage(&Face).Padding(FMargin(10,0,12,0))
                    [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)
                        [SNew(SBox).WidthOverride(14)[SNew(SACELobbyGlyph).Kind(SACELobbyGlyph::EKind::Star).Size(12).Filled(Favorite).Color(FLinearColor(FColor(0xd7,0xb0,0x6a)))]]
                    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Names]
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10,0,0,0)
                        [Label(When,9,FLinearColor(FColor(0xd8,0xc0,0x8a)),TEXT("AccountDragWhen"))]]];
            // Windowless drag previews paint in window pixels, including DPI and lobby scaling.
            Op->Preview=SNew(SBox).Tag(TEXT("AccountDragPreview")).WidthOverride(Geometry.GetAbsoluteSize().X).HeightOverride(Geometry.GetAbsoluteSize().Y)
                [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[Card]];
            Op->bCreateNewWindow=false; Op->MouseCursor=EMouseCursor::GrabHandClosed;
            Op->Construct(); return Op;
        }
        void OnDragged(const FDragDropEvent& Event) override
        { Position=Event.GetScreenSpacePosition(); FDragDropOperation::OnDragged(Event); }
        FVector2D GetDecoratorPosition() const override { return Position-GrabOffset; }
        TSharedPtr<SWidget> GetDefaultDecorator() const override { return Preview; }
    };

    // Full-row drag source and drop target. Identity is read through
    // attributes because the Play page's virtualised rows are recycled.
    class SAccountOrderRow : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SAccountOrderRow):_Owner(nullptr) {}
            SLATE_ATTRIBUTE(UACELoginWidget*,Owner)
            SLATE_ATTRIBUTE(FString,Id)
            SLATE_ATTRIBUTE(FString,Title)
            SLATE_DEFAULT_SLOT(FArguments,Content)
        SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            Owner=Args._Owner; Id=Args._Id; Title=Args._Title;
            SetTag(FName(*(TEXT("AccountOrderRow.")+Id.Get())));
            SetCursor(TAttribute<TOptional<EMouseCursor::Type>>::CreateLambda([this](){auto* Host=Owner.Get();return TOptional<EMouseCursor::Type>(Host&&Host->CanReorderAccounts()?EMouseCursor::GrabHand:EMouseCursor::Default);}));
            Content=Args._Content.Widget;
            ChildSlot[Content.ToSharedRef()];
        }
        FReply OnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& Event) override
        {
            auto* Host=Owner.Get();
            const float X=G.AbsoluteToLocal(Event.GetScreenSpacePosition()).X;
            // The favorite star keeps its own click. Every other part of the row can drag.
            if(Event.GetEffectingButton()!=EKeys::LeftMouseButton || (X>=8&&X<=28)
                || !Host || !Host->CanReorderAccounts() || Id.Get().IsEmpty())return FReply::Unhandled();
            Pressed=true;PressedId=Id.Get();PressPosition=Event.GetScreenSpacePosition();
            return FReply::Handled().CaptureMouse(SharedThis(this)).DetectDrag(SharedThis(this),EKeys::LeftMouseButton);
        }
        FReply OnMouseButtonUp(const FGeometry& G,const FPointerEvent& Event) override
        {
            if(!Pressed||Event.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
            Pressed=false;
            if(auto* Host=Owner.Get();Host&&PressedId==Id.Get()&&G.IsUnderLocation(Event.GetScreenSpacePosition()))
                Host->RunAction(TEXT("selectaccount"),PressedId);
            return FReply::Handled().ReleaseMouseCapture();
        }
        void OnMouseCaptureLost(const FCaptureLostEvent& Event) override
        { Pressed=false;SCompoundWidget::OnMouseCaptureLost(Event); }
        FReply OnDragDetected(const FGeometry& G,const FPointerEvent& Event) override
        {
            auto* Host=Owner.Get();
            if(!Pressed||!Host||!Host->CanReorderAccounts()||PressedId!=Id.Get())return FReply::Unhandled();
            Pressed=false;
            return FReply::Handled().BeginDragDrop(FAccountOrderDrag::New(Host,PressedId,Title.Get(),Event.GetScreenSpacePosition(),G,PressPosition));
        }
        void Tick(const FGeometry& G,double Time,float Delta) override
        {
            SCompoundWidget::Tick(G,Time,Delta);
            const auto Drag=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetDragDroppingContent():nullptr;
            const auto AccountDrag=Drag&&Drag->IsOfType<FAccountOrderDrag>()?StaticCastSharedPtr<FAccountOrderDrag>(Drag):nullptr;
            const bool IsSource=AccountDrag&&AccountDrag->Owner.Get()==Owner.Get()&&AccountDrag->Id==Id.Get();
            if(IsSource!=WasSource){Content->SetRenderOpacity(IsSource?.18f:1.f);WasSource=IsSource;}
            SetTag(FName(*(TEXT("AccountOrderRow.")+Id.Get())));
        }
        FReply OnDragOver(const FGeometry& G,const FDragDropEvent& E) override
        {
            Hovered=CanAccept(E); After=IsAfter(G,E); Invalidate(EInvalidateWidgetReason::Paint);
            return Hovered?FReply::Handled():FReply::Unhandled();
        }
        void OnDragLeave(const FDragDropEvent& E) override
        {
            Hovered=false; Invalidate(EInvalidateWidgetReason::Paint); SCompoundWidget::OnDragLeave(E);
        }
        FReply OnDrop(const FGeometry& G,const FDragDropEvent& E) override
        {
            Hovered=false; Invalidate(EInvalidateWidgetReason::Paint);
            if (!CanAccept(E)) return FReply::Unhandled();
            Owner.Get()->MoveAccountInOrder(E.GetOperationAs<FAccountOrderDrag>()->Id,Id.Get(),IsAfter(G,E));
            return FReply::Handled();
        }
        int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Culling,FSlateWindowElementList& Out,
            int32 Layer,const FWidgetStyle& Style,bool Enabled) const override
        {
            Layer=SCompoundWidget::OnPaint(Args,G,Culling,Out,Layer,Style,Enabled);
            bool bLine=Hovered, bBelow=After;
#if !UE_BUILD_SHIPPING
            if (!bLine && !GDropPreviewId.IsEmpty() && Id.Get()==GDropPreviewId) bLine=bBelow=true;
#endif
            if (bLine)
            {
                // Insertion line: a 2px gold hairline with small end dots, centred in the gap between rows.
                const FLinearColor Mod=Style.GetColorAndOpacityTint();
                const FLinearColor Edge(FColor(0xf0,0xad,0x42)), Mid(FColor(0xff,0xd7,0x7a));
                const float X0=0.f, W=FMath::Max(10.f,float(G.GetLocalSize().X)-X0);
                const float Y=bBelow?G.GetLocalSize().Y+2.f:-2.f;
                auto Band=[&](float H, float Alpha, int32 L)
                {
                    auto A=[&](FLinearColor C, float K){ C.A*=K*Alpha; return C*Mod; };
                    TArray<FSlateGradientStop> Stops;
                    Stops.Add(FSlateGradientStop(FVector2D(0,0),A(Edge,0)));
                    Stops.Add(FSlateGradientStop(FVector2D(W*.08f,0),A(Edge,1)));
                    Stops.Add(FSlateGradientStop(FVector2D(W*.5f,0),A(Mid,1)));
                    Stops.Add(FSlateGradientStop(FVector2D(W*.92f,0),A(Edge,1)));
                    Stops.Add(FSlateGradientStop(FVector2D(W,0),A(Edge,0)));
                    FSlateDrawElement::MakeGradient(Out,L,G.ToPaintGeometry(FVector2D(W,H),FSlateLayoutTransform(FVector2D(X0,Y-H*.5f))),Stops,Orient_Vertical);
                };
                Band(2.f,1.f,++Layer);
                static const FSlateRoundedBoxBrush Dot(FLinearColor::White,3.f);
                for (const float X:{X0+2.f,X0+W-8.f})
                    FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(6,6),FSlateLayoutTransform(FVector2D(X,Y-3.f))),&Dot,ESlateDrawEffect::None,Mid*Mod);
            }
            return Layer;
        }
    private:
        TAttribute<UACELoginWidget*> Owner;
        TAttribute<FString> Id,Title;
        TSharedPtr<SWidget> Content;
        FString PressedId;
        FVector2D PressPosition;
        bool Pressed=false,WasSource=false;
        bool Hovered=false,After=false;
        bool CanAccept(const FDragDropEvent& Event) const
        {
            const auto Op=Event.GetOperationAs<FAccountOrderDrag>();
            auto* Host=Owner.Get();
            return Op && Host && Op->Owner.Get()==Host && Host->CanMoveAccount(Op->Id,Id.Get());
        }
        static bool IsAfter(const FGeometry& G,const FDragDropEvent& E)
        {
            return G.AbsoluteToLocal(E.GetScreenSpacePosition()).Y>=G.GetLocalSize().Y*.5;
        }
    };
}

const UACELobbyAccountItem* UACELoginWidget::AccountDisplayItem(const FString& Id) const
{
    if(const auto* Found=AccountItems.Find(Id))return Found->Get();
    return nullptr;
}

TSharedRef<SWidget> ACEMakeAccountOrderRow(TAttribute<UACELoginWidget*> Owner,TAttribute<FString> Id,TAttribute<FString> Title,TSharedRef<SWidget> Content)
{
    return SNew(SAccountOrderRow).Owner(Owner).Id(Id).Title(Title)[Content];
}

#if !UE_BUILD_SHIPPING
void ACESetAccountDropPreview(const FString& Id) { GDropPreviewId=Id; }
#endif

void UACELoginWidget::TickAccountDrag(float DeltaTime)
{
    if (!CanReorderAccounts() || !FSlateApplication::IsInitialized()) return;
    const auto Op=FSlateApplication::Get().GetDragDroppingContent();
    if (!Op || !Op->IsOfType<FAccountOrderDrag>()) return;
    const auto Drag=StaticCastSharedPtr<FAccountOrderDrag>(Op);
    if (Drag->Owner.Get()!=this) return;
    // Scroll whichever Play-page list the pointer is near the top or bottom edge of.
    for (UACELobbyAccountListView* List:{FavoriteList.Get(),AccountList.Get()})
    {
        if (!List || !List->CanScroll()) continue;
        const auto& G=List->GetCachedGeometry();
        const auto P=G.AbsoluteToLocal(Drag->Position),Size=G.GetLocalSize();
        if (P.X<0 || P.X>Size.X || P.Y<0 || P.Y>Size.Y || Size.Y<=0) continue;
        const float Edge=FMath::Min(30.f,float(Size.Y*.25));
        const float Speed=P.Y<Edge?-(1.f-P.Y/Edge):P.Y>Size.Y-Edge?1.f-(Size.Y-P.Y)/Edge:0.f;
        if (Speed!=0) List->ScrollBy(Speed*12.f*FMath::Min(DeltaTime,.1f));
    }
}
