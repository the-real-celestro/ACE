#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ACEDatSubsystem.h"
#include "ACETestDatDirectory.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "Dat/ACEDatMotionPlayer.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEFullCastMotionTest,"ACE.RetailParity.FullCastMotion",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEFullCastMotionTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
 auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);Context.OwningGameInstance=GI;GI->OnWorldChanged(nullptr,World);GI->Init();
 ON_SCOPE_EXIT{GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();if(!TestTrue(TEXT("DAT opens"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
 FACEDatMotionPlayer Reference(Dat->GetPortalDat());if(!TestTrue(TEXT("Human motion table loads"),Reference.SetMotionTable(0x09000001)))return false;
 constexpr uint32 Magic=ACEMotion::StanceMagic,ItemCast=0x40000037u;
 auto Link=[&](uint32 From,uint32 To)->const FACEDatMotionData*
 {
  if(const auto* Row=Reference.MotionTable->Links.Find((Magic<<16)|(From&0xffffffu)))return Row->Find(To);
  return nullptr;
 };
 const auto* ItemEntry=Link(ACEMotion::Ready,ItemCast);const auto* ItemReturn=Link(ItemCast,ACEMotion::Ready);
 if(!TestTrue(TEXT("Item enchantment has distinct authored entry and recovery"),ItemEntry && ItemReturn))return false;
 auto Duration=[&](const auto* Clips){float Time=0;for(const auto& C:Clips->Anims)Time+=Reference.GetAnimDataDuration(C);return Time;};
 AddInfo(FString::Printf(TEXT("MagicEnchantItem: entry %.4fs; recovery %.4fs"),Duration(ItemEntry),Duration(ItemReturn)));
 for(const auto* Data:{ItemEntry,ItemReturn})for(const auto& C:Data->Anims)
  AddInfo(FString::Printf(TEXT("Item cast clip %08X frames %d..%d rate %.3f"),C.AnimId,C.LowFrame,C.HighFrame,C.Framerate));
 FACEWorldObject Object;Object.Guid=174;Object.SetupId=0x02000001;Object.MotionTableId=0x09000001;Object.bIsPlayer=true;
 auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Object,100,false);auto* App=Actor->Appearance.Get();
 FString Trace=TEXT("phase,phase_seconds,total_seconds,right_hand_z_cm,authored_right_hand_z_cm\n");
 float EntryHandMin=MAX_flt,RecoveryHandMin=MAX_flt;
 TArray<uint32> Gestures;for(uint32 C=0x4000002b;C<=0x40000039;++C)if(Link(ACEMotion::Ready,C) && Link(C,ACEMotion::Ready))Gestures.Add(C);
 for(uint32 C:{0x400000d3u,0x400000e0u,0x400000e1u})if(Link(ACEMotion::Ready,C) && Link(C,ACEMotion::Ready))Gestures.Add(C);
 int32 Cases=0;
 for(uint32 Setup:{0x02000001u,0x0200004eu})
 {
  Object.SetupId=Setup;if(!TestTrue(TEXT("Human cast geometry builds"),App->ApplyWorldObject(Object,100,false)))return false;
  App->SetPreviewCapture(true);
  for(uint32 Gesture:Gestures)for(float Dt:{1.f/30,1.f/90})for(float Rate:{1.f,1.7f})for(int32 ReadyMode:{0,1,2})
  {
   App->ClearActionMotion();App->PreferredStyle=Magic;App->SetLocomotionInput(0,0,false,1);
   App->PlayActionMotion(Gesture,Rate,Magic,false);
   bool Entry=false,Recovery=false,SentReady=false;double Elapsed=0,MaxError=0;FString WorstPose;
   for(int32 Frame=0;Frame<1800 && App->ActionCommand;++Frame)
   {
    if(ReadyMode && !SentReady && App->AnimTime>Duration(Link(ACEMotion::Ready,Gesture))*.2f)
    {
     if(ReadyMode==1){App->FinishCastMotion();App->FinishCastMotion();}
     else{FACEObjectMotionState Ready;Ready.CurrentStyle=Magic;Ready.ForwardCommand=ACEMotion::Ready;Actor->ApplyMotionState(Ready);Actor->ApplyMotionState(Ready);}
     TestEqual(TEXT("Early Ready queues a single recovery"),App->PendingActionCommands.Num(),1);SentReady=true;
    }
    const uint32 Command=App->ActionCommand,From=App->ActionFromCommand;
    const float Time=App->AnimTime+Dt*App->ActionPlayRate;
    App->TickComponent(Dt,LEVELTICK_All,nullptr);Elapsed+=Dt;
    TArray<FTransform> Expected;int32 Count=0;bool Finished=false;
    if(Command==Gesture){Entry=true;Reference.EvaluateLink(ACEMotion::Ready,Gesture,Time,App->GetPartCount(),Expected,100,Count,Finished,nullptr,nullptr,Magic);}
    else if(Command==ACEMotion::Ready && From==Gesture){Recovery=true;Reference.EvaluateTransition(Gesture,ACEMotion::Ready,Time,App->GetPartCount(),Expected,100,Count,Finished,nullptr,nullptr,Magic);}
    else{AddError(TEXT("Cast left its authored entry/recovery sequence"));return false;}
    if(!TestTrue(TEXT("Every displayed cast frame has an authored reference"),Count==App->GetPartCount()))return false;
    for(int32 Part=0;Part<Count;++Part)
    {
     const auto Pose=App->GetPartMesh(Part)->GetRelativeTransform();
     // ApplyPartTransform suppresses updates below .04 cm on EACH axis.
     // Compare axes at .05 cm; a Euclidean .05 threshold falsely rejects a
     // permitted .04/.00/.036 cm change despite the authored pose being intact.
     const double TranslationError=(Pose.GetTranslation()-Expected[Part].GetTranslation()).GetAbsMax();
     // Scene components normalize their rotations. Compare orientation, not
     // the tiny length error in a DAT's single-precision stored quaternion.
     const double RotationError=Pose.GetRotation().GetNormalized().AngularDistance(Expected[Part].GetRotation().GetNormalized())*100.;
     const double Error=FMath::Max(TranslationError,RotationError);
     if(Error>MaxError){MaxError=Error;WorstPose=FString::Printf(TEXT("phase=%08X part=%d t=%.9f translation=%.9f angular=%.9f actual=%s expected=%s blend=%.6f"),Command,Part,Time,TranslationError,RotationError,*Pose.GetTranslation().ToString(),*Expected[Part].GetTranslation().ToString(),App->StanceBlendAlpha);}
    }
    if(Setup==0x02000001 && Gesture==ItemCast && ReadyMode==0 && Rate==1.f && Dt<1.f/60 && Count>15)
    {
     Trace+=FString::Printf(TEXT("%s,%.6f,%.6f,%.6f,%.6f\n"),Command==Gesture?TEXT("entry"):TEXT("recovery"),Time,Elapsed,App->GetPartMesh(15)->GetRelativeLocation().Z,Expected[15].GetTranslation().Z);
     float& Minimum=Command==Gesture?EntryHandMin:RecoveryHandMin;
     Minimum=FMath::Min(Minimum,float(App->GetPartMesh(15)->GetRelativeLocation().Z));
    }
   }
   const FString Case=FString::Printf(TEXT("cast=%08X setup=%08X rate=%.2f fps=%.0f ready=%d"),Gesture,Setup,Rate,1./Dt,ReadyMode);
   TestTrue(*FString(TEXT("Full cast completes: ")+Case),Entry && Recovery && App->ActionCommand==0);
   TestTrue(*FString(TEXT("Complete cast pose follows DAT without flattened arc: ")+Case),MaxError<.05);
   if(MaxError>=.05)AddError(Case+TEXT(" ")+WorstPose);
   const double ExpectedSeconds=Duration(Link(ACEMotion::Ready,Gesture))/Rate+Duration(Link(Gesture,ACEMotion::Ready));
   TestTrue(*FString(TEXT("Both cast segments keep their authored duration: ")+Case),Elapsed>=ExpectedSeconds-.001 && Elapsed<ExpectedSeconds+Dt*2.1);
   ++Cases;
  }
 }
 FFileHelper::SaveStringToFile(Trace,*(FPaths::ProjectSavedDir()/TEXT("Full-cast-item-hand-trace.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 TestTrue(TEXT("Item cast recovery includes the near-ground hand sweep"),RecoveryHandMin<25.f && EntryHandMin-RecoveryHandMin>50.f);
 AddInfo(FString::Printf(TEXT("Item cast lowest hand: entry %.3f cm; recovery %.3f cm"),EntryHandMin,RecoveryHandMin));
 AddInfo(FString::Printf(TEXT("Verified %d full cast sequences on two bodies, at two speeds/frame rates and with automatic/local/remote Ready"),Cases));
 // Rapid next-spell and stance packets must wait for the recovery's low sweep.
 App->ClearActionMotion();App->PreferredStyle=Magic;App->PlayActionMotion(ItemCast,1,Magic,false);
 for(int32 F=0;F<600 && App->ActionCommand!=ACEMotion::Ready;++F)App->TickComponent(1.f/90,LEVELTICK_All,nullptr);
 TestTrue(TEXT("Item cast enters its recovery"),App->ActionCommand==ACEMotion::Ready && App->ActionFromCommand==ItemCast);
 App->TickComponent(.05f,LEVELTICK_All,nullptr);const float Before=App->AnimTime;
 App->PlayActionMotion(0x1000006fu,1,Magic,false);App->SetPreferredStyle(ACEMotion::StanceNonCombat);
 TestTrue(TEXT("New spell and stance cannot truncate the return sweep"),App->ActionCommand==ACEMotion::Ready && App->ActionFromCommand==ItemCast && App->AnimTime==Before);
 TestEqual(TEXT("Both subsequent motions stay queued"),App->PendingActionCommands.Num(),2);
 App->ClearActionMotion();App->SetHeldActionMotion(0x40000015u,Magic);App->FinishCastMotion();
 TestEqual(TEXT("Late cast Ready cannot cancel an airborne pose"),App->ActionCommand,0x40000015u);
 return !HasAnyErrors();
}
#endif
