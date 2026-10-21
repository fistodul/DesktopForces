#include "World/MFBlocker.h"
#include "Game/CTF/MFFlag.h"
#include "Game/MFGameMode.h"
#include "Game/MFPlayerState.h"
#include "Player/MFCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AMFBlocker::AMFBlocker()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;   // so a server-side Destroy (no team game) reaches clients
	Shape = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shape"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Shape->SetStaticMesh(Cylinder.Object);   // 100 cm across, 100 tall, centred: its convex hull is the collision
	}
	Shape->SetHiddenInGame(true);
	Shape->SetCastShadow(false);
	Shape->SetMobility(EComponentMobility::Static);
	Shape->SetCanEverAffectNavigation(false);
	Shape->SetCollisionObjectType(ECC_WorldDynamic);
	Shape->SetCollisionResponseToAllChannels(ECR_Ignore);
	Shape->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	RootComponent = Shape;
}

void AMFBlocker::SetCylinder(float Radius, float HalfHeight)
{
	Shape->SetWorldScale3D(FVector(Radius / 50.f, Radius / 50.f, HalfHeight / 50.f));
	Reach = Radius + 600.f;
}

void AMFBlocker::BeginPlay()
{
	Super::BeginPlay();
	Reach = FMath::Max(Reach, Shape->Bounds.SphereRadius + 600.f);
	if (bBlockVehicles)
	{
		Shape->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Shape->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
	}
	if (Team == MF::NoTeam)
	{
		return;
	}
	// LoadoutBlocker: team games only (bNonTeamPlay False); every projectile and players'
	// shots stop at it
	const AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>();
	if (GM && !GM->IsTeamGame())
	{
		Destroy();   // replicated: gone on clients too
		return;
	}
	Shape->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);   // projectiles
	Shape->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);   // weapon traces
	GetWorldTimerManager().SetTimer(PassTimer, this, &AMFBlocker::UpdatePasses, 0.25f, true, 0.f);
}

void AMFBlocker::UpdatePasses()
{
	for (TActorIterator<AMFCharacter> It(GetWorld()); It; ++It)
	{
		AMFCharacter* C = *It;
		if (FVector::DistSquared(C->GetActorLocation(), GetActorLocation()) > Reach * Reach)
		{
			continue;
		}
		const AMFPlayerState* PS = C->GetPlayerState<AMFPlayerState>();
		const bool bPass = C->GetTeam() == Team && !(PS && PS->bCaptain) && !AMFFlag::FindCarriedBy(C);
		C->GetCapsuleComponent()->IgnoreActorWhenMoving(this, bPass);
	}
}
