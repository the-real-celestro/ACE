#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "Components/Button.h"
#include "Components/ListView.h"
#include "ACELobbyAccountList.generated.h"

class UACELoginWidget;
class UButton;
class UACERetailTextBlock;

/** Mouse-only button: clicking a row must not take keyboard focus from the launcher. */
UCLASS()
class ACECLIENT_API UACELobbyPlainButton : public UButton
{
	GENERATED_BODY()
public:
	UACELobbyPlainButton(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) { InitIsFocusable(false); }
};

/** One launcher account as shown in the Play page lists. Holds no credentials. */
UCLASS()
class ACECLIENT_API UACELobbyAccountItem : public UObject
{
	GENERATED_BODY()
public:
	FString Id, Title;
	bool bFavorite = false;
	bool bSelected = false;
	/** Search match highlighted in the name, in characters of Title. */
	int32 MatchStart = INDEX_NONE, MatchLen = 0;
	TWeakObjectPtr<UACELoginWidget> Owner;
};

/** Retail slot row: favourite star and account name. Click selects; the star toggles favourite. */
UCLASS()
class ACECLIENT_API UACELobbyAccountRow : public UUserWidget, public IUserObjectListEntry
{
	GENERATED_BODY()
public:
	static constexpr float RowHeight = 26.f;
	/** Re-applies selection, favourite and search-highlight state without regenerating rows. */
	void Refresh();
	UACELobbyAccountItem* GetItem() const { return Item; }
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;
	UFUNCTION() void HandleClicked();
	UFUNCTION() void HandleStarClicked();
	UPROPERTY() TObjectPtr<UButton> RowButton;
	UPROPERTY() TObjectPtr<UButton> StarButton;
	UPROPERTY() TObjectPtr<UACERetailTextBlock> Name;
	UPROPERTY() TObjectPtr<UACELobbyAccountItem> Item;
	bool bStarFilled = false;
	bool bStarHot = false;
};

/** Virtualised account list with the retail chain scrollbar (shown only when it can scroll). */
UCLASS()
class ACECLIENT_API UACELobbyAccountListView : public UListView
{
	GENERATED_BODY()
public:
	UACELobbyAccountListView(const FObjectInitializer& ObjectInitializer);
	/** Switches the scrollbar to DAT art once it has loaded. */
	void ApplyArt();
	bool CanScroll() const;
	void ScrollBy(float Items);
	/** Restyles the visible rows after a selection or favourite change. */
	void RefreshRows();
};
