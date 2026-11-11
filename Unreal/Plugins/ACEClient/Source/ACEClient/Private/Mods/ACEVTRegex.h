#pragma once
#include "CoreMinimal.h"

// ICU matching with explicit work/stack limits. Never run an unrestricted
// backtracking engine on imported profiles on the game thread.
namespace ACEVTRegex
{
// Close all ICU-owned objects before the engine tears down ICU's allocator.
void Initialize();
void Shutdown();
// Exact-pattern validation (including diagnostics) uses a bounded cache;
// changes to pattern text always go through validation again.
bool Validate(const FString& Pattern, FString& Error);
#if WITH_DEV_AUTOMATION_TESTS
uint64 ValidationCompilationsForTests();
int32 ValidationCacheEntriesForTests();
int32 CompiledCacheEntriesForTests();
#endif
// Boolean filters do not allocate capture maps. Compiled patterns and exact
// predicate results use bounded caches; failures are never cached. ICU ownership
// is shared and synchronized so shutdown can drain calls from every thread.
bool Test(const FString& Pattern, const FString& Text, FString& Error);
bool Match(const FString& Pattern, const FString& Text, TMap<FString,FString>& Captures, FString& Error);
}
