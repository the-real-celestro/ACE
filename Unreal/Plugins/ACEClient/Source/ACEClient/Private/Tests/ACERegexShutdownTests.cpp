#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Async/Async.h"
#include "Mods/ACEVTRegex.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERegexShutdownTest,"ACE.Plugins.RegexShutdown",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACERegexShutdownTest::RunTest(const FString&)
{
    ACEVTRegex::Initialize();ON_SCOPE_EXIT{ACEVTRegex::Initialize();};
    FString Error;TMap<FString,FString> Captures;
    TestTrue(TEXT("Loot-filter regex cache is warmed"),ACEVTRegex::Match(TEXT("(?<item>Frost) Sword"),TEXT("Frost Sword"),Captures,Error));
    TArray<TFuture<bool>> Workers;
    for(int32 Worker=0;Worker<4;++Worker)Workers.Add(Async(EAsyncExecution::ThreadPool,[Worker]()
    {
        FString E;
        for(int32 I=0;I<50;++I)
        {
            const FString Text=FString::Printf(TEXT("Item%d_%d"),Worker,I);
            if(!ACEVTRegex::Test(TEXT("^Item[0-9]+_[0-9]+$"),Text,E))return false;
        }
        return true;
    }));
    for(auto& Worker:Workers)TestTrue(TEXT("Worker-thread regex calls use lifecycle-owned templates"),Worker.Get());
    TestTrue(TEXT("Compiled ICU objects exist before shutdown"),ACEVTRegex::CompiledCacheEntriesForTests()>0);
    ACEVTRegex::Shutdown();
    TestEqual(TEXT("Shutdown releases compiled ICU objects on every thread"),ACEVTRegex::CompiledCacheEntriesForTests(),0);
    TestFalse(TEXT("Cached predicates cannot reopen regex during teardown"),ACEVTRegex::Test(TEXT("^Item[0-9]+_[0-9]+$"),TEXT("Item0_0"),Error));
    TestFalse(TEXT("Capture calls cannot reopen regex during teardown"),ACEVTRegex::Match(TEXT(".*"),TEXT("Item"),Captures,Error));
    TestFalse(TEXT("Validation cannot reopen regex during teardown"),ACEVTRegex::Validate(TEXT(".*"),Error));
    TestTrue(TEXT("Rejected late calls explain shutdown"),Error.Contains(TEXT("shutting down")));
    ACEVTRegex::Shutdown(); // Idempotent module/pre-exit cleanup.
    ACEVTRegex::Initialize();
    // Deliberately leave a live template: native process exit must drain it
    // through the registered pre-exit hook before ICU resets its allocator.
    TestTrue(TEXT("Regex service restarts and leaves a warm exit fixture"),ACEVTRegex::Match(TEXT("^(?<item>Frost Sword)$"),TEXT("Frost Sword"),Captures,Error));
    TestEqual(TEXT("Capture semantics survive service restart"),Captures.FindRef(TEXT("item")),FString(TEXT("Frost Sword")));
    return !HasAnyErrors();
}
#endif
