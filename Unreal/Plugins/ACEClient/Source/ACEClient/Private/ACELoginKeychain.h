#pragma once
#include "CoreMinimal.h"

#if PLATFORM_MAC
/** macOS Keychain storage for launcher passwords (generic password items, service "ACUnreal"). */
namespace ACELoginKeychain
{
	/** Store Secret under Key, replacing any existing value. An empty Secret deletes the item. */
	bool Write(const FString& Key, const FString& Secret);
	/** Read the secret stored under Key. Returns false when missing or access was denied. */
	bool Read(const FString& Key, FString& OutSecret);
}
#endif
