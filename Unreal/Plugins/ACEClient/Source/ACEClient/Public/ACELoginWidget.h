#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "ACETypes.h"
#include "ACELoginProfile.h"
#include "ACELoginWidget.generated.h"

class UACEClientSubsystem;
class UEditableTextBox;
class UTextBlock;
class UScrollBox;
class UVerticalBox;
class UBorder;
class UACELoginWidget;
class UACELobbyAccountListView;
class UACELobbyAccountItem;

/** Payload-bearing buttons avoid native popup windows, which do not render in VR. */
UCLASS()
class UACELoginActionButton : public UButton
{
	GENERATED_BODY()
public:
	FString Action, Value;
	bool bAccountDragHandle = false;
	/** Retail art used for this button (see ACELoginLobby.cpp), and whether it is the active/selected one. */
	uint8 LobbyStyle = 0;
	bool bLobbyAccent = false;
	UPROPERTY() TWeakObjectPtr<UACELoginWidget> Owner;
	UFUNCTION() void HandleAction();
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};

/** A single clickable character-list row. Carries its own list index so a shared OnClicked
 *  handler (dynamic multicast delegates can't carry extra payload data) can tell the owning
 *  UACELoginWidget which character was picked. */
UCLASS()
class UACECharacterRowButton : public UButton
{
	GENERATED_BODY()

public:
	int32 CharacterIndex = 0;

	UPROPERTY()
	TWeakObjectPtr<UACELoginWidget> OwnerWidget;

	UFUNCTION()
	void HandleClicked();
};

/**
 * Shared desktop/VR launcher lobby with local server/account profiles.
 * The existing retail character-selection screen takes over after authentication.
 */
