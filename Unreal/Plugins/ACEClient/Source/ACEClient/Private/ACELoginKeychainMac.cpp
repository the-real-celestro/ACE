#include "ACELoginKeychain.h"

#if PLATFORM_MAC
#ifndef __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#endif
THIRD_PARTY_INCLUDES_START
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogACELoginKeychain, Log, All);

namespace ACELoginKeychain
{
	static const CFStringRef Service = CFSTR("ACUnreal");

	static CFStringRef MakeCFString(const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		return CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(Utf8.Get()), Utf8.Length(), kCFStringEncodingUTF8, false);
	}

	static CFMutableDictionaryRef MakeQuery(CFStringRef Account)
	{
		CFMutableDictionaryRef Query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CFDictionarySetValue(Query, kSecClass, kSecClassGenericPassword);
		CFDictionarySetValue(Query, kSecAttrService, Service);
		CFDictionarySetValue(Query, kSecAttrAccount, Account);
		return Query;
	}

	bool Write(const FString& Key, const FString& Secret)
	{
		CFStringRef Account = MakeCFString(Key);
		if (!Account) return false;
		CFMutableDictionaryRef Query = MakeQuery(Account);
		OSStatus Status;
		if (Secret.IsEmpty())
		{
			Status = SecItemDelete(Query);
			if (Status == errSecItemNotFound) Status = errSecSuccess;
		}
		else
		{
			const FTCHARToUTF8 Utf8(*Secret);
			CFDataRef Data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(Utf8.Get()), Utf8.Length());
			// Replace the item rather than update it, so the running build becomes its owner.
			SecItemDelete(Query);
			CFMutableDictionaryRef Add = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, Query);
			CFDictionarySetValue(Add, kSecValueData, Data);
			CFDictionarySetValue(Add, kSecAttrLabel, CFSTR("ACUnreal saved logins"));
			// Default access: only the build that created the item reads it silently. Replacing the item
			// makes the running build its owner, so after one prompt per new build nothing else asks.
			Status = SecItemAdd(Add, nullptr);
			CFRelease(Add);
			CFRelease(Data);
		}
		CFRelease(Query);
		CFRelease(Account);
		if (Status != errSecSuccess) UE_LOG(LogACELoginKeychain, Warning, TEXT("Keychain write failed (OSStatus %d)."), static_cast<int32>(Status));
		return Status == errSecSuccess;
	}

	bool Read(const FString& Key, FString& OutSecret)
	{
		OutSecret.Reset();
		CFStringRef Account = MakeCFString(Key);
		if (!Account) return false;
		CFMutableDictionaryRef Query = MakeQuery(Account);
		CFDictionarySetValue(Query, kSecReturnData, kCFBooleanTrue);
		CFDictionarySetValue(Query, kSecMatchLimit, kSecMatchLimitOne);
		CFTypeRef Result = nullptr;
		const OSStatus Status = SecItemCopyMatching(Query, &Result);
		CFRelease(Query);
		CFRelease(Account);
		if (Status != errSecSuccess || !Result)
		{
			if (Status != errSecItemNotFound) UE_LOG(LogACELoginKeychain, Warning, TEXT("Keychain read failed (OSStatus %d)."), static_cast<int32>(Status));
			if (Result) CFRelease(Result);
			return false;
		}
		bool bOk = false;
		if (CFGetTypeID(Result) == CFDataGetTypeID())
		{
			CFDataRef Data = static_cast<CFDataRef>(Result);
			const CFIndex Length = CFDataGetLength(Data);
			if (Length >= 0 && Length <= 524288)
			{
				const FUTF8ToTCHAR Utf8(reinterpret_cast<const ANSICHAR*>(CFDataGetBytePtr(Data)), static_cast<int32>(Length));
				OutSecret = FString(Utf8.Length(), Utf8.Get());
				bOk = true;
			}
		}
		CFRelease(Result);
		return bOk;
	}
}
#endif
