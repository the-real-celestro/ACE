#include "Mods/ACEPluginStyle.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "Engine/GameInstance.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
    class SMicroDot : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SMicroDot) {} SLATE_ATTRIBUTE(bool, On) SLATE_END_ARGS()
        void Construct(const FArguments& Args) { On=Args._On; }
        virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16,16); }
        virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool) const override
        {
            static const FSlateRoundedBoxBrush Ring(FLinearColor(.025f,.035f,.045f),FLinearColor(.68f,.62f,.45f),1.2f);
            static const FSlateRoundedBoxBrush Dot(FLinearColor(.18f,.78f,.32f));
            FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(14,14),FSlateLayoutTransform(FVector2D(1,1))),&Ring,ESlateDrawEffect::None,FLinearColor(.025f,.035f,.045f));
            if(On.Get(false))FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(6,6),FSlateLayoutTransform(FVector2D(5,5))),&Dot,ESlateDrawEffect::None,FLinearColor(.18f,.78f,.32f));
            return Layer+1;
        }
    private:
        TAttribute<bool> On;
    };
    // Independent checkbox semantics, with a round indicator drawn at native resolution.
    const FCheckBoxStyle* MicroIndicatorStyle()
    {
        static const FCheckBoxStyle Style=[]
        {
            FCheckBoxStyle S;
            S.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
                .SetUncheckedImage(FSlateNoResource()).SetUncheckedHoveredImage(FSlateNoResource()).SetUncheckedPressedImage(FSlateNoResource())
                .SetCheckedImage(FSlateNoResource()).SetCheckedHoveredImage(FSlateNoResource()).SetCheckedPressedImage(FSlateNoResource())
                .SetPadding(FMargin(0));
            return S;
        }();
        return &Style;
    }
    TSharedRef<SWidget> MicroLabel(const TCHAR* Label,TFunction<bool()> Read)
    {
        return SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)[SNew(SMicroDot).On_Lambda([Read](){return Read();})]
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SACEPluginText).Text(FText::FromString(Label)).AutoWrapText(true).Font(FACEPluginStyle::NativeFont("Regular",14))];
    }

    class SMicroActivities : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SMicroActivities) {} SLATE_ATTRIBUTE(bool, Condensed) SLATE_ATTRIBUTE(float, AvailableWidth) SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        { Condensed=Args._Condensed; AvailableWidth=Args._AvailableWidth; ChildSlot[SAssignNew(Grid,SUniformGridPanel).SlotPadding(FMargin(2))]; }
        bool IsSingleColumn() const { return Columns==1; }
        bool IsCondensed() const { return IsSingleColumn()&&Condensed.Get(false); }
        void Add(const TSharedRef<SWidget>& Widget)
        { Activities.Add(Widget); Reflow(); }
        virtual void Tick(const FGeometry& Geometry,double Time,float Delta) override
        {
            SCompoundWidget::Tick(Geometry,Time,Delta);
            // Base columns on the panel width, so a scrollbar appearing/disappearing
            // cannot change compact mode or disagree with the native window height.
            const float PanelWidth=AvailableWidth.Get(0);
            const int32 Wanted=(PanelWidth>0?PanelWidth:Geometry.GetLocalSize().X)<300.f?1:2;
            if (Wanted!=Columns || LastCondensed!=(Wanted==1&&Condensed.Get(false))) { Columns=Wanted; Reflow(); }
        }
    private:
        void Reflow()
        {
            Grid->ClearChildren(); LastCondensed=IsCondensed();
            const int32 Count=LastCondensed?FMath::Min(6,Activities.Num()):Activities.Num();
            for (int32 I=0;I<Count;++I)
                Grid->AddSlot(I%Columns,I/Columns).VAlign(VAlign_Center)[Activities[I]];
        }
        int32 Columns=2;
        bool LastCondensed=false;
        TAttribute<bool> Condensed;
        TAttribute<float> AvailableWidth;
        TSharedPtr<SUniformGridPanel> Grid;
        TArray<TSharedRef<SWidget>> Activities;
    };
}

