#include "World/DFBlocker.h"
#include "Game/CTF/Flag.h"
#include "Game/DFGameMode.h"
#include "Game/DFPlayerState.h"
#include "Player/DFCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ADFBlocker::ADFBlocker()
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

void ADFBlocker::SetCylinder(float Radius, float HalfHeight)
{
	Shape->SetWorldScale3D(FVector(Radius / 50.f, Radius / 50.f, HalfHeight / 50.f));
	Reach = Radius + 600.f;
}

void ADFBlocker::BeginPlay()
{
	Super::BeginPlay();
	Reach = FMath::Max(Reach, Shape->Bounds.SphereRadius + 600.f);
	if (bBlockVehicles)
	{
		Shape->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Shape->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
	}
	if (Team == DF::NoTeam)
	{
		return;
	}
	// LoadoutBlocker: team games only (bNonTeamPlay False); every projectile and players'
	// shots stop at it
	const ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>();
	if (GM && !GM->IsTeamGame())
	{
		Destroy();   // replicated: gone on clients too
		return;
	}
	Shape->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);   // projectiles
	Shape->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);   // weapon traces
	GetWorldTimerManager().SetTimer(PassTimer, this, &ADFBlocker::UpdatePasses, 0.25f, true, 0.f);
}

void ADFBlocker::UpdatePasses()
{
	for (TActorIterator<ADFCharacter> It(GetWorld()); It; ++It)
	{
		ADFCharacter* C = *It;
		if (FVector::DistSquared(C->GetActorLocation(), GetActorLocation()) > Reach * Reach)
		{
			continue;
		}
		const ADFPlayerState* PS = C->GetPlayerState<ADFPlayerState>();
		const bool bPass = C->GetTeam() == Team && !(PS && PS->bCaptain) && !AFlag::FindCarriedBy(C);
		C->GetCapsuleComponent()->IgnoreActorWhenMoving(this, bPass);
	}
}
