#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "Core/DFUserSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/DFGameMode.h"
#include "Game/DFGameState.h"
#include "Player/DFCharacter.h"
#include "Player/DFCharacterMovement.h"
#include "UI/DFCrosshairs.h"
#include "Vehicles/Buggy/Buggy.h"
#include "Vehicles/Humvee/Humvee.h"
#include "World/DFDecals.h"
#include "World/DFMapActors.h"
#include "World/MineGenerator.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "EngineUtils.h"
#include "Core/DFContent.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundClass.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDFOptionsTest, "DesktopForces.Gameplay.Options",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FDFOptionsTest::RunTest(const FString& Parameters) {
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.World() && Context.World()->IsGameWorld())
			World = Context.World();
	if (!World || !World->GetAuthGameMode()) {
		AddError(TEXT("Run this test in a standalone/listen game."));
		return false;
	}

	UDFUserSettings& Settings = UDFUserSettings::Get();
	const bool bOldConsole = Settings.bConsole;
	Settings.bConsole = false;
	Settings.ApplyConsole();
	TestFalse(TEXT("Console unchecked actually disables it"), Settings.IsConsoleEnabled());
	Settings.bConsole = true;
	Settings.ApplyConsole();
	TestTrue(TEXT("Console checked enables it"), Settings.IsConsoleEnabled());
	Settings.bConsole = bOldConsole;
	Settings.ApplyConsole();

	TSet<int32> Shapes;
	int32 Shape = 0;
	for (int32 I = 0; I < 21; ++I) {
		Shapes.Add(Shape);
		Shape = DF::NextCrosshair(Shape, 1);
	}
	TestEqual(TEXT("Crosshair choices are unique"), Shapes.Num(), 21);
	TestEqual(TEXT("Crosshair cycling wraps"), Shape, 0);
	TestFalse(TEXT("Small duplicate is absent"), Shapes.Contains(3));
	TestEqual(TEXT("Backwards cycling wraps"), DF::NextCrosshair(0, -1), 23);

	ADFGameMode* Rules = World->SpawnActor<ADFGameMode>();
	FString Error;
	Rules->InitGame(World->GetMapName(), TEXT("?ScoreLimit=7?Parachutes=0"), Error);
	TestEqual(TEXT("Canonical score option"), Rules->ScoreLimit, 7);
	TestFalse(TEXT("Server parachute option"), Rules->bParachutes);
	Rules->InitGame(World->GetMapName(), TEXT("?ScoreLimit=0?Parachutes=1"), Error);
	TestEqual(TEXT("Unlimited score option"), Rules->ScoreLimit, 0);
	TestTrue(TEXT("Server parachutes can be enabled"), Rules->bParachutes);
	Rules->Destroy();

	ADFGameState* State = World->GetGameState<ADFGameState>();
	if (!State) {
		AddError(TEXT("Expected DFGameState."));
		return false;
	}
	const bool bOldParachutes = State->bParachutes;
	State->bParachutes = true;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Location(0.f, 0.f, 10000.f);
	ADFCharacter* Soldier = World->SpawnActor<ADFCharacter>(Location, FRotator::ZeroRotator, Spawn);
	AAIController* Controller = World->SpawnActor<AAIController>();
	Controller->Possess(Soldier);
	Soldier->SetInLoadout(false);
	UDFCharacterMovement* Move = CastChecked<UDFCharacterMovement>(Soldier->GetCharacterMovement());
	Move->SetMovementMode(MOVE_Falling);
	Move->bWantsParachute = true;
	Move->Velocity = FVector(0.f, 0.f, -Move->JumpZVelocity);
	Move->TickComponent(0.001f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Normal jump descent does not deploy"), Soldier->IsParachuting());
	Move->Velocity = FVector(0.f, 0.f, -1000.f);
	Move->TickComponent(0.001f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("A longer fall deploys"), Soldier->IsParachuting());
	Move->Velocity = FVector(0.f, 0.f, -450.f);
	Move->TickComponent(0.001f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Canopy stays open at terminal speed"), Soldier->IsParachuting());
	State->bParachutes = false;
	Move->TickComponent(0.001f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Server rule closes canopy"), Soldier->IsParachuting());
	State->bParachutes = true;
	Move->Velocity = FVector(0.f, 0.f, -1000.f);
	Move->TickComponent(0.001f, LEVELTICK_All, nullptr);

	ABuggy* Car = World->SpawnActor<ABuggy>(Soldier->GetActorLocation(), FRotator::ZeroRotator, Spawn);
	TestTrue(TEXT("Enter vehicle while parachuting"), Car->TryEnter(Soldier));
	TestFalse(TEXT("Vehicle entry closes canopy on server"), Soldier->IsParachuting());
	Soldier->SetParachute(true); // simulate the owner's predicted state before attachment arrives
	FRepAttachment& Attachment = const_cast<FRepAttachment&>(Soldier->GetAttachmentReplication());
	Attachment.AttachParent = Car;
	Attachment.AttachComponent = Car->GetRootComponent();
	Attachment.RelativeScale3D = FVector::OneVector;
	Soldier->OnRep_AttachmentReplication();
	TestFalse(TEXT("Replicated attachment closes predicted canopy"), Soldier->IsParachuting());
	TestFalse(TEXT("Vehicle entry clears held parachute input"), Move->bWantsParachute);
	Car->ExitAll();
	Car->Destroy();
	Controller->Destroy();
	Soldier->Destroy();
	// A lone player can use every passenger seat; driver entry remains explicit for bots.
	AHumvee* Humvee = World->SpawnActor<AHumvee>(Location, FRotator::ZeroRotator, Spawn);
	ADFCharacter* Driver = World->SpawnActor<ADFCharacter>(Location + FVector(35, 200, 0), FRotator::ZeroRotator, Spawn);
	AAIController* DriverController = World->SpawnActor<AAIController>();
	DriverController->Possess(Driver);
	Driver->SetInLoadout(false);
	for (const auto& Seat : TArray<TPair<int32, FVector>>{ { 1, FVector(35, 200, 0) }, { 2, FVector(-240, -160, 100) }, { 3, FVector(-240, 160, 100) } }) {
		Driver->SetActorLocation(Location + Seat.Value);
		TestTrue(TEXT("Solo passenger entry"), Humvee->TryEnter(Driver, true));
		TestEqual(TEXT("Nearest passenger seat without a driver"), Humvee->GetSeatOf(Driver), Seat.Key);
		TestNull(TEXT("Passenger entry does not fill the driver seat"), Humvee->GetDriver());
		Humvee->ExitAll();
	}
	Driver->SetActorLocation(Location + FVector(35, 200, 0));
	ADFCharacter* WaitingPassenger = World->SpawnActor<ADFCharacter>(Location + FVector(35, 200, 0), FRotator::ZeroRotator, Spawn);
	WaitingPassenger->SetInLoadout(false);
	TestTrue(TEXT("Passenger can wait in the shotgun seat"), Humvee->EnterSeat(WaitingPassenger, 1));
	TestTrue(TEXT("Front passenger approach uses empty driver seat"), Humvee->TryEnter(Driver));
	TestEqual(TEXT("MF front-seat driver priority with shotgun occupied"), Humvee->GetSeatOf(Driver), 0);
	Humvee->ExitAll();
	WaitingPassenger->Destroy();
	Driver->SetActorLocation(Location + FVector(-240, -160, 100));
	TestTrue(TEXT("Rear approach remains a passenger with no driver"), Humvee->TryEnter(Driver));
	TestEqual(TEXT("Rear seat does not redirect to driver"), Humvee->GetSeatOf(Driver), 2);
	Humvee->ExitAll();
	Driver->SetActorLocation(Location + FVector(35, 200, 0));
	TestTrue(TEXT("Explicit driver entry works from either side"), Humvee->EnterSeat(Driver, 0));
	TestEqual(TEXT("Explicit seat zero means driver"), Humvee->GetSeatOf(Driver), 0);
	TArray<ADFCharacter*> Riders;
	for (const auto& Seat : TArray<TPair<int32, FVector>>{ { 2, FVector(-240, -160, 100) }, { 3, FVector(-240, 160, 100) }, { 1, FVector(35, 200, 0) } }) {
		ADFCharacter* Rider = World->SpawnActor<ADFCharacter>(Location + Seat.Value, FRotator::ZeroRotator, Spawn);
		Rider->SetInLoadout(false);
		TestTrue(TEXT("Enter free passenger seat"), Humvee->TryEnter(Rider));
		TestEqual(TEXT("Nearest free Humvee seat"), Humvee->GetSeatOf(Rider), Seat.Key);
		Riders.Add(Rider);
	}
	Humvee->ExitAll();
	Humvee->Destroy();
	DriverController->Destroy();
	Driver->Destroy();
	for (ADFCharacter* Rider : Riders)
		Rider->Destroy();

	// Imported legacy markers produce real mine/splash generators at match start.
	TSet<AMineGenerator*> Existing;
	for (TActorIterator<AMineGenerator> It(World); It; ++It)
		Existing.Add(*It);
	ADFLegacyActor* MineMarker = World->SpawnActor<ADFLegacyActor>(Location, FRotator::ZeroRotator, Spawn);
	MineMarker->MFClass = TEXT("mineGenerator");
	ADFLegacyActor* SplashMarker = World->SpawnActor<ADFLegacyActor>(Location, FRotator::ZeroRotator, Spawn);
	SplashMarker->MFClass = TEXT("splashMakerGen");
	AMineGenerator::SpawnAll(World);
	int32 Mines = 0, Splashes = 0;
	for (TActorIterator<AMineGenerator> It(World); It; ++It)
		if (!Existing.Contains(*It)) {
			It->bSplash ? ++Splashes : ++Mines;
			It->Destroy();
		}
	TestTrue(TEXT("Mine legacy marker creates a mine"), Mines >= 1);
	TestTrue(TEXT("Splash legacy marker creates a splash generator"), Splashes >= 1);
	MineMarker->Destroy();
	SplashMarker->Destroy();

	// Projection follows both surfaces instead of leaving a flat square across a step.
	TArray<AActor*> Surfaces;
	for (int32 I = 0; I < 2; ++I) {
		AActor* Surface = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Surface);
		Surface->SetRootComponent(Box);
		Box->InitBoxExtent(FVector(100, 180, 10));
		Box->SetWorldLocation(Location + FVector(I ? 100 : -100, 0, I ? 20 : -10));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetMobility(EComponentMobility::Static);
		Box->RegisterComponent();
		Surfaces.Add(Surface);
	}
	ADFProjectedScorch* Scorch = World->SpawnActor<ADFProjectedScorch>(Location, FRotator::ZeroRotator, Spawn);
	TestTrue(TEXT("Full scorch projects onto collision geometry"), Scorch->Project(FVector::UpVector, 240.f, nullptr));
	if (const FProcMeshSection* Section = Scorch->Mesh->GetProcMeshSection(0)) {
		float MinZ = BIG_NUMBER, MaxZ = -BIG_NUMBER;
		for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer) {
			MinZ = FMath::Min(MinZ, Vertex.Position.Z);
			MaxZ = FMath::Max(MaxZ, Vertex.Position.Z);
		}
		TestTrue(TEXT("Full scorch conforms to different heights"), MaxZ - MinZ > 20.f);
	}
	Scorch->Destroy();
	for (AActor* Surface : Surfaces)
		Surface->Destroy();
	const float SavedEffects = Settings.EffectsVolume;
	Settings.EffectsVolume = 0.3f;
	DF::UpdateSoundVolumes();
	USoundWave* Sound = NewObject<USoundWave>();
	DF::EffectsSound(Sound);
	TestEqual(TEXT("Effects sound uses the effects volume class"), Sound->SoundClassObject->Properties.Volume, 0.3f);
	Settings.EffectsVolume = SavedEffects;
	DF::UpdateSoundVolumes();
	State->bParachutes = bOldParachutes;
	return true;
}

#endif
