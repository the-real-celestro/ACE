#include "Mods/ACEPluginStyle.h"
#include "Mods/ACEPluginSubsystem.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"

namespace
{
    FLinearColor Color(EACEUCMLogLevel Level)
    {return Level==EACEUCMLogLevel::Error?FLinearColor(1,.35f,.3f):Level==EACEUCMLogLevel::Warning?FLinearColor(1,.73f,.28f):FLinearColor(.78f,.86f,.92f);}
    /** Codex: the log list sits directly on the window (no recessed gray panel). */
    const FTableViewStyle& QuietList()
    {static FTableViewStyle S=[]{FTableViewStyle T=FCoreStyle::Get().GetWidgetStyle<FTableViewStyle>("ListView");T.SetBackgroundBrush(FSlateNoResource());return T;}();return S;}
    const FTableRowStyle& QuietRow()
    {static FTableRowStyle S=[]{FTableRowStyle T=FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
        T.SetEvenRowBackgroundBrush(FSlateNoResource()).SetOddRowBackgroundBrush(FSlateNoResource())
         .SetEvenRowBackgroundHoveredBrush(FSlateColorBrush(FLinearColor(1,1,1,.03f))).SetOddRowBackgroundHoveredBrush(FSlateColorBrush(FLinearColor(1,1,1,.03f)));return T;}();return S;}
    class SACEUCMLogPanel : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SACEUCMLogPanel) {} SLATE_ARGUMENT(UACEPluginSubsystem*,Host) SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            Host=Args._Host;FACEPluginStyle::Use(Args._Host,TEXT("ucm.log"));
            auto Controls=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8,4));
            Controls->AddSlot()[SNew(SCheckBox).Style(&FACEPluginStyle::Current()->Check)
                .IsChecked_Lambda([this]{return ProblemsOnly?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState V){ProblemsOnly=V==ECheckBoxState::Checked;Refresh();})
                [SNew(SACEPluginText).Text(FText::FromString(TEXT("Problems only")))]];
            Controls->AddSlot()[SNew(SCheckBox).Style(&FACEPluginStyle::Current()->Check)
                .IsChecked_Lambda([this]{return Follow?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState V){Follow=V==ECheckBoxState::Checked;if(Follow){FollowLayoutPasses=3;List->ScrollToBottom();}})
                [SNew(SACEPluginText).Text(FText::FromString(TEXT("Follow latest")))]];
            Controls->AddSlot()[SNew(SACEPluginButton).ContentPadding(FMargin(FACEPluginStyle::ButtonPadding)).ButtonStyle(FACEPluginStyle::IsCodex()?&FACEPluginStyle::Current()->DockButton:&FACEPluginStyle::Current()->Button).IsFocusable(false)
                .Text(FText::FromString(TEXT("Copy log"))).OnClicked_Lambda([this]
                {
                    FString Text;for(const auto& E:Rows){Text+=E->ToText();Text+=TEXT("\r\n\r\n");}
                    FPlatformApplicationMisc::ClipboardCopy(*Text);return FReply::Handled();
                })];
            Controls->AddSlot()[SNew(SACEPluginButton).ContentPadding(FMargin(FACEPluginStyle::ButtonPadding)).ButtonStyle(FACEPluginStyle::IsCodex()?&FACEPluginStyle::Current()->DockButton:&FACEPluginStyle::Current()->Button).IsFocusable(false)
                .Text(FText::FromString(TEXT("Clear"))).OnClicked_Lambda([this]{if(Host.IsValid())Host->ClearUCMLog();Refresh();return FReply::Handled();})];
            ChildSlot[SNew(SBorder).Padding(8).BorderImage(&FACEPluginStyle::Current()->Background)
                [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Controls]
                +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)[SNew(SACEPluginText).AutoWrapText(true)
                    .Visibility_Lambda([this]{return Host.IsValid()&&Host->GetUCMLog().GetLastProblem().IsValid()?EVisibility::Visible:EVisibility::Collapsed;})
                    .ColorAndOpacity(Color(EACEUCMLogLevel::Warning)).Text_Lambda([this]
                    {
                        if(!Host.IsValid())return FText::GetEmpty();const auto E=Host->GetUCMLog().GetLastProblem();
                        return FText::FromString(E?TEXT("Latest problem: ")+E->ToText():TEXT("No problems recorded."));
                    })]
                +SVerticalBox::Slot().FillHeight(1)[SAssignNew(List,SListView<TSharedPtr<FACEUCMLogEntry>>)
                    .ListItemsSource(&Rows).SelectionMode(ESelectionMode::None)
                    .ListViewStyle(FACEPluginStyle::IsCodex()?&QuietList():&FCoreStyle::Get().GetWidgetStyle<FTableViewStyle>("ListView"))
                    .OnGenerateRow_Lambda([](TSharedPtr<FACEUCMLogEntry> E,const TSharedRef<STableViewBase>& Owner)
                    {
                        return SNew(STableRow<TSharedPtr<FACEUCMLogEntry>>,Owner).Padding(FMargin(3,4)).Style(FACEPluginStyle::IsCodex()?&QuietRow():&FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"))
                            [SNew(SACEPluginText).AutoWrapText(true).Font(FACEPluginStyle::NativeFont("Regular",12))
                            .ColorAndOpacity(Color(E->Level)).Text_Lambda([E]{return FText::FromString(E->ToText());})];
                    })]
                +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[SNew(SACEPluginText).AutoWrapText(true)
                    .Font(FACEPluginStyle::NativeFont("Regular",11)).ColorAndOpacity(FLinearColor(.55f,.65f,.72f))
                    .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Oldest events are replaced when full. Logs are saved across restarts and continue when this window is closed."))))
                    .Text_Lambda([this]
                    {
                        if(!Host.IsValid())return FText::GetEmpty();
                        return FText::FromString(FString::Printf(TEXT("%d / %d events%s"),
                            Host->GetUCMLog().GetEntries().Num(),FACEUCMLog::Capacity,
                            Host->UCMLogSaveError.IsEmpty()?TEXT(""):*(TEXT("\n")+Host->UCMLogSaveError)));
                    })]]];
            Refresh();
        }
        virtual void Tick(const FGeometry& G,double Time,float Delta) override
        {
            SCompoundWidget::Tick(G,Time,Delta);
            if(Host.IsValid()&&SeenRevision!=Host->GetUCMLog().Revision)
            {
                // Slate reports the remaining fraction, not a pixel distance.
                if(FollowLayoutPasses==0&&!Rows.IsEmpty()&&List->GetScrollDistanceRemaining().Y>.001)Follow=false;
                Refresh();
            }
            // Wrapped rows acquire their actual height after the first Slate
            // layout. Reapply the end offset while that layout settles.
            if(Follow&&FollowLayoutPasses>0){List->ScrollToBottom();--FollowLayoutPasses;}
        }
    private:
        TWeakObjectPtr<UACEPluginSubsystem> Host;
        TArray<TSharedPtr<FACEUCMLogEntry>> Rows;
        TSharedPtr<SListView<TSharedPtr<FACEUCMLogEntry>>> List;
        uint64 SeenRevision=MAX_uint64;
        bool ProblemsOnly=false,Follow=true;
        int32 FollowLayoutPasses=0;
        void Refresh()
        {
            Rows.Reset();if(Host.IsValid())
            {
                for(const auto& E:Host->GetUCMLog().GetEntries())if(!ProblemsOnly||E->Level!=EACEUCMLogLevel::Info)Rows.Add(E);
                SeenRevision=Host->GetUCMLog().Revision;
            }
            if(List){List->RequestListRefresh();if(Follow){FollowLayoutPasses=3;List->ScrollToBottom();}}
        }
    };
}
TSharedRef<SWidget> UACEPluginSubsystem::MakeUCMLogPanel() { return SNew(SACEUCMLogPanel).Host(this); }
