#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACETestDatDirectory.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEBodySweep.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEClosedChestEscapeTest,"ACE.Collision.ClosedChestEscape",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEClosedChestEscapeTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);Context.OwningGameInstance=GI;GI->OnWorldChanged(nullptr,World);GI->Init();
    ON_SCOPE_EXIT{GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(!TestTrue(TEXT("DAT opens"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
    FACEWorldObject Chest;Chest.Guid=48742;Chest.WeenieClassId=48742;Chest.Name=TEXT("Legendary Magic Chest");
    Chest.SetupId=0x02000F34;Chest.MotionTableId=0x09000123;Chest.ItemType=ACEItemType::Container;
    Chest.ObjectDescriptionFlags=ACEObjectDescFlag::Openable;Chest.PhysicsState=1048;Chest.bHasPosition=true;
    Chest.Position.CellId=0x016C0101;Chest.Position.Location=FVector(50,50,100);
    auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Chest,100,true);
    const FACECollisionBody Body(FCollisionShape::MakeCapsule(48,91.75));
    const FVector Inside=Actor->GetActorLocation()+FVector(0,0,91.25);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ClosedChestEscape),false);FHitResult Hit;
    auto Open=[&]{Actor->Appearance->SetDoorHeldCommand(ACEMotion::OnCommandU16);Actor->ApplyPhysicsState(Chest.PhysicsState|ACEPhysicsState::Ethereal);};
    auto Close=[&]{Actor->Appearance->SetDoorHeldCommand(ACEMotion::OffCommandU16);Actor->ApplyPhysicsState(Chest.PhysicsState);};
    Open();TestFalse(TEXT("Opened chest is walk-through"),ACEBodySweep::SweepBody(*World,Hit,Inside-FVector(300,0,0),Inside+FVector(300,0,0),Body,Query));
    Close();
    FHitResult Initial;
    const bool Embedded=World->SweepSingleByChannel(Initial,Inside+Body.Centers[0],Inside+Body.Centers[0]+FVector(0,0,.001),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(Body.Radii[0]),Query);
    AddInfo(FString::Printf(TEXT("Closed chest fixture: embedded=%d depth=%.3f component=%s authored=%d"),Embedded,Initial.PenetrationDepth,*GetNameSafe(Initial.GetComponent()),Actor->AuthoredCollision.Num()));
    if(!TestTrue(TEXT("Closing actual Legendary chest overlaps the standing player"),Embedded&&Initial.bStartPenetrating))return false;
    FVector Direction=FVector::ZeroVector;
    for(int32 I=0;I<16;++I)
    {
        const double Angle=I*2.*PI/16.;const FVector D(FMath::Cos(Angle),FMath::Sin(Angle),0);
        if(!ACEBodySweep::SweepBody(*World,Hit,Inside,Inside+D*5.,Body,Query)){Direction=D;break;}
    }
    if(!TestFalse(TEXT("Player has a gradual escape direction after chest closes"),Direction.IsNearlyZero()))return false;
    FVector Position=Inside;
    for(int32 Step=0;Step<80;++Step)
    {
        const FVector Next=Position+Direction*5.;
        if(ACEBodySweep::SweepBody(*World,Hit,Position,Next,Body,Query))break;
        Position=Next;
    }
    TestTrue(TEXT("Small walking steps carry the player completely out"),FVector::Dist2D(Position,Inside)>200.);
    TestTrue(TEXT("Closed chest still blocks entering from outside"),ACEBodySweep::SweepBody(*World,Hit,Position,Inside,Body,Query));
    for(int32 Pass=0;Pass<3;++Pass)
    {
        Actor->ApplyPhysicsState(Chest.PhysicsState);Actor->SetCellVisible(false);Actor->SetCellVisible(true);
        TestFalse(TEXT("Refreshes retain escape from the existing overlap"),ACEBodySweep::SweepBody(*World,Hit,Inside,Inside+Direction*5.,Body,Query));
        TestTrue(TEXT("Refreshes retain solid collision from outside"),ACEBodySweep::SweepBody(*World,Hit,Position,Inside,Body,Query));
    }
    auto* Wall=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Wall->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(10,300,300));Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();
    Wall->SetActorLocation(Inside+Direction*150.);Wall->SetActorRotation(Direction.Rotation());
    TestTrue(TEXT("Escaping chest still sweeps neighboring walls"),ACEBodySweep::SweepBody(*World,Hit,Inside,Inside+Direction*300.,Body,Query));
    TestTrue(TEXT("The neighboring wall remains the blocking contact"),Hit.GetActor()==Wall);
    return !HasAnyErrors();
}
#endif
