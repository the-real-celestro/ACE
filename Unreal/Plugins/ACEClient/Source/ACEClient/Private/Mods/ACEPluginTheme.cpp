#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginDesktop.h"
#include "Mods/ACEPluginStyle.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACELobbyArt.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

/** UCM page to open first in a freshly built UCM panel (non-shipping plugin preview shots only). */
FString GACEPluginPreviewUCMPage;

FString UACEPluginSubsystem::GetPluginTheme() const
{
    FString Theme=TEXT("codex");if(Settings)Settings->TryGetStringField(TEXT("_theme"),Theme);
    return Theme.Equals(TEXT("original"),ESearchCase::IgnoreCase)?TEXT("original"):TEXT("codex");
}
void UACEPluginSubsystem::SetPluginTheme(const FString& Theme)
{
    if(!Settings)Settings=MakeShared<FJsonObject>();
    const FString Previous=GetPluginTheme();
    Settings->SetStringField(TEXT("_theme"),Theme);SaveSettings();
    if(GetPluginTheme()!=Previous){ApplyPluginTheme();RebuildPluginWindows();}
}
bool UACEPluginSubsystem::IsMatchWorldColor() const
{
    bool Match=true;if(Settings)Settings->TryGetBoolField(TEXT("_match_world_color"),Match);return Match;
}
void UACEPluginSubsystem::SetMatchWorldColor(bool bMatch)
{
    if(!Settings)Settings=MakeShared<FJsonObject>();
    Settings->SetBoolField(TEXT("_match_world_color"),bMatch);SaveSettings();
    FACEPluginStyle::SetMatchWorldColor(bMatch);
}
bool UACEPluginSubsystem::IsPluginOnBar(const FString& Id) const
{
    const TSharedPtr<FJsonObject>* Hidden=nullptr;bool bHidden=false;
    if(Settings&&Settings->TryGetObjectField(TEXT("_bar_hidden"),Hidden))(*Hidden)->TryGetBoolField(Id,bHidden);
    return !bHidden;
}
void UACEPluginSubsystem::SetPluginOnBar(const FString& Id,bool bShow)
{
    if(Id.IsEmpty())return;if(!Settings)Settings=MakeShared<FJsonObject>();
    const TSharedPtr<FJsonObject>* Existing=nullptr;
    auto Hidden=Settings->TryGetObjectField(TEXT("_bar_hidden"),Existing)?*Existing:MakeShared<FJsonObject>();
    if(bShow)Hidden->RemoveField(Id);else Hidden->SetBoolField(Id,true);
    Settings->SetObjectField(TEXT("_bar_hidden"),Hidden);SaveSettings();
    if(DesktopDock)DesktopDock->Refresh();
}
void UACEPluginSubsystem::ApplyPluginTheme()
{
    FACEPluginStyle::SetTheme(GetPluginTheme());
    FACEPluginStyle::SetMatchWorldColor(IsMatchWorldColor());
}
void UACEPluginSubsystem::RebuildPluginWindows()
{
    // Brushes are rebuilt in place, then every window is recreated next tick (outside the
    // click that asked for it) so theme-specific layout such as the UCM rail is rebuilt too.
    if(RetailPluginStyle)RetailPluginStyle->Reinitialize();
    for(auto& Pair:WindowPluginStyles)if(Pair.Value)Pair.Value->Reinitialize();
    FTSTicker::GetCoreTicker().RemoveTicker(ThemeRebuildTicker);
    ThemeRebuildTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float)
    {
        ThemeRebuildTicker.Reset();
        if(DesktopDock){RemoveDesktopDock();UpdateDesktopDock();}
        if(DesktopPanel){TogglePanel();if(!DesktopPanel)TogglePanel();}
        return false;
    }));
}