UCLASS()
class ACECLIENT_API UACELoginWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void RunAction(const FString& Action, const FString& Value = FString());
	bool CanReorderAccounts() const;
	bool MoveAccountInOrder(const FString& Id, const FString& TargetId, bool bAfter);
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** Play page keyboard: type to jump, Backspace/Esc edit the search, arrows pick, Enter launches. */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyChar(const FGeometry& InGeometry, const FCharacterEvent& InCharEvent) override;

	/** True once EnsureDefaultLayout() has actually populated WidgetTree->RootWidget with our
	 *  real Canvas/Border/TextBox layout (as opposed to it still being null / a placeholder). */
	UFUNCTION(BlueprintCallable, Category = "ACE")
	bool IsDefaultLayoutBuilt() const { return HostBox != nullptr; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	FString DefaultHost;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	int32 DefaultPort = 9000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	FString DefaultAccount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	FString DefaultPassword;

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void DoLogin();

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void DoEnterSelectedCharacter();

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void SelectCharacterIndex(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "ACE")
	void SetStatus(const FString& Msg);

	/** Keep the launcher backdrop opaque, including when returning from the game. */
	void SetUseDatIntroBackdrop(bool bUse);

	/** Retail connect screen (local DAT layout 0x21000000): the title logo with the
	 *  Connect/Update progress vials, shown from Login until character selection. */
	void EndConnectScreen();
	bool IsConnectScreenVisible() const;
	/** True while the connect screen is still filling its vials; character select waits for it. */
	bool IsConnectScreenHoldingCharacterSelect() const;

protected:
	friend class FACEMissingDatLoginTest;
	friend class FACELauncherWidgetTest;
	friend class FACELauncherViewportTest;
	friend class FACEVRRigTest;
	FACELoginSettings Profile;
	FString ProfilePath; // Optional isolated storage for automation fixtures.
	TArray<FACELoginServer> Directory;
	FString EditingServerId, BrowserServerId, PendingRemoval;
	bool bEditingGDLE = false;
	bool bRefreshingDirectory = false;
	FVector2D LayoutView = FVector2D::ZeroVector;
	float LayoutBgScale = 1.f;
	int32 StyledPage = INDEX_NONE;
	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> DirectoryRequest;
	UPROPERTY() TObjectPtr<class UWidgetSwitcher> Pages;
	UPROPERTY() TObjectPtr<class UScaleBox> ResponsiveScale;
	UPROPERTY() TArray<TObjectPtr<UACELoginActionButton>> NavigationButtons;
	/** Right-hand column: tabs, status/confirmation and the active page. */
	UPROPERTY() TObjectPtr<class USizeBox> DashboardSize;
	UPROPERTY() TObjectPtr<class USizeBox> FooterSize;
	/** Left-hand column: Your Servers and the Accounts panel. */
	UPROPERTY() TObjectPtr<class USizeBox> SidebarSize;
	UPROPERTY() TObjectPtr<class USizeBox> ServerListSize;
	UPROPERTY() TObjectPtr<class USizeBox> PlayWindowSize;
	UPROPERTY() TObjectPtr<class USizeBox> LaunchSize;
	UPROPERTY() TObjectPtr<UBorder> PlayWindow;
	UPROPERTY() TObjectPtr<UBorder> SelectedAccountRow;
	UPROPERTY() TObjectPtr<class UImage> LobbyBackground;
	UPROPERTY() TObjectPtr<UTextBlock> LobbySubtitle;
	UPROPERTY() TObjectPtr<UTextBlock> LobbyBrand;
	UPROPERTY() TObjectPtr<UScrollBox> ServerList;
	/** Play page account picker: pinned favourites, then everything else (virtualised). */
	UPROPERTY() TObjectPtr<UACELobbyAccountListView> AccountList;
	UPROPERTY() TObjectPtr<UACELobbyAccountListView> FavoriteList;
	UPROPERTY() TObjectPtr<class USizeBox> FavoriteSize;
	UPROPERTY() TObjectPtr<UWidget> FavoriteHeader;
	UPROPERTY() TObjectPtr<UEditableTextBox> AccountSearchBox;
	UPROPERTY() TObjectPtr<UWidget> AccountSearchClear;
	UPROPERTY() TObjectPtr<UTextBlock> AccountsHeading;
	UPROPERTY() TObjectPtr<UTextBlock> AccountsEmpty;
	UPROPERTY() TObjectPtr<UTextBlock> SelectedAccountName;
	UPROPERTY() TObjectPtr<UTextBlock> LastLaunchedText;
	UPROPERTY() TObjectPtr<UWidget> AccountScrollUp;
	UPROPERTY() TObjectPtr<UWidget> AccountScrollDown;
	UPROPERTY() TArray<TObjectPtr<UACELoginActionButton>> SortButtons;
	UPROPERTY() TMap<FString, TObjectPtr<UACELobbyAccountItem>> AccountItems;
	TArray<TWeakPtr<class SWidget>> LobbyFrames;
	/** Account IDs in on-screen order (favourites first) for arrow-key selection. */
	TArray<FString> VisibleAccountOrder;
	bool bLobbyArtApplied = false;
	bool bSelectedAccountFavorite = false;
	void ApplyLobbyArt();
	void LayoutLobby(const FVector2D& View);
	void RefreshAccountLists();
	void UpdateSelectedAccountInfo();
	void MoveAccountSelection(int32 Delta);
	bool IsLobbyKeyboardActive() const;
	UFUNCTION() void OnAccountSearchChanged(const FText& Text);
	UFUNCTION() void OnAccountSearchCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UTextBlock* Heading(const FString& Text, int32 FancySize = 24);
	UWidget* Frame(UWidget* Content, const FMargin& Padding);
	UPROPERTY() TObjectPtr<UScrollBox> ManagedAccountList;
	UPROPERTY() TObjectPtr<UScrollBox> BrowserList;
	UPROPERTY() TObjectPtr<UTextBlock> ServerTitle;
	UPROPERTY() TObjectPtr<UTextBlock> ServerDetail;
	UPROPERTY() TObjectPtr<UTextBlock> ServerDescription;
	UPROPERTY() TObjectPtr<UTextBlock> BrowserDetail;
	UPROPERTY() TObjectPtr<UTextBlock> FileStatus;
	UPROPERTY() TObjectPtr<UTextBlock> FileProblem;
	UPROPERTY() TObjectPtr<UEditableTextBox> NameBox;
	UPROPERTY() TObjectPtr<UEditableTextBox> DescriptionBox;
	UPROPERTY() TObjectPtr<UEditableTextBox> WebsiteBox;
	UPROPERTY() TObjectPtr<UEditableTextBox> DiscordBox;
	UPROPERTY() TObjectPtr<UEditableTextBox> SearchBox;
	UPROPERTY() TObjectPtr<UEditableTextBox> DatBox;
	UPROPERTY() TObjectPtr<UEditableTextBox> AccountAliasBox;
	UPROPERTY() TObjectPtr<UButton> PasswordVisibilityButton;
	UPROPERTY() TObjectPtr<UBorder> ConfirmPanel;
	UPROPERTY() TObjectPtr<UTextBlock> ConfirmText;
	UPROPERTY() TObjectPtr<UButton> ACETypeButton;
	UPROPERTY() TObjectPtr<UButton> GDLETypeButton;
	UPROPERTY() TObjectPtr<UButton> WebsiteButton;
	UPROPERTY() TObjectPtr<UButton> DiscordButton;
	UPROPERTY() TObjectPtr<class UACEUpdateSubsystem> Updater;
	UPROPERTY() TObjectPtr<UTextBlock> UpdateText;
	UPROPERTY() TObjectPtr<UTextBlock> UpdateInstallHelp;
	UPROPERTY() TObjectPtr<UBorder> ConnectScreen;
	UPROPERTY() TObjectPtr<class UImage> ConnectLogo;
	UPROPERTY() TObjectPtr<UTextBlock> ConnectLabel;
	UPROPERTY() TObjectPtr<UTextBlock> ConnectUpdateLabel;
	UPROPERTY() TObjectPtr<UTextBlock> ConnectDetail;
	UPROPERTY() TObjectPtr<UACELoginActionButton> ConnectCancelButton;
	UPROPERTY() TArray<TObjectPtr<class UTexture2D>> VialTextures;
	TSharedPtr<struct FACEVialArt> VialArt;
	float ConnectFill = 0, ConnectStepBase = 0, ConnectStepCap = 0, ConnectStepTime = 0, ConnectFailTime = -1;
	float UpdateVialFill = 0, UpdatePageFill = 0, UpdatePageTarget = 0;
	bool bSuppressConnectDisconnect = false;
	bool bConnectScreenPreview = false; // -ACEConnectScreenPreview (non-shipping): loop the screen without connecting.
	float ConnectPreviewTime = 0;
	int32 ConnectPreviewPass = 0;
	float LobbyShotTime = -1.f;
	FString LobbyShotAccountId;
	int32 ConnectPhase = 0; // 0 connecting, 1 connected (left vial filling), 2 updating, 3 both vials done.
	float ConnectPhaseTime = 0;
	bool bUpdateDownloadRequested = false, bUpdatePhaseFinished = false;
	void MarkConnected();
	void BeginUpdatePhase();
	void TickUpdatePhase(float& Target, FString& Text);
	void TickConnectPreview(float DeltaTime);
	void ShowConnectScreen();
	void SetConnectStep(float Base, float Cap, const FString& Text);
	void TickConnectScreen(float DeltaTime);
	void LoadVialArt();
	UPROPERTY() TObjectPtr<UACELoginActionButton> UpdateNavButton;
	UPROPERTY() TObjectPtr<UButton> CheckUpdateButton;
	UPROPERTY() TObjectPtr<UButton> DownloadUpdateButton;
	UPROPERTY() TObjectPtr<UButton> InstallUpdateButton;
	UPROPERTY() TObjectPtr<UButton> CancelUpdateButton;
	UPROPERTY() TObjectPtr<class UCheckBox> AutoUpdateCheckBox;
	UPROPERTY() TObjectPtr<UTextBlock> AutoUpdateMark;
	UFUNCTION() void OnAutoUpdateChanged(bool bChecked);
	float UpdatePollTime = 0;
	void RefreshUpdateControls();
	void RefreshServers();
	void RefreshAccounts();
	void TickAccountDrag(float DeltaTime);
	bool bAccountOrderDirty = false;
	void SetPasswordVisible(bool bVisible);
	void SelectServer(const FString& Id);
	void EditServer(bool bNew);
	bool StoreAccount();
	void RefreshBrowser();
	void FetchDirectory();
	void UpdateDatStatus();
	bool ValidateGameFiles();
	UFUNCTION() void OnSearchChanged(const FText& Text);
	UFUNCTION() void OnDatChanged(const FText& Text);
	UTextBlock* Label(const FString& Text, int32 Size = 18, bool bMuted = false, float BitmapScale = 1.25f);
	UACELoginActionButton* ActionButton(const FString& Text, const FString& Action, const FString& Value = FString(), bool bAccent = false);
	UEditableTextBox* Field(UVerticalBox* Parent, const FString& Title, const FName Name, bool bSecret = false);
	/** CRITICAL: builds WidgetTree->RootWidget BEFORE UUserWidget::RebuildWidget() ever runs.
	 *  UUserWidget::RebuildWidget() captures `WidgetTree->RootWidget ? RootWidget->TakeWidget()
	 *  : SNew(SSpacer)` at the moment it's called, and only calls NativeConstruct() AFTER that
	 *  (see UUserWidget::OnWidgetRebuilt). If EnsureDefaultLayout() were only called from
	 *  NativeConstruct() (too late), the very first build would permanently freeze this widget's
	 *  content as an invisible, zero-sized SSpacer — even though the outer viewport slot still
	 *  gets a real, non-zero allocated geometry (that geometry belongs to the slot, not to the
	 *  SSpacer content). That exactly reproduces "CachedGeometry non-zero, DesiredSize (0,0),
	 *  nothing painted" — so building the layout here, and calling this again defensively from
	 *  NativeConstruct() (where it's a no-op once HostBox is set), guarantees the real widget
	 *  tree is what RebuildWidget() actually captures. */
	virtual TSharedRef<SWidget> RebuildWidget() override;

	void EnsureDefaultLayout();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> HostBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> PortBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> AccountBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> PasswordBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> LoginButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> EnterWorldButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CharacterListLabel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> CharacterListBox;

	UPROPERTY()
	TObjectPtr<class USizeBox> CharacterListSizeBox;

	UPROPERTY()
	TArray<TObjectPtr<UACECharacterRowButton>> CharacterRowButtons;

	UPROPERTY()
	TObjectPtr<UACEClientSubsystem> Client = nullptr;

	int32 SelectedCharacterId = 0;
	int32 SelectedCharacterIndex = INDEX_NONE;
	TArray<FACECharacterInfo> CachedCharacters;

	void RebuildCharacterRows();
	void UpdateCharacterRowHighlight();

	/** Set once NativeTick has fired the one-shot "still zero-sized" safety-net prepass, so we
	 *  don't spam it every frame. */
	bool bForcedZeroSizePrepass = false;
	bool bUseDatIntroBackdrop = false;
	bool bRetailFontsReady = false;
	bool bLoginSettingsLoaded = false;
	bool bLoginEntriesDirty = false;
	double LastLoginEditTime = 0.0;
	void SaveLoginEntries();
	UFUNCTION() void OnLoginEntryChanged(const FText& Text);
	UFUNCTION() void OnLoginEntryCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UPROPERTY()
	TObjectPtr<UBorder> BackdropBorder;

	UFUNCTION()
	void OnLoginClicked();

	UFUNCTION()
	void OnEnterWorldClicked();

	UFUNCTION()
	void HandleState(EACESessionState NewState);

	UFUNCTION()
	void HandleCharacters(const TArray<FACECharacterInfo>& Characters, const FString& ServerName);

	UFUNCTION()
	void HandleLog(const FString& Message);
};