bool UACEPluginSubsystem::IsUCMFollowEnabled() const
{
    auto P=Find(TEXT("ucm"));if(!P||!P->Profile)return false;
    bool Navigation=false;FString Name;double Id=0;
    P->Profile->TryGetBoolField(TEXT("navigation"),Navigation);
    P->Profile->TryGetStringField(TEXT("follow_name"),Name);P->Profile->TryGetNumberField(TEXT("follow_id"),Id);
    return Navigation&&(!Name.IsEmpty()||Id!=0);
}
bool UACEPluginSubsystem::SetUCMFollowSelected(bool Enabled)
{
    auto P=Find(TEXT("ucm"));if(!P||!P->Profile)return false;
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();FACEWorldObject Target;
    if(Enabled&&(!C->GetWorldObject(C->GetSelectedObject().Guid,Target)||!Target.bIsPlayer
        ||Target.Guid==C->GetPlayerGuid()||!Target.bHasPosition))
    {P->Status=TEXT("Select another player to follow");return false;}
    const auto Previous=MakeShared<FJsonObject>(*P->Profile);
    P->Profile->SetStringField(TEXT("follow_name"),Enabled?Target.Name:FString());
    P->Profile->SetNumberField(TEXT("follow_id"),Enabled?uint32(Target.Guid):0);
    // Stopping Follow must not unexpectedly begin walking an old recorded route.
    P->Profile->SetBoolField(TEXT("navigation"),Enabled);
    if(!SaveCurrentProfile(P->Id,false))
    {P->Profile=Previous;P->Status=TEXT("Could not save follow settings: ")+Notice;return false;}
    FollowMovementSamples.Reset();FollowMovementTarget=0;bFollowMovementOverflow=false;
    if(MovementOwner==P->Id){MovementOwner.Empty();MoveExpires=0;FaceHeading.Reset();}
    P->NextDecision=0;
    P->Status=Enabled?(P->Running?TEXT("Following: "):TEXT("Start UCM to follow: "))+Target.Name:TEXT("Follow stopped; navigation disabled");
    ScheduleDecisionWake(FPlatformTime::Seconds());
    return true;
}
void UACEPluginSubsystem::ObserveFollowPosition(int32 Guid,const FACEPosition& Position)
{
    auto P=Find(TEXT("ucm"));bool Corners=false;
    if(!P||!P->Running||!IsUCMFollowEnabled()||!P->Profile->TryGetBoolField(TEXT("follow_corners"),Corners)||!Corners)return;
    FString Name;double Id=0;P->Profile->TryGetStringField(TEXT("follow_name"),Name);P->Profile->TryGetNumberField(TEXT("follow_id"),Id);
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();FACEWorldObject Target;
    if(!C->GetWorldObject(Guid,Target)||!Target.bIsPlayer||(!Name.IsEmpty()?Target.Name!=Name:uint32(Guid)!=uint32(Id)))return;
    if(FollowMovementTarget!=Guid||FollowMovementTeleport!=C->GetTeleportSeq())
    {FollowMovementSamples.Reset();bFollowMovementOverflow=false;}
    FollowMovementTarget=Guid;FollowMovementTeleport=C->GetTeleportSeq();
    if(!FollowMovementSamples.IsEmpty()&&FVector::DistSquared(FollowMovementSamples.Last().ToUnrealLocation(),Position.ToUnrealLocation())<FMath::Square(9.6))return;
    if(FollowMovementSamples.Num()>=512){bFollowMovementOverflow=true;return;}
    FollowMovementSamples.Add(Position);
}
FString UACEPluginSubsystem::UCMCurrentStatus() const
{
    auto P=Find(TEXT("ucm"));if(!P)return TEXT("UCM unavailable");
    if(!P->Running)return P->Status;
    for(const TCHAR* Id:{TEXT("itemtools"),TEXT("autocraft")})if(auto Other=Find(Id);Other&&Other->Running)
        return TEXT("Paused while ")+Other->Name+TEXT(" runs");
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto S=C->GetSession();
    const auto Elapsed=[](double Start){return FString::Printf(TEXT(" (%ds)"),FMath::Max(0,int32(FPlatformTime::Seconds()-Start)));};
    if(S&&S->GetPendingEquipmentGuid())
    {
        FACEWorldObject Item;C->GetWorldObject(S->GetPendingEquipmentGuid(),Item);
        return TEXT("Switching equipment: ")+Item.Name;
    }
    if(InventoryInteractionPending())return TEXT("Waiting for inventory transfer");
    if(PendingSpell)
    {
        FString Name;uint32 Icon=0;GetGameInstance()->GetSubsystem<UACEDatSubsystem>()->TryGetSpellInfo(PendingSpell,Name,Icon);
        if(Name.IsEmpty())Name=FString::Printf(TEXT("spell %d"),PendingSpell);
        return (PendingSpellActivity==TEXT("buffs")||PendingSpellActivity==TEXT("buff_others")?FString(TEXT("Buffing: ")):FString(TEXT("Casting: ")))
            +Name+TEXT("; awaiting result")+Elapsed(PendingSpellAt);
    }
    if(ConfirmedBuffAckAt&&DeferredBuffIntent)return TEXT("Waiting for final buff acknowledgment")+Elapsed(ConfirmedBuffAckAt);
    if(P->CombatTransitionIntent)return FString(P->WaitingAttackCancel?TEXT("Waiting for attack to finish"):TEXT("Waiting for combat stance"))+Elapsed(P->CombatTransitionStarted);
    if(C->IsUseBusy())return TEXT("Waiting for game action acknowledgment");
    if(auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
    {
        if(PC->IsWorldTransitionActive())return TEXT("Waiting for world transition");
        if(PC->IsUseApproachActive())return TEXT("Approaching interaction target");
    }
    return P->Status;
}

TSharedRef<SWidget> UACEPluginSubsystem::MakeUCMMicroPanel()
{
    FACEPluginStyle::Use(this,TEXT("ucmmicro"));
    const TWeakObjectPtr<UACEPluginSubsystem> Host(this);
    const auto UCM=[Host]() -> TSharedPtr<FACEClientPlugin>
    {if(Host.IsValid())for(auto P:Host->Plugins)if(P->Id==TEXT("ucm"))return P;return nullptr;};
    auto Root=SNew(SBorder).BorderImage(&FACEPluginStyle::Current()->Background).Padding(6);
    auto Rows=SNew(SVerticalBox);
    auto Toggles=SNew(SMicroActivities).AvailableWidth_Lambda([Root=TWeakPtr<SBorder>(Root)](){auto Panel=Root.Pin();return Panel?float(Panel->GetCachedGeometry().GetLocalSize().X)-12.f:0.f;}).Condensed_Lambda([Host](){return Host.IsValid()&&Host->LedgerOption(TEXT("ucmmicro"),TEXT("condensed"));});
    const auto ExtraVisibility=[Toggles](){return Toggles->IsCondensed()?EVisibility::Collapsed:EVisibility::Visible;};
    const auto Row=[Toggles](const TCHAR* Label,TFunction<bool()> Read,TFunction<void(bool)> Write)
    {
        Toggles->Add(SNew(SCheckBox).Tag(FName(*(FString(TEXT("MicroToggle."))+Label))).Style(MicroIndicatorStyle()).IsFocusable(false).ClickMethod(EButtonClickMethod::MouseDown)
            .IsChecked_Lambda([Read](){return Read()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([Write](ECheckBoxState State){Write(State==ECheckBoxState::Checked);})
            [MicroLabel(Label,Read)]);
    };
    auto Header=SNew(SHorizontalBox);
    Header->AddSlot().FillWidth(1).VAlign(VAlign_Center)[SNew(SCheckBox).Tag(TEXT("MicroRun")).Style(MicroIndicatorStyle()).IsFocusable(false).ClickMethod(EButtonClickMethod::MouseDown)
        .ToolTip(FACEPluginStyle::Tooltip(TAttribute<FText>::CreateLambda([Host](){return FText::FromString(TEXT("UCM on/off. ")+(Host.IsValid()?Host->UCMCurrentStatus():FString()));})))
        .IsChecked_Lambda([UCM](){auto P=UCM();return P&&P->Running?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
        .OnCheckStateChanged_Lambda([Host,UCM](ECheckBoxState State)
        {
            if(Host.IsValid())if(auto P=UCM())
            {
                if(State==ECheckBoxState::Checked&&!P->Running)Host->Start(P->Id);
                else if(State==ECheckBoxState::Unchecked&&P->Running)Host->Stop(P->Id,TEXT("Stopped"),true);
            }
        })[MicroLabel(TEXT("UCM"),[UCM](){auto P=UCM();return P&&P->Running;})]];
    Header->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SACEPluginButton).Tag(TEXT("MicroCompact"))
        .Visibility_Lambda([Toggles](){return Toggles->IsSingleColumn()?EVisibility::Visible:EVisibility::Collapsed;})
        .ButtonStyle(&FACEPluginStyle::Current()->DockButton).ContentPadding(FMargin(4,1)).IsFocusable(false)
        .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Less: show six primary activities and shrink the window. More: restore all controls. Hidden activities keep their current settings."))))
        .OnClicked_Lambda([Host](){if(Host.IsValid())Host->SetLedgerOption(TEXT("ucmmicro"),TEXT("condensed"),!Host->LedgerOption(TEXT("ucmmicro"),TEXT("condensed")));return FReply::Handled();})
        [SNew(SACEPluginText).Font(FACEPluginStyle::NativeFont("Regular",13)).Text_Lambda([Toggles](){return FText::FromString(Toggles->IsCondensed()?TEXT("More"):TEXT("Less"));})]];
    Rows->AddSlot().AutoHeight().Padding(2,2)[Header];
    auto Quick=SNew(SWrapBox).Visibility_Lambda(ExtraVisibility).UseAllottedSize(true).InnerSlotPadding(FVector2D(3,3));
    const auto QuickButton=[&](const TCHAR* Label,const TCHAR* Help,TFunction<void()> Click,TAttribute<bool> Enabled)
    {
        Quick->AddSlot()[SNew(SACEPluginButton).ContentPadding(FMargin(FACEPluginStyle::CompactButtonPadding)).ButtonStyle(FACEPluginStyle::IsCodex()?&FACEPluginStyle::Current()->DockButton:&FACEPluginStyle::Current()->Button)
            .IsFocusable(false).ToolTip(FACEPluginStyle::Tooltip(FText::FromString(Help))).IsEnabled(Enabled)
            .OnClicked_Lambda([Click](){Click();return FReply::Handled();})
            [SNew(SACEPluginText).Text(FText::FromString(Label)).Font(FACEPluginStyle::NativeFont("Regular",14))]];
    };
    QuickButton(TEXT("F"),TEXT("Force Buff: refresh enabled buffs, even while UCM is stopped."),[Host](){if(Host.IsValid())Host->RequestForceBuff();},true);
    QuickButton(TEXT("CF"),TEXT("Cancel Force Buff after the current cast."),[Host](){if(Host.IsValid())Host->CancelForceBuff();},
        TAttribute<bool>::CreateLambda([Host](){return Host.IsValid()&&Host->IsForceBuffRequested();}));
    Quick->AddSlot()[SNew(SCheckBox).Style(MicroIndicatorStyle()).IsFocusable(false)
        .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Follow Character: select a player, enable FC, then start UCM. Disable to stop following."))))
        .IsChecked_Lambda([Host](){return Host.IsValid()&&Host->IsUCMFollowEnabled()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
        .OnCheckStateChanged_Lambda([Host](ECheckBoxState V){if(Host.IsValid())Host->SetUCMFollowSelected(V==ECheckBoxState::Checked);})
        [MicroLabel(TEXT("FC"),[Host](){return Host.IsValid()&&Host->IsUCMFollowEnabled();})]];
    Quick->AddSlot()[SNew(SCheckBox).Style(MicroIndicatorStyle()).IsFocusable(false)
        .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Corners: follow the player's observed footsteps in order instead of heading directly toward them."))))
        .IsChecked_Lambda([UCM](){auto P=UCM();bool On=false;if(P)P->Profile->TryGetBoolField(TEXT("follow_corners"),On);return On?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
        .OnCheckStateChanged_Lambda([Host,UCM](ECheckBoxState V){if(auto P=UCM();P&&Host.IsValid()){P->Profile->SetBoolField(TEXT("follow_corners"),V==ECheckBoxState::Checked);Host->SaveCurrentProfile(P->Id,false);}})
        [MicroLabel(TEXT("C"),[UCM](){auto P=UCM();bool On=false;if(P)P->Profile->TryGetBoolField(TEXT("follow_corners"),On);return On;})]];
    Rows->AddSlot().AutoHeight()[SNew(SBox).Visibility_Lambda(ExtraVisibility).Padding(FMargin(2,4))[Quick]];
    // Text length must never change the Micro window/drag-bar dimensions.
    Rows->AddSlot().AutoHeight().Padding(2,4)[SNew(SBox).Visibility_Lambda(ExtraVisibility).WidthOverride(1).HeightOverride(24).Clipping(EWidgetClipping::ClipToBounds)
        .ToolTip(FACEPluginStyle::Tooltip(TAttribute<FText>::CreateLambda([Host](){return FText::FromString(Host.IsValid()?Host->UCMCurrentStatus():FString());})))
        [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(SACEPluginText).AutoWrapText(false).Font(FACEPluginStyle::NativeFont("Regular",14))
            .Text_Lambda([Host](){return FText::FromString(Host.IsValid()?Host->UCMCurrentStatus():FString());})]]];
    Rows->AddSlot().AutoHeight()[Toggles];
    for(const auto& Entry:TArray<TPair<FString,FString>>{
        {TEXT("meta_enabled"),TEXT("Metas")},{TEXT("buffing"),TEXT("Buffing")},
        {TEXT("combat"),TEXT("Combat")},{TEXT("looting"),TEXT("Looting")},
        {TEXT("navigation"),TEXT("Navigation")},{TEXT("summon_pets"),TEXT("Summoning")},
        {TEXT("recovery"),TEXT("Vital recovery")},{TEXT("buff_others"),TEXT("Buff others")},
        {TEXT("vendor_restock"),TEXT("Vendor restocking")},{TEXT("idle_peace"),TEXT("Peace when idle")}})
    {
        const FString Key=Entry.Key;
        Row(*Entry.Value,[UCM,Key]()
        {
            auto P=UCM();if(!P)return false;
            if(Key==TEXT("combat")){FString Mode;P->Profile->TryGetStringField(Key,Mode);return !Mode.IsEmpty()&&Mode!=TEXT("off");}
            bool Value=Key==TEXT("recovery")||Key==TEXT("meta_enabled");P->Profile->TryGetBoolField(Key,Value);return Value;
        },[Host,UCM,Key](bool On)
        {
            if(!Host.IsValid())return;auto P=UCM();if(!P)return;
            if(Key==TEXT("combat")){P->Profile->SetStringField(Key,On?TEXT("auto"):TEXT("off"));P->Profile->SetBoolField(TEXT("manual_combat"),false);}
            else P->Profile->SetBoolField(Key,On);
            Host->SaveCurrentProfile(P->Id,false);
        });
    }
    Rows->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(2,4)[SNew(SACEPluginButton).Visibility_Lambda(ExtraVisibility).ContentPadding(FMargin(FACEPluginStyle::ButtonPadding)).ButtonStyle(FACEPluginStyle::IsCodex()?&FACEPluginStyle::Current()->DockButton:&FACEPluginStyle::Current()->Button).IsFocusable(false)

        .OnClicked_Lambda([Host](){if(Host.IsValid())Host->TogglePluginWindow(TEXT("ucm.log"));return FReply::Handled();})
        [SNew(SACEPluginText).Font(FACEPluginStyle::NativeFont("Regular",14)).Text(FText::FromString(TEXT("Open UCM Log")))]];
    Root->SetContent(SNew(SScrollBox)+SScrollBox::Slot()[Rows]);
    return Root;
}
