#include "ACELoginWidget.h"
#include "ACELobbyAccountList.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/DragAndDrop.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
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
        FVector2D Position;

        static TSharedRef<FAccountOrderDrag> New(UACELoginWidget* Owner,const FString& Id,
            const FString& Title,const FVector2D& Position)
        {
            auto Op=MakeShared<FAccountOrderDrag>();
            Op->Owner=Owner; Op->Id=Id; Op->Title=Title; Op->Position=Position;
            Op->bCreateNewWindow=false; Op->MouseCursor=EMouseCursor::GrabHandClosed;
            Op->Construct(); return Op;
        }
        void OnDragged(const FDragDropEvent& Event) override
        {
            Position=Event.GetScreenSpacePosition(); FDragDropOperation::OnDragged(Event);
        }
        TSharedPtr<SWidget> GetDefaultDecorator() const override
        {
            return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(.045f,.036f,.025f)).Padding(8)
                [SNew(STextBlock).Text(FText::FromString(Title)).Font(FCoreStyle::GetDefaultFontStyle("Regular",18))
                    .ColorAndOpacity(FLinearColor(.88f,.85f,.77f))];
        }
    };

    // Six-dot grip works before game data loads, without a font glyph dependency.
    // Compact mode is the subtle two-column grip beside each Play-page account row.
    class SAccountGripIcon : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SAccountGripIcon):_Compact(false),_Tint(FLinearColor::White) {} SLATE_ARGUMENT(bool,Compact) SLATE_ATTRIBUTE(FLinearColor,Tint) SLATE_END_ARGS()
        void Construct(const FArguments& Args) { Compact=Args._Compact; Tint=Args._Tint; SetVisibility(EVisibility::HitTestInvisible); }
        FVector2D ComputeDesiredSize(float) const override { return Compact?FVector2D(10,16):FVector2D(18,26); }
        int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,
            int32 Layer,const FWidgetStyle& Style,bool Enabled) const override
        {
            const auto Effect=ShouldBeEnabled(Enabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect;
            const FVector2D Box=ComputeDesiredSize(1);
            const float Dot=Compact?2.f:3.f, Pitch=Compact?5.f:7.f, Inset=Compact?2.f:4.f;
            const auto Offset=(G.GetLocalSize()-Box)*.5;
            for (int32 Y=0;Y<3;++Y) for (int32 X=0;X<2;++X)
                FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(Dot,Dot),
                    FSlateLayoutTransform(Offset+FVector2D(Inset+X*Pitch,Inset+Y*Pitch))),FCoreStyle::Get().GetBrush("WhiteBrush"),
                    Effect,FLinearColor(.88f,.85f,.77f)*Tint.Get()*Style.GetColorAndOpacityTint());
            return Layer;
        }
    private:
        bool Compact=false;
        TAttribute<FLinearColor> Tint;
    };

    // Grip + drop target around one account row. Identity is read through
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
            GripStyle=FButtonStyle().SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource()).SetPressed(FSlateNoResource())
                .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
            SetTag(FName(*(TEXT("AccountOrderRow.")+Id.Get())));
            ChildSlot
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,3,0)
                [SAssignNew(Grip,SButton).Tag(FName(*(TEXT("AccountOrderGrip.")+Id.Get())))
                    .ButtonStyle(&GripStyle).ContentPadding(FMargin(3,5)).IsFocusable(false)
                    .AllowDragDrop(true).Cursor(EMouseCursor::GrabHand)
                    .Visibility_Lambda([this]{ auto* Host=Owner.Get(); return Host&&Host->CanReorderAccounts()?EVisibility::Visible:EVisibility::Hidden; })
                    .ToolTipText(FText::FromString(TEXT("Drag to reorder. The list switches to Custom order and is saved automatically.")))
                    .OnSlateButtonDragDetected_Lambda([this](const FGeometry&,const FPointerEvent& Event)
                    {
                        auto* Host=Owner.Get();
                        if (!Host || !Host->CanReorderAccounts() || Id.Get().IsEmpty()) return FReply::Unhandled();
                        return FReply::Handled().BeginDragDrop(FAccountOrderDrag::New(Host,Id.Get(),Title.Get(),Event.GetScreenSpacePosition()));
                    })
                    [SNew(SAccountGripIcon).Compact(true)
                        .Tint_Lambda([this]{ return FLinearColor(1,1,1,Grip.IsValid()&&Grip->IsHovered()?.95f:.45f); })]]
                +SHorizontalBox::Slot().FillWidth(1)[Args._Content.Widget]];
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
                const float Y=bBelow?G.GetLocalSize().Y-1:1;
                FSlateDrawElement::MakeLines(Out,++Layer,G.ToPaintGeometry(),{FVector2D(0,Y),FVector2D(G.GetLocalSize().X,Y)},
                    ESlateDrawEffect::None,FLinearColor(.94f,.68f,.26f)*Style.GetColorAndOpacityTint(),true,2.f);
            }
            return Layer;
        }
    private:
        TAttribute<UACELoginWidget*> Owner;
        TAttribute<FString> Id,Title;
        FButtonStyle GripStyle;
        TSharedPtr<SButton> Grip;
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