void UACEPluginSubsystem::TickPluginPreview()
{
#if !UE_BUILD_SHIPPING
    if(PreviewStep<0)
    {
        if(PreviewStep==-2||!FParse::Param(FCommandLine::Get(),TEXT("ACEPluginPreviewShot"))){PreviewStep=-2;return;}
        PreviewStep=0;PreviewNext=FPlatformTime::Seconds()+8;
    }
    const double Now=FPlatformTime::Seconds();
    if(Now<PreviewNext)return;
    auto* GI=GetGameInstance();auto* Viewport=GI?GI->GetGameViewportClient():nullptr;
    auto* Client=GI?GI->GetSubsystem<UACEClientSubsystem>():nullptr;
    auto* Dat=GI?GI->GetSubsystem<UACEDatSubsystem>():nullptr;
    if(!Viewport||!Client)return;
    auto Shot=[](const TCHAR* Name){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/Name,true,false);};
    auto Enable=[this](const TCHAR* Id){for(auto& P:Plugins)if(P->Id==Id)P->Enabled=true;};
    auto Desktop=[this,Viewport]()
    {
        if(PreviewDesktop)Viewport->RemoveViewportWidgetContent(PreviewDesktop.ToSharedRef());
        PreviewDesktop=SNew(SACEPluginDesktop).Host(this);
        Viewport->AddViewportWidgetContent(PreviewDesktop.ToSharedRef(),1500);
    };
    auto Only=[this](const TCHAR* Id)
    {
        TArray<FString> Ids={TEXT("ucm.log"),TEXT("waypoint.map"),TEXT("waypoint.arrow"),TEXT("waypoint.dungeon")};
        for(const auto& P:Plugins)Ids.Add(P->Id);
        for(const FString& Other:Ids)
        {
            const bool Want=Other==Id;
            if(PreviewDesktop->IsOpen(Other)!=Want)PreviewDesktop->Toggle(Other);
        }
    };
    // Every UCM page is captured in rail order (UCM_<Page>.png), each in a freshly built desktop.
    static const TCHAR* const UCMPages[]={TEXT("Overview"),TEXT("Buffs"),TEXT("Buff others"),TEXT("Combat"),TEXT("Recovery"),TEXT("Route"),
        TEXT("Loot"),TEXT("Vendors"),TEXT("Monsters"),TEXT("Metas"),TEXT("Import"),TEXT("Log")};
    constexpr int32 PageFirst=4,PageEnd=PageFirst+UE_ARRAY_COUNT(UCMPages)*2;
    if(PreviewStep>=PageFirst&&PreviewStep<PageEnd)
    {
        const FString PageName=UCMPages[(PreviewStep-PageFirst)/2];
        if((PreviewStep-PageFirst)%2==0){GACEPluginPreviewUCMPage=PageName;Desktop();Only(TEXT("ucm"));PreviewNext=Now+3;}
        else{Shot(*(TEXT("UCM_")+PageName.Replace(TEXT(" "),TEXT(""))+TEXT(".png")));PreviewNext=Now+1.5;}
        ++PreviewStep;return;
    }
    switch(PreviewStep>=PageEnd?PreviewStep-PageEnd+7:PreviewStep)
    {
    case 0:
        // Wait for the DAT (fonts, retail art) and the lobby's art decode.
        if(!Dat||!Dat->IsDatReady()||!ACELobbyArt::Load(Dat)){PreviewNext=Now+1;return;}
        UE_LOG(LogTemp,Display,TEXT("Plugin preview: theme %s, world tint %d (%s)"),*FACEPluginStyle::ThemeName(),FACEPluginStyle::WindowTint(),*ACELobbyArt::WorldName());
        SavePluginWindowPosition(TEXT("__manager"),FVector2D(250,70));SavePluginWindowPosition(TEXT("__manager.size"),FVector2D(1030,650));
        if(!DesktopPanel)TogglePanel();
        PreviewNext=Now+4;break;
    case 1: Shot(TEXT("PluginManager.png"));PreviewNext=Now+1.5;break;
    case 2:
        if(DesktopPanel)TogglePanel();
        Enable(TEXT("ucm"));Enable(TEXT("huntledger"));
        SavePluginWindowPosition(TEXT("ucm"),FVector2D(290,70));SavePluginWindowPosition(TEXT("ucm.size"),FVector2D(870,700));
        SavePluginWindowPosition(TEXT("huntledger"),FVector2D(1240,110));
        // Show the framed state. Preview mode is a non-shipping capture aid; the capture script restores settings.
        SetPluginWindowPinned(TEXT("ucm"),false);SetPluginWindowPinned(TEXT("huntledger"),false);
        PreviewNext=Now+2;break;
    // Warm-up: the first window built after the manager closes is laid out before the desktop
    // knows its size, so build once here and capture pages from the second build onward.
    case 3: GACEPluginPreviewUCMPage=TEXT("Overview");Desktop();Only(TEXT("ucm"));PreviewNext=Now+3;break;
    case 7: GACEPluginPreviewUCMPage.Reset();SavePluginWindowPosition(TEXT("huntledger"),FVector2D(1240,110));Only(TEXT("huntledger"));PreviewNext=Now+3;break;
    case 8: Shot(TEXT("HuntLedger.png"));PreviewNext=Now+1.5;break;
    case 9:
        // All three together (closest to States_Plugins: bar, manager and windows at once).
        Only(TEXT("ucm"));PreviewDesktop->Toggle(TEXT("huntledger"));
        PreviewNext=Now+3;break;
    case 10: Shot(TEXT("PluginsTogether.png"));PreviewNext=Now+2;break;
    case 11: UE_LOG(LogTemp,Display,TEXT("Plugin preview: done"));FPlatformMisc::RequestExit(false);PreviewNext=Now+60;break;
    default: return;
    }
    ++PreviewStep;
#endif
}
