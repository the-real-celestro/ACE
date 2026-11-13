#include "ACEBodySweep.h"
#include "HAL/IConsoleManager.h"
#include "ACEWorldEntityActor.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProceduralMeshComponent.h"

static TAutoConsoleVariable<int32> GACEBatchCrowdQueries(TEXT("ace.Collision.BatchCrowdQueries"),1,
    TEXT("Gather escapable creature overlaps, floor-ray obstructions and rejected sphere supports together. 0 retains per-body retries for comparison."));

bool ACEBodySweep::BatchCrowdQueries()
{
    return GACEBatchCrowdQueries.GetValueOnGameThread()!=0;
}

bool ACEBodySweep::CanEscapeClosedContainer(const FHitResult& Hit,const FVector& From,const FVector& To,const FACECollisionBody& Body)
{
    // A chest can close while a player occupies its open, ethereal body.
    // Permit gradual escape from that existing overlap. The exemption
    // belongs to this component and sweep; neighboring solids still collide.
    if(!Hit.bStartPenetrating || !Hit.Component.IsValid())return false;
    auto* Component=Hit.GetComponent();
    const auto* Chest=Cast<AACEWorldEntityActor>(Hit.GetActor());
    if(!Chest || Chest->IsDoor() || !(Chest->ItemType&ACEItemType::Container) || !Chest->UsesOnOffMotion())return false;
    auto* Setup=Component->GetBodySetup();
    if(!Setup)return false;
    const bool Convex=Component->ComponentHasTag(TEXT("ACECollisionOnly"))
        && Setup->AggGeom.GetElementCount()==1 && Setup->AggGeom.ConvexElems.Num()==1;
    FBox Envelope(ForceInit);
    if(!Convex)
    {
        // A chest's BSP is a hollow shell: escaping its interior necessarily
        // crosses its surface. Use the envelope of authored collision vertices
        // for outward progress, never render bounds or the enlarged pick proxy.
        auto* Mesh=Cast<UProceduralMeshComponent>(Component);if(!Mesh)return false;
        for(int32 Section=0;Section<Mesh->GetNumSections();++Section)
            if(const auto* Geometry=Mesh->GetProcMeshSection(Section);Geometry && Geometry->bEnableCollision)
                for(const auto& Vertex:Geometry->ProcVertexBuffer)Envelope+=Vertex.Position;
        if(!Envelope.IsValid)return false;
    }
    const FVector Delta=To-From;
    if(Delta.IsNearlyZero(.0001))return false;
    bool Overlapping=false;
    for(int32 I=0;I<Body.Count;++I)
    {
        FHitResult Contact;
        if(!Component->SweepComponent(Contact,From+Body.Centers[I],To+Body.Centers[I],FQuat::Identity,
            FCollisionShape::MakeSphere(Body.Radii[I]),false))continue;
        if(!Contact.bStartPenetrating)return false; // No newly colliding body sphere.
        if(Convex)
        {
            // Signed distance to one convex body is convex. A nonnegative
            // initial derivative cannot deepen the overlap along this segment.
            if(Contact.Normal.IsNearlyZero() || FVector::DotProduct(Contact.Normal,Delta)<-1.e-6*Delta.Size())return false;
        }
        else
        {
            const auto Transform=Component->GetComponentTransform();
            const FVector Local=Transform.InverseTransformPosition(From+Body.Centers[I]);
            const FVector Travel=Transform.InverseTransformVector(Delta);
            const FVector Outside=Local-Envelope.GetClosestPointTo(Local);
            FVector Normal=Outside.GetSafeNormal();
            if(Normal.IsNearlyZero())
            {
                const FVector Relative=Local-Envelope.GetCenter(),Distance=Relative.GetAbs()-Envelope.GetExtent();
                const int32 Axis=Distance.X>=Distance.Y && Distance.X>=Distance.Z?0:Distance.Y>=Distance.Z?1:2;
                Normal=FVector::ZeroVector;Normal[Axis]=Relative[Axis]<0.?-1.:1.;
            }
            if(FVector::DotProduct(Normal,Travel)<-1.e-6*Travel.Size())return false;
        }
        Overlapping=true;
    }
    return Overlapping;
}
