#include "Player/DFPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Core/DFTypes.h"
#include "Core/DFGameSettings.h"
#include "Game/DFGameState.h"
#include "Core/DFContent.h"
#include "Game/DFPlayerState.h"
#include "Engine/Console.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "UI/DFHUD.h"
#include "UI/DFMenu.h"
#include "Vehicles/DFVehicle.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "GameFramework/CheatManager.h"
#include "Misc/StringOutputDevice.h"
#include "Game/CTF/Flag.h"
#include "Core/DFUserSettings.h"
#include "World/DFMapActors.h"
#include "Camera/CameraComponent.h"
#include "Game/DFGameMode.h"
#include "AI/DFBotController.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Player/DFCheatManager.h"
#include "Player/DFCharacter.h"
#include "Player/DFCharacterMovement.h"
#include "Weapons/DFWeapons.h"
#include "Loadout/DFLoadout.h"
#include "DesktopForces.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "UnrealClient.h"
#include "EngineUtils.h"
#include "DesktopForces.h"

ADFPlayerController::ADFPlayerController() {
	CheatClass = UDFCheatManager::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

ADFCharacter* ADFPlayerController::GetMFCharacter() const {
	return Cast<ADFCharacter>(GetPawn());
}

void ADFPlayerController::BeginPlay() {
	Super::BeginPlay();
	if (IsLocalController()) {
		// cheat commands always parse here: the server ones go to the server, which says
		// whether this player may (UDFCheatManager::ProcessConsoleExec)
		AddCheats(true);
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		// until we stand somewhere the camera sits wherever the map starts it, textures still
		// streaming in: black, then a fade in on the first spawn (MF showed its loading screen)
		if (PlayerCameraManager) {
			PlayerCameraManager->SetManualCameraFade(1.f, FLinearColor::Black, false);
			GetWorldTimerManager().SetTimer(FadeInTimer, this, &ADFPlayerController::FadeIn, 1.5f, false); // spectators, waiting
		}
	}
}

void ADFPlayerController::AcknowledgePossession(APawn* P) {
	Super::AcknowledgePossession(P);
	FadeInSoon(P);
}

void ADFPlayerController::FadeInSoon(APawn* P) {
	if (P && !bFadedIn && IsLocalController()) {
		// a moment for the spawn spot's textures, then in
		GetWorldTimerManager().SetTimer(FadeInTimer, this, &ADFPlayerController::FadeIn, 0.1f, false);
	}
}

void ADFPlayerController::FadeIn() {
	if (!bFadedIn && PlayerCameraManager) {
		bFadedIn = true;
		PlayerCameraManager->StartCameraFade(1.f, 0.f, 0.3f, FLinearColor::Black, false, false);
	}
}

void ADFPlayerController::OnPossess(APawn* InPawn) {
	Super::OnPossess(InPawn);
	SetControlRotation(InPawn ? InPawn->GetActorRotation() : FRotator::ZeroRotator);
	FadeInSoon(InPawn); // a listen server / standalone player isn't always told through AcknowledgePossession
}

void ADFPlayerController::Name(const FString& NewName) {
	const FString Clean = NewName.TrimStartAndEnd().Replace(TEXT("?"), TEXT("")).Left(20);
	if (Clean.IsEmpty())
		return;
	SetName(Clean); // ServerChangeName -> GameMode ChangeName (everyone is told)
	UDFUserSettings& S = UDFUserSettings::Get();
	S.PlayerName = Clean;
	S.SaveSettings();
}

// --- input setup ---------------------------------------------------------------------

#define LOCTEXT_NAMESPACE "DFControls"

TArray<FDFBinding> ADFPlayerController::GetBindings() {
	// MF DefUser.ini, both keys where it binds two: W / Up = MoveForward, Ctrl / C = Duck,
	// E / Enter = TryEnterExit, Q / Backspace = TryLoadout, MouseWheelDown / ] = NextWeapon ...
	TArray<FDFBinding> Out = {
		{ TEXT("MoveForward"), LOCTEXT("KMoveForward", "Move forward"), EKeys::W, EKeys::Up },
		{ TEXT("MoveBack"), LOCTEXT("KMoveBack", "Move back"), EKeys::S, EKeys::Down },
		{ TEXT("StrafeLeft"), LOCTEXT("KStrafeLeft", "Strafe left"), EKeys::A, EKeys::Left },
		{ TEXT("StrafeRight"), LOCTEXT("KStrafeRight", "Strafe right"), EKeys::D, EKeys::Right },
		{ TEXT("Fire"), LOCTEXT("KFire", "Fire"), EKeys::LeftMouseButton },
		{ TEXT("AltFire"), LOCTEXT("KAltFire", "Alt fire"), EKeys::RightMouseButton },
		{ TEXT("Jump"), LOCTEXT("KJump", "Jump"), EKeys::SpaceBar, EKeys::NumPadZero },
		{ TEXT("Crouch"), LOCTEXT("KCrouch", "Crouch"), EKeys::LeftControl, EKeys::C },
		{ TEXT("Walk"), LOCTEXT("KWalk", "Walk"), EKeys::LeftShift },
		{ TEXT("Reload"), LOCTEXT("KReload", "Reload"), EKeys::R },
		{ TEXT("Loadout"), LOCTEXT("KLoadout", "Loadout"), EKeys::Q, EKeys::BackSpace },
		{ TEXT("Use"), LOCTEXT("KUse", "Use / get in or out"), EKeys::E, EKeys::Enter },
		{ TEXT("NextWeapon"), LOCTEXT("KNextWeapon", "Next weapon"), EKeys::MouseScrollDown, EKeys::RightBracket },
		{ TEXT("PrevWeapon"), LOCTEXT("KPrevWeapon", "Previous weapon"), EKeys::MouseScrollUp, EKeys::LeftBracket },
		{ TEXT("Throw"), LOCTEXT("KThrow", "Drop weapon"), EKeys::Semicolon },
		{ TEXT("Scores"), LOCTEXT("KScores", "Scores"), EKeys::F1 },
	};
	// Number keys 1..9, 0 -> inventory groups 1..10 (knife .. adrenaline); X -> everything
	// above (armour and mod gadgets; unbound in MF's default controls)
	const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero, EKeys::X };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Numbers); ++i)
		Out.Add({ *FString::Printf(TEXT("Slot%d"), i + 1), FText::Format(LOCTEXT("KSlot", "Weapon group {0}"), i + 1), Numbers[i] });
	// keys that run console commands (config, MF User.ini style)
	const TArray<FDFCommandBind>& Commands = GetDefault<ADFPlayerController>()->CommandBindings;
	for (int32 i = 0; i < Commands.Num(); ++i)
		if (!Commands[i].Command.IsEmpty())
			Out.Add({ *FString::Printf(TEXT("Cmd%d"), i), FText::FromString(Commands[i].Command), Commands[i].Key });
	return Out;
}

FKey ADFPlayerController::GetBoundKey(const FDFBinding& B, bool bAlt) {
	const FKey* Changed = UDFUserSettings::Get().KeyBindings.Find(bAlt ? AltSlot(B.Action) : B.Action);
	return Changed ? *Changed : (bAlt ? B.AltDefault : B.Default);
}

#undef LOCTEXT_NAMESPACE

void ADFPlayerController::BuildInput() {
	auto Make = [this](FName Name, EInputActionValueType Type) -> UInputAction* {
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = Type;
		Actions.Add(Name, A);
		return A;
	};
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_MobileForces"));
	Make(TEXT("Move"), EInputActionValueType::Axis2D);
	Make(TEXT("Look"), EInputActionValueType::Axis2D);
	// MF Delete = LookUp, PageDown = LookDown (aLookUp, held)
	Make(TEXT("LookKey"), EInputActionValueType::Axis1D);
	for (const TCHAR* Name : { TEXT("Fire"), TEXT("AltFire"), TEXT("Jump"), TEXT("Crouch"), TEXT("CrouchAlt"), TEXT("Walk"),
			 TEXT("Reload"), TEXT("Loadout"), TEXT("Use"), TEXT("NextWeapon"), TEXT("PrevWeapon"), TEXT("NextWeaponKey"),
			 TEXT("PrevWeaponKey"), TEXT("Throw"), TEXT("LoadoutAlt"), TEXT("Scores"), TEXT("MenuUp"), TEXT("MenuDown"), TEXT("MenuSelect") })
		Make(Name, EInputActionValueType::Boolean);
	for (int32 i = 1; i <= 11; ++i)
		Make(*FString::Printf(TEXT("Slot%d"), i), EInputActionValueType::Boolean);
	for (int32 i = 0; i < CommandBindings.Num(); ++i)
		Make(*FString::Printf(TEXT("Cmd%d"), i), EInputActionValueType::Boolean);
	MapKeys();
}

void ADFPlayerController::MapKeys() {
	MappingContext->UnmapAll();
	auto A = [this](const TCHAR* Name) { return Actions.FindRef(Name).Get(); };
	auto Map = [this](UInputAction* Action, const FKey& Key) -> FEnhancedActionKeyMapping* {
		return Action && Key.IsValid() ? &MappingContext->MapKey(Action, Key) : nullptr;
	};
	TMap<FName, FKey> Keys, AltKeys;
	for (const FDFBinding& B : GetBindings()) {
		Keys.Add(B.Action, GetBoundKey(B));
		AltKeys.Add(B.Action, GetBoundKey(B, true));
	}
	// moving: Move is (X = right, Y = forward); forward / back swizzle into Y, back / left negate
	auto MapMove = [&](const FKey& Key, bool bForwardAxis, bool bNegative) {
		if (FEnhancedActionKeyMapping* M = Map(A(TEXT("Move")), Key)) {
			if (bForwardAxis) {
				UInputModifierSwizzleAxis* S = NewObject<UInputModifierSwizzleAxis>(this);
				S->Order = EInputAxisSwizzle::YXZ;
				M->Modifiers.Add(S);
			}
			if (bNegative)
				M->Modifiers.Add(NewObject<UInputModifierNegate>(this));
		}
	};
	for (const TMap<FName, FKey>* Set : { &Keys, &AltKeys }) {
		MapMove(Set->FindRef(TEXT("MoveForward")), true, false);
		MapMove(Set->FindRef(TEXT("MoveBack")), true, true);
		MapMove(Set->FindRef(TEXT("StrafeLeft")), false, true);
		MapMove(Set->FindRef(TEXT("StrafeRight")), false, false);
	}
	// MF DefUser.ini: , and . strafe too
	MapMove(EKeys::Comma, false, true);
	MapMove(EKeys::Period, false, false);
	Map(A(TEXT("LookKey")), EKeys::Delete);
	if (FEnhancedActionKeyMapping* M = Map(A(TEXT("LookKey")), EKeys::PageDown))
		M->Modifiers.Add(NewObject<UInputModifierNegate>(this));
	Map(A(TEXT("Move")), EKeys::Gamepad_Left2D);
	if (FEnhancedActionKeyMapping* M = Map(A(TEXT("Look")), EKeys::Mouse2D)) {
		UInputModifierNegate* N = NewObject<UInputModifierNegate>(this);
		N->bX = false;
		N->bY = true;
		N->bZ = false;
		M->Modifiers.Add(N);
	}
	Map(A(TEXT("Look")), EKeys::Gamepad_Right2D);

	// the player's keys (MF default controls, Manual.pdf "Controls"), then the fixed extras
	for (const TPair<FName, FKey>& K : Keys)
		if (!K.Key.ToString().StartsWith(TEXT("Move")) && !K.Key.ToString().StartsWith(TEXT("Strafe")))
			Map(Actions.FindRef(K.Key), K.Value);
	// second keys: their own actions where holding both at once matters (crouch) or a wheel
	// notch and a key press differ (weapon cycling)
	static const TMap<FName, FName> AltAction = { { TEXT("Crouch"), TEXT("CrouchAlt") }, { TEXT("Loadout"), TEXT("LoadoutAlt") },
		{ TEXT("NextWeapon"), TEXT("NextWeaponKey") }, { TEXT("PrevWeapon"), TEXT("PrevWeaponKey") } };
	for (const TPair<FName, FKey>& K : AltKeys) {
		if (!K.Key.ToString().StartsWith(TEXT("Move")) && !K.Key.ToString().StartsWith(TEXT("Strafe"))) {
			const FName* Own = AltAction.Find(K.Key);
			Map(Actions.FindRef(Own ? *Own : K.Key), K.Value);
		}
	}
	struct FExtra {
		const TCHAR* Action;
		FKey Key;
	};
	const FExtra Extras[] = {
		{ TEXT("Fire"), EKeys::Gamepad_RightTrigger }, { TEXT("AltFire"), EKeys::Gamepad_LeftTrigger },
		{ TEXT("Jump"), EKeys::Gamepad_FaceButton_Bottom },
		{ TEXT("Crouch"), EKeys::Gamepad_FaceButton_Right }, { TEXT("CrouchAlt"), EKeys::RightControl }, // MF Ctrl: either one
		{ TEXT("Reload"), EKeys::Gamepad_FaceButton_Left }, { TEXT("Loadout"), EKeys::Gamepad_Special_Right },
		{ TEXT("Use"), EKeys::Gamepad_FaceButton_Top }, { TEXT("NextWeapon"), EKeys::Gamepad_RightShoulder },
		{ TEXT("PrevWeapon"), EKeys::Gamepad_LeftShoulder },
		{ TEXT("Scores"), EKeys::Gamepad_Special_Left },
		// in loadout the move keys walk the cursor too (MF MoveForward = LoadoutPrev, MoveBackward = LoadoutNext)
		{ TEXT("MenuUp"), Keys.FindRef(TEXT("MoveForward")) }, { TEXT("MenuUp"), AltKeys.FindRef(TEXT("MoveForward")) },
		{ TEXT("MenuDown"), Keys.FindRef(TEXT("MoveBack")) }, { TEXT("MenuDown"), AltKeys.FindRef(TEXT("MoveBack")) },
		// (Enter selects as Use's second key: Use picks in the loadout and gets in or out elsewhere)
	};
	for (const FExtra& E : Extras)
		Map(A(E.Action), E.Key);
}

void ADFPlayerController::ApplyKeyBindings() {
	if (!MappingContext)
		return;
	MapKeys();
	if (ULocalPlayer* LP = GetLocalPlayer()) {
		if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			Sub->RequestRebuildControlMappings();
	}
}

void ADFPlayerController::SetupInputComponent() {
	Super::SetupInputComponent();
	BuildInput();

	if (ULocalPlayer* LP = GetLocalPlayer()) {
		if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) {
			Sub->ClearAllMappings();
			Sub->AddMappingContext(MappingContext, 0);
		}
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC) {
		UE_LOG(LogDF, Error, TEXT("Enhanced Input component missing; check DefaultInput.ini"));
		return;
	}
	auto A = [this](const TCHAR* Name) { return Actions.FindRef(Name).Get(); };
	EIC->BindAction(A(TEXT("Move")), ETriggerEvent::Triggered, this, &ADFPlayerController::OnMove);
	EIC->BindAction(A(TEXT("Move")), ETriggerEvent::Completed, this, &ADFPlayerController::OnMoveStop);
	EIC->BindAction(A(TEXT("Look")), ETriggerEvent::Triggered, this, &ADFPlayerController::OnLook);
	EIC->BindAction(A(TEXT("Jump")), ETriggerEvent::Started, this, &ADFPlayerController::OnJump);
	EIC->BindAction(A(TEXT("Jump")), ETriggerEvent::Completed, this, &ADFPlayerController::OnJumpReleased);
	EIC->BindAction(A(TEXT("Crouch")), ETriggerEvent::Started, this, &ADFPlayerController::OnCrouch);
	EIC->BindAction(A(TEXT("Crouch")), ETriggerEvent::Completed, this, &ADFPlayerController::OnCrouchReleased);
	EIC->BindAction(A(TEXT("CrouchAlt")), ETriggerEvent::Started, this, &ADFPlayerController::OnCrouch);
	EIC->BindAction(A(TEXT("CrouchAlt")), ETriggerEvent::Completed, this, &ADFPlayerController::OnCrouchReleased);
	EIC->BindActionValueLambda(A(TEXT("LookKey")), ETriggerEvent::Triggered, [this](const FInputActionValue& V) {
		if (!IsLoadoutOpen()) {
			// UE1 PlayerPawn.UpdateRotation: 32 * aLookUp (100) uu/s = 17.6 deg/s
			FRotator R = GetControlRotation();
			R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch + V.Get<float>() * 17.6f * GetWorld()->GetDeltaSeconds()), -89.f, 89.f);
			SetControlRotation(R);
		}
	});
	for (int32 i = 0; i < CommandBindings.Num(); ++i) {
		if (UInputAction* Cmd = A(*FString::Printf(TEXT("Cmd%d"), i))) {
			const FString Command = CommandBindings[i].Command;
			EIC->BindActionValueLambda(Cmd, ETriggerEvent::Started, [this, Command](const FInputActionValue&) { ConsoleCommand(Command); });
		}
	}
	EIC->BindAction(A(TEXT("Walk")), ETriggerEvent::Started, this, &ADFPlayerController::OnWalk);
	EIC->BindAction(A(TEXT("Walk")), ETriggerEvent::Completed, this, &ADFPlayerController::OnWalkReleased);
	EIC->BindAction(A(TEXT("Fire")), ETriggerEvent::Started, this, &ADFPlayerController::OnFire);
	EIC->BindAction(A(TEXT("Fire")), ETriggerEvent::Completed, this, &ADFPlayerController::OnFireReleased);
	EIC->BindAction(A(TEXT("AltFire")), ETriggerEvent::Started, this, &ADFPlayerController::OnAltFire);
	EIC->BindAction(A(TEXT("AltFire")), ETriggerEvent::Completed, this, &ADFPlayerController::OnAltFireReleased);
	EIC->BindAction(A(TEXT("Reload")), ETriggerEvent::Started, this, &ADFPlayerController::OnReload);
	EIC->BindAction(A(TEXT("Loadout")), ETriggerEvent::Started, this, &ADFPlayerController::OnLoadout);
	EIC->BindAction(A(TEXT("LoadoutAlt")), ETriggerEvent::Started, this, &ADFPlayerController::OnLoadout);
	EIC->BindAction(A(TEXT("Throw")), ETriggerEvent::Started, this, &ADFPlayerController::ThrowWeapon);
	EIC->BindAction(A(TEXT("NextWeapon")), ETriggerEvent::Started, this, &ADFPlayerController::OnNextWeapon);
	EIC->BindAction(A(TEXT("PrevWeapon")), ETriggerEvent::Started, this, &ADFPlayerController::OnPrevWeapon);
	EIC->BindAction(A(TEXT("NextWeaponKey")), ETriggerEvent::Started, this, &ADFPlayerController::OnNextWeapon);
	EIC->BindAction(A(TEXT("PrevWeaponKey")), ETriggerEvent::Started, this, &ADFPlayerController::OnPrevWeapon);
	EIC->BindAction(A(TEXT("Scores")), ETriggerEvent::Started, this, &ADFPlayerController::OnScores);
	EIC->BindAction(A(TEXT("Scores")), ETriggerEvent::Completed, this, &ADFPlayerController::OnScoresReleased);
	EIC->BindAction(A(TEXT("MenuUp")), ETriggerEvent::Started, this, &ADFPlayerController::OnMenuUp);
	EIC->BindAction(A(TEXT("MenuDown")), ETriggerEvent::Started, this, &ADFPlayerController::OnMenuDown);
	EIC->BindAction(A(TEXT("MenuSelect")), ETriggerEvent::Started, this, &ADFPlayerController::OnMenuSelect);
	EIC->BindAction(A(TEXT("Use")), ETriggerEvent::Started, this, &ADFPlayerController::OnMenuSelect);
	for (int32 i = 1; i <= 11; ++i)
		EIC->BindAction(A(*FString::Printf(TEXT("Slot%d"), i)), ETriggerEvent::Started, this, &ADFPlayerController::OnWeaponSlot, i);
}

// --- movement / look -----------------------------------------------------------------

void ADFPlayerController::OnMove(const FInputActionValue& Value) {
	if (Cast<ADFVehicle>(GetPawn())) {
		DriveMove = Value.Get<FVector2D>(); // sent every frame by PlayerTick (with the handbrake)
		return;
	}
	ADFCharacter* C = GetMFCharacter();
	if (!C || C->IsInLoadout())
		return;
	const FVector2D V = Value.Get<FVector2D>();
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	C->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V.Y);
	C->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V.X);
}

void ADFPlayerController::OnMoveStop(const FInputActionValue& Value) {
	DriveMove = FVector2D::ZeroVector;
}

void ADFPlayerController::OnLook(const FInputActionValue& Value) {
	if (CommandMenuStage > 0)
		return; // the orders menu's cursor; the loadout screen leaves the mouse free: look around
	const FVector2D V = Value.Get<FVector2D>();
	// Slower look while zoomed, like MF (FOV-scaled mouse)
	const UDFUserSettings& User = UDFUserSettings::Get();
	float Scale = MouseSensitivity * User.MouseSensitivity;
	if (const ADFCharacter* C = GetMFCharacter())
		Scale *= C->GetDesiredFOV() / C->DefaultFOV;
	AddYawInput(V.X * Scale);
	AddPitchInput(V.Y * Scale * (User.bInvertMouse ? -1.f : 1.f));
}

void ADFPlayerController::OnJump() {
	bVehicleHandbrake = true; // jump = handbrake while driving
	if (ADFCharacter* C = GetMFCharacter(); C && !C->IsInLoadout()) {
		C->Jump();
		if (UDFCharacterMovement* M = Cast<UDFCharacterMovement>(C->GetCharacterMovement()))
			M->bWantsParachute = true; // held: the parachute while falling
	}
}

void ADFPlayerController::OnJumpReleased() {
	bVehicleHandbrake = false;
	if (ADFCharacter* C = GetMFCharacter()) {
		C->StopJumping();
		if (UDFCharacterMovement* M = Cast<UDFCharacterMovement>(C->GetCharacterMovement()))
			M->bWantsParachute = false;
	}
}

void ADFPlayerController::OnCrouch() {
	if (ADFVehicle* V = Cast<ADFVehicle>(GetPawn())) {
		V->SetNitro(true); // TurboCars: Player.bDuck in the driver's seat
		return;
	}
	if (ADFCharacter* C = GetMFCharacter(); C && !C->IsInLoadout())
		C->Crouch();
}

void ADFPlayerController::OnCrouchReleased() {
	if (ADFVehicle* V = Cast<ADFVehicle>(GetPawn())) {
		V->SetNitro(false);
		return;
	}
	if (ADFCharacter* C = GetMFCharacter())
		C->UnCrouch();
}

void ADFPlayerController::OnWalk() {
	if (ADFCharacter* C = GetMFCharacter())
		C->SetWalking(true);
}

void ADFPlayerController::OnWalkReleased() {
	if (ADFCharacter* C = GetMFCharacter())
		C->SetWalking(false);
}

// --- weapons -------------------------------------------------------------------------

void ADFPlayerController::OnFire() {
	if (CommandMenuStage > 0) {
		// the orders menu takes mouse clicks (MF OrdersWindow is a UWindow)
		float X = 0.f, Y = 0.f;
		const ADFHUD* HUD = Cast<ADFHUD>(GetHUD());
		const int32 Slot = HUD && GetMousePosition(X, Y) ? HUD->GetCommandMenuSlotAt(FVector2D(X, Y)) : 0;
		if (Slot > 0)
			CommandMenuKey(Slot);
		else
			SetCommandMenuStage(0);
		return;
	}
	if (ADFVehicle* V = Cast<ADFVehicle>(GetPawn())) {
		V->SetDriverFire(true, false);
		return;
	}
	ADFCharacter* C = GetMFCharacter();
	if (C && C->IsInLoadout()) {
		OnMenuSelect(); // MF: left mouse button picks in the loadout screen
		return;
	}
	if (C)
		C->StartFire(false);
}

void ADFPlayerController::OnFireReleased() {
	if (ADFVehicle* V = Cast<ADFVehicle>(GetPawn())) {
		V->SetDriverFire(false, false);
		return;
	}
	if (ADFCharacter* C = GetMFCharacter())
		C->StopFire(false);
}

void ADFPlayerController::OnAltFire() {
	if (ADFVehicle* V = Cast<ADFVehicle>(GetPawn())) {
		V->SetDriverFire(true, true);
		return;
	}
	if (ADFCharacter* C = GetMFCharacter(); C && !C->IsInLoadout())
		C->StartFire(true);
}

void ADFPlayerController::OnAltFireReleased() {
	if (ADFVehicle* V = Cast<ADFVehicle>(GetPawn())) {
		V->SetDriverFire(false, true);
		return;
	}
	if (ADFCharacter* C = GetMFCharacter())
		C->StopFire(true);
}

void ADFPlayerController::OnReload() {
	if (ADFCharacter* C = GetMFCharacter())
		C->Reload();
}

void ADFPlayerController::OnNextWeapon() {
	ADFCharacter* C = GetMFCharacter();
	if (!C)
		return;
	if (C->IsInLoadout()) {
		LoadoutNext();
		return;
	}
	if (ARifle* Rifle = Cast<ARifle>(C->GetCurrentWeapon()); Rifle && Rifle->IsZoomed()) {
		Rifle->AdjustZoom(-1); // MF NextWeapon alias: ZoomOutScope
		return;
	}
	C->CycleWeapon(+1);
}

void ADFPlayerController::OnPrevWeapon() {
	ADFCharacter* C = GetMFCharacter();
	if (!C)
		return;
	if (C->IsInLoadout()) {
		LoadoutPrev();
		return;
	}
	if (ARifle* Rifle = Cast<ARifle>(C->GetCurrentWeapon()); Rifle && Rifle->IsZoomed()) {
		Rifle->AdjustZoom(+1); // MF PrevWeapon alias: ZoomInScope
		return;
	}
	C->CycleWeapon(-1);
}

void ADFPlayerController::OnWeaponSlot(int32 Slot) {
	if (CommandMenuStage > 0) {
		CommandMenuKey(Slot);
		return;
	}
	if (ADFCharacter* C = GetMFCharacter()) {
		if (C->IsInLoadout())
			LoadoutKey(Slot);
		else
			C->SelectInventoryGroup(Slot);
	}
}

int32 ADFPlayerController::GetLoadoutOpenSlot() const {
	return LoadoutOpenSlot != 0 && GetWorld()->GetRealTimeSeconds() - LoadoutOpenTime < 4.f ? LoadoutOpenSlot : 0;
}

void ADFPlayerController::LoadoutKey(int32 Slot) {
	const TArray<TSubclassOf<ADFWeapon>> Items = GetLoadoutItems();
	auto RowOf = [&Items](int32 Key) {
		TArray<int32> Row;
		for (int32 i = 0; i < Items.Num(); ++i)
			if (Items[i] && Items[i]->GetDefaultObject<ADFWeapon>()->GetSlotKey() == Key)
				Row.Add(i);
		return Row;
	};
	auto Pick = [this, &Items](int32 Index) {
		LoadoutCursor = Index;
		LoadoutOpenSlot = 0;
		ServerLoadoutPick(Items[Index]);
	};
	if (const int32 Open = GetLoadoutOpenSlot()) {
		// Second key: item number inside the open row (1..9, 0 = 10th)
		const TArray<int32> Row = RowOf(Open);
		if (Slot <= 10 && Row.IsValidIndex(Slot - 1)) {
			Pick(Row[Slot - 1]);
			return;
		}
		LoadoutOpenSlot = 0; // not an item of the row: treat it as a new row key
	}
	const TArray<int32> Row = RowOf(Slot);
	if (Row.Num() == 1) {
		Pick(Row[0]);
	} else if (Row.Num() > 1) {
		LoadoutOpenSlot = Slot;
		LoadoutOpenTime = GetWorld()->GetRealTimeSeconds();
		LoadoutCursor = Row[0];
	}
}

void ADFPlayerController::OnScores() {
	bShowScores = true;
}

void ADFPlayerController::OnScoresReleased() {
	bShowScores = false;
}

// --- loadout -------------------------------------------------------------------------

bool ADFPlayerController::IsLoadoutOpen() const {
	const ADFCharacter* C = GetMFCharacter();
	return C && C->IsInLoadout();
}

bool ADFPlayerController::IsGroundLoadout() const {
	return !UDFLoadoutLibrary::FindLoadoutSourceFor(GetMFCharacter());
}

TArray<ADFWeapon*> ADFPlayerController::GetGroundWeapons() const {
	TArray<ADFWeapon*> Out;
	const ADFCharacter* C = GetMFCharacter();
	if (!C)
		return Out;
	const float Radius = 562.5f; // MF TryLoadoutDropped: 250 uu
	for (TActorIterator<ADFWeapon> It(GetWorld()); It; ++It)
		if (It->IsDropped() && FVector::DistSquared(It->GetActorLocation(), C->GetActorLocation()) < Radius * Radius)
			Out.Add(*It);
	Out.StableSort([](const ADFWeapon& A, const ADFWeapon& B) { return A.GetSlotKey() < B.GetSlotKey(); });
	return Out;
}

TArray<TSubclassOf<ADFWeapon>> ADFPlayerController::GetLoadoutItems() const {
	TArray<TSubclassOf<ADFWeapon>> Items;
	if (const ADFLoadoutSource* Src = UDFLoadoutLibrary::FindLoadoutSourceFor(GetMFCharacter()))
		Items = Src->GetItems();
	else
		for (const ADFWeapon* W : GetGroundWeapons())
			Items.Add(W->GetClass());
	const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
	Items.RemoveAll([GS](const TSubclassOf<ADFWeapon>& W) { return !W || (GS && !GS->IsItemAllowed(W)); });
	Items.StableSort([](const TSubclassOf<ADFWeapon>& A, const TSubclassOf<ADFWeapon>& B) {
		return A->GetDefaultObject<ADFWeapon>()->GetSlotKey() < B->GetDefaultObject<ADFWeapon>()->GetSlotKey();
	});
	return Items;
}

void ADFPlayerController::OnLoadout() {
	ServerToggleLoadout();
}

void ADFPlayerController::ServerToggleLoadout_Implementation() {
	ADFCharacter* C = GetMFCharacter();
	if (!C || !C->IsAlive())
		return;
	if (C->IsInLoadout()) {
		ExitLoadout();
	} else if (C->GetCharacterMovement()->IsMovingOnGround() && !C->IsDeployed()) {
		// MF: "Can only get loadout when walking" (PHYS_Walking: not with a deployed HMG). Away from crates and loadout zones it still
		// opens: take weapons lying around, put carried ones down (MF only opened it there).
		const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
		if (GS && GS->GetLoadoutAccess(C->GetTeam()) <= 0) {
			if (const ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>())
				GM->LoadoutDenied(this);
			return;
		}
		EnterLoadout();
	}
}

void ADFPlayerController::EnterLoadout() {
	ADFCharacter* C = GetMFCharacter();
	if (!HasAuthority() || !C)
		return;
	C->SetInLoadout(true);
	if (!IsGroundLoadout())
		C->RefillAmmo(); // MF ServerTryLoadout: a crate or zone refills carried weapons
}

void ADFPlayerController::ExitLoadout() {
	if (ADFCharacter* C = GetMFCharacter(); C && HasAuthority()) {
		C->SetInLoadout(false);
		if (!C->GetCurrentWeapon())
			C->SwitchToBestWeapon();
	}
}

void ADFPlayerController::OnMenuUp() {
	LoadoutPrev();
}

void ADFPlayerController::OnMenuDown() {
	LoadoutNext();
}

void ADFPlayerController::OnMenuSelect() {
	if (IsLoadoutOpen())
		LoadoutSelect();
	else
		TryEnterExit(); // MF E = TryEnterExit
}

void ADFPlayerController::TryEnterExit() {
	ServerTryEnterExit();
}

void ADFPlayerController::ServerTryEnterExit_Implementation() {
	if (ADFVehicle* V = Cast<ADFVehicle>(GetPawn())) {
		V->Exit();
		return;
	}
	ADFCharacter* C = GetMFCharacter();
	if (!C || !C->IsAlive() || C->IsInLoadout())
		return;
	if (ADFVehicle* Riding = Cast<ADFVehicle>(C->GetAttachParentActor())) {
		Riding->ExitPassenger(C);
		return;
	}
	// the nearest vehicle with a door in reach
	ADFVehicle* Best = nullptr;
	float BestDist = 1e9f;
	for (TActorIterator<ADFVehicle> It(GetWorld()); It; ++It) {
		const float D = FVector::Dist(It->GetActorLocation(), C->GetActorLocation());
		if (D < BestDist && It->IsNear(C)) {
			BestDist = D;
			Best = *It;
		}
	}
	if (Best)
		Best->TryEnter(C);
}

TArray<ADFWeapon*> ADFPlayerController::GetLoadoutInventory() const {
	TArray<ADFWeapon*> Out;
	if (const ADFCharacter* C = GetMFCharacter()) {
		for (ADFWeapon* W : C->GetInventory())
			if (W && W->bLoadoutSelectable && W->InventoryGroup != 1) // MF: SwitchWeapon F != 1
				Out.Add(W);
	}
	Out.StableSort([](const ADFWeapon& A, const ADFWeapon& B) { return A.InventoryGroup < B.InventoryGroup; });
	return Out;
}

ADFWeapon* ADFPlayerController::GetLoadoutCursorWeapon() const {
	const TArray<ADFWeapon*> Inv = GetLoadoutInventory();
	const int32 i = LoadoutCursor - GetLoadoutItems().Num();
	return Inv.IsValidIndex(i) ? Inv[i] : nullptr;
}

bool ADFPlayerController::IsLoadoutEntryValid(int32 Index) const {
	const ADFCharacter* C = GetMFCharacter();
	if (!C)
		return false;
	const TArray<TSubclassOf<ADFWeapon>> Items = GetLoadoutItems();
	if (Items.IsValidIndex(Index))
		return C->CanCarry(Items[Index]); // RagePlayer LoadoutAvailable.IsSelectionValid: CanIPickup
	return GetLoadoutInventory().IsValidIndex(Index - Items.Num());
}

void ADFPlayerController::LoadoutMove(int32 Dir) {
	const int32 N = GetLoadoutItems().Num() + GetLoadoutInventory().Num();
	for (int32 Step = 0; Step < N; ++Step) {
		LoadoutCursor = ((LoadoutCursor + Dir) % N + N) % N;
		if (IsLoadoutEntryValid(LoadoutCursor))
			return;
	}
}

void ADFPlayerController::LoadoutNext() {
	if (IsLoadoutOpen()) {
		LoadoutOpenSlot = 0;
		LoadoutMove(+1);
	}
}

void ADFPlayerController::LoadoutPrev() {
	if (IsLoadoutOpen()) {
		LoadoutOpenSlot = 0;
		LoadoutMove(-1);
	}
}

void ADFPlayerController::LoadoutSelect() {
	if (!IsLoadoutOpen())
		return;
	const TArray<TSubclassOf<ADFWeapon>> Items = GetLoadoutItems();
	if (Items.IsValidIndex(LoadoutCursor)) {
		if (IsLoadoutEntryValid(LoadoutCursor)) {
			ServerLoadoutPick(Items[LoadoutCursor]);
			PlayUISound(TEXT("/MobileForcesClassic/Sounds/MiscSFX/LoadoutGet.LoadoutGet"));
		}
	} else if (ADFWeapon* W = GetLoadoutCursorWeapon()) {
		ServerLoadoutDrop(W);
		PlayUISound(TEXT("/MobileForcesClassic/Sounds/MiscSFX/LoadoutDrop.LoadoutDrop"));
	}
}

void ADFPlayerController::ClientAnnouncer_Implementation(FName Line) {
	PlayAnnouncer(Line);
}

void ADFPlayerController::PlayAnnouncer(FName Line) {
	USoundBase* Sound = Line.IsNone() ? nullptr : DF::LoadClassic<USoundBase>(TEXT("Sounds/Announcer/") + Line.ToString());
	UE_LOG(LogDF, Verbose, TEXT("Announcer: %s%s"), *Line.ToString(), Sound ? TEXT("") : TEXT(" (no sound)"));
	if (!Sound || !IsLocalController())
		return;
	if (UAudioComponent* Old = AnnouncerAudio.Get())
		Old->Stop();
	AnnouncerAudio = UGameplayStatics::SpawnSound2D(this, Sound);
}

FName ADFPlayerController::TimeRemainingLine(int32 Seconds) {
	// TimeLimitSound[]: one .. ten minutes, then "less than" 15 / 20 / 25 / 30 / 45 / 60
	static const TCHAR* Minutes[] = { TEXT("You_ve_got_one_minute"), TEXT("You_ve_got_two_minutes"), TEXT("You_ve_got_three_minutes"),
		TEXT("You_ve_got_four_minutes"), TEXT("You_ve_got_five_minutes"), TEXT("You_ve_got_six_minutes"), TEXT("You_ve_got_seven_minutes"),
		TEXT("You_ve_got_eight_minutes"), TEXT("You_ve_got_nine_minutes"), TEXT("You_ve_got_ten_minutes") };
	static const TPair<int32, const TCHAR*> LessThan[] = { { 15, TEXT("You_ve_got_less_than_fifteen_minutes") },
		{ 20, TEXT("You_ve_got_less_than_twenty_minutes") }, { 25, TEXT("You_ve_got_less_than_twenty_five_minutes") },
		{ 30, TEXT("You_ve_got_less_than_thirty_minutes") }, { 45, TEXT("You_ve_got_less_than_forty_five_minutes") },
		{ 60, TEXT("You_ve_got_less_than_sixty_minutes") } };
	const int32 M = (Seconds + 15) / 60;
	if (M < 11)
		return Minutes[FMath::Max(M, 1) - 1];
	for (const auto& L : LessThan)
		if (M <= L.Key)
			return L.Value;
	return NAME_None;
}

void ADFPlayerController::TellTime() {
	// RageHUD: in the last five minutes "Battle ends in 5 / 3 minutes" on the minute, then
	// "30 seconds left" and the last ten seconds counted down (RageMessageTime.TimeSound)
	const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
	const int32 T = GS && GS->IsMatchInProgress() ? GS->GetRemainingTime() : -1;
	if (T <= 0 || T > 300 || T == LastReportedTime)
		return;
	LastReportedTime = T;
	if (T == 300 || T == 180)
		PlayAnnouncer(T == 300 ? TEXT("Battle_ends_in_5_minutes") : TEXT("Battle_ends_in_3_minutes"));
	else if (T == 30 || T == 10)
		PlayAnnouncer(T == 30 ? TEXT("30_seconds_left") : TEXT("10_seconds_left"));
	else if (T < 10)
		PlayAnnouncer(FName(*FString::FromInt(T)));
}

void ADFPlayerController::PlayerTick(float DeltaTime) {
	Super::PlayerTick(DeltaTime);
	if (IsLocalController()) {
		TellTime();
		// driving: forward = throttle, strafe = steer, jump = handbrake (the heli climbs on it),
		// every frame while held (vehicles let go after 0.15 s without input)
		if (ADFVehicle* V = Cast<ADFVehicle>(GetPawn()); V && (!DriveMove.IsNearlyZero() || bVehicleHandbrake) && ForcedMoveUntil <= 0.f)
			V->SetDriveInput(DriveMove.Y, DriveMove.X, bVehicleHandbrake);
	}
	// RagePlayer Loadout.PlayerTick -> CheckLoadoutState: keep the cursor on something usable
	const bool bOpen = IsLoadoutOpen();
	if (bOpen && (!bLoadoutWasOpen || !IsLoadoutEntryValid(LoadoutCursor))) {
		if (!bLoadoutWasOpen) {
			LoadoutCursor = -1; // StartLoadout: LoadoutCurrent = 0 -> first usable entry
			LoadoutOpenSlot = 0;
		}
		LoadoutMove(+1);
	}
	bLoadoutWasOpen = bOpen;

	if (ForcedMoveUntil > 0.f) {
		const float Now = GetWorld()->GetTimeSeconds();
		APawn* P = GetPawn();
		if (Now > ForcedMoveUntil || !P) {
			ForcedMoveUntil = 0.f;
			if (ForcedUp > 0.f && Cast<ACharacter>(P))
				OnJumpReleased();
		} else if (ADFVehicle* V = Cast<ADFVehicle>(P)) {
			V->SetDriveInput(ForcedMove.X, ForcedMove.Y, ForcedUp > 0.f);
			V->SetNitro(ForcedUp < 0.f);
		} else {
			const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
			P->AddMovementInput(Yaw.Vector(), ForcedMove.X);
			P->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), ForcedMove.Y);
			if (ForcedUp > 0.f)
				OnJump(); // held, as the key: jumps again on landing, the parachute while falling
		}
		if (P && Now >= ForcedMoveNextLog) {
			ForcedMoveNextLog = Now + 0.25f;
			const UCharacterMovementComponent* M = Cast<ACharacter>(P) ? Cast<ACharacter>(P)->GetCharacterMovement() : nullptr;
			UE_LOG(LogDF, Display, TEXT("DFWalk %.1f: %s mode %s speed %.0f"), Now, *P->GetActorLocation().ToCompactString(),
				M ? *M->GetMovementName() : *P->GetClass()->GetName(), P->GetVelocity().Size());
		}
	}
}

void ADFPlayerController::ForceMove(const FVector2D& Input, float Seconds, float Up) {
	ForcedUp = Up;
	ForcedMove = Input;
	ForcedMoveUntil = GetWorld()->GetTimeSeconds() + Seconds;
	ForcedMoveNextLog = 0.f;
}

void ADFPlayerController::PlayUISound(const TCHAR* Path) {
	UGameplayStatics::PlaySound2D(this, LoadObject<USoundBase>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet));
}

void ADFPlayerController::ServerLoadoutDrop_Implementation(ADFWeapon* Item) {
	ADFCharacter* C = GetMFCharacter();
	if (!C || !C->IsInLoadout() || !Item || !Item->bLoadoutSelectable || !C->GetInventory().Contains(Item))
		return;
	if (IsGroundLoadout()) {
		C->DropWeapon(Item); // RagePlayer.DropItem: away from loadout it lands on the ground
		return;
	}
	// at a crate or in a loadout zone it goes back into the store
	const bool bWasCurrent = C->GetCurrentWeapon() == Item;
	C->RemoveWeapon(Item, true);
	if (bWasCurrent)
		C->SwitchToBestWeapon();
}

// --- MF console commands -------------------------------------------------------------

void ADFPlayerController::Suicide() {
	ServerSuicide();
}

void ADFPlayerController::ServerSuicide_Implementation() {
	// driving: the controller is on the vehicle, the soldier is its driver
	ADFVehicle* V = Cast<ADFVehicle>(GetPawn());
	if (ADFCharacter* C = V ? V->GetDriver() : GetMFCharacter(); C && C->IsAlive())
		C->Die(this, UDFDamage_Suicide::StaticClass(), nullptr); // PlayerPawn.Suicide: KilledBy(None)
}

void ADFPlayerController::TryLoadout() {
	OnLoadout();
}

void ADFPlayerController::SwitchWeapon(int32 Group) {
	OnWeaponSlot(Group);
}

void ADFPlayerController::NextWeapon() {
	OnNextWeapon();
}

void ADFPlayerController::PrevWeapon() {
	OnPrevWeapon();
}

void ADFPlayerController::ThrowWeapon() {
	if (!IsLoadoutOpen())
		ServerDropWeapon();
}

void ADFPlayerController::Drop() {
	ThrowWeapon();
}

void ADFPlayerController::ShowScores() {
	bShowScores = !bShowScores;
}

void ADFPlayerController::SShot() {
	FScreenshotRequest::RequestScreenshot(false);
}

void ADFPlayerController::AddBots(int32 N) {
	ServerAddBots(N, 0.f);
}

void ADFPlayerController::GoodBot(int32 N) {
	ServerAddBots(N, 0.3f);
}

void ADFPlayerController::BadBot(int32 N) {
	ServerAddBots(N, -0.3f);
}

void ADFPlayerController::ServerAddBots_Implementation(int32 N, float SkillOffset) {
	// PlayerPawn.ServerAddBots: admin or single player only; here: the host
	ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>();
	if (!GM || (GetNetMode() != NM_Standalone && !IsLocalController()))
		return;
	GM->AddBots(FMath::Clamp(N, 1, 64), FMath::Clamp(GM->BotSkill + SkillOffset, 0.f, 1.f));
}

void ADFPlayerController::BehindView(int32 On) {
	bBehindView = On < 0 ? !bBehindView : On != 0;
}

void ADFPlayerController::AdminLogin(const FString& Password) {
	ServerAdmin(TEXT("AdminLogin ") + Password.Left(128));
}

void ADFPlayerController::AdminLogout() {
	ServerAdmin(TEXT("AdminLogout"));
}

bool ADFPlayerController::CanCheat() const {
	const ADFPlayerState* PS = GetPlayerState<ADFPlayerState>();
	const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
	return GetNetMode() == NM_Standalone || (PS && PS->bAdmin) || (GS && GS->bCheatsAllowed);
}

void ADFPlayerController::Admin(const FString& Command) {
	ServerAdmin(Command.Left(512));
}

void ADFPlayerController::ServerAdmin_Implementation(const FString& Command) {
	ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>();
	if (!GM || Command.IsEmpty())
		return;
	FString Password;
	if (Command.Split(TEXT(" "), nullptr, &Password) && Command.StartsWith(TEXT("AdminLogin "), ESearchCase::IgnoreCase)) {
		if (!GM->AdminLogin(this, Password.TrimStartAndEnd()))
			ClientMessage(TEXT("Admin login failed."));
		return;
	}
	if (Command.Equals(TEXT("AdminLogout"), ESearchCase::IgnoreCase)) {
		if (ADFPlayerState* PS = GetPlayerState<ADFPlayerState>(); PS && PS->bAdmin && !IsLocalController()) {
			PS->bAdmin = false;
			ClientMessage(TEXT("Logged out."));
		}
		return;
	}
	if (!GM->AllowCheats(this)) {
		ClientMessage(TEXT("Cheats are off here: AdminLogin <password> first."));
		return;
	}
	AddCheats(true);
	UE_LOG(LogDF, Log, TEXT("%s %s: %s"), GM->IsAdmin(this) ? TEXT("Admin") : TEXT("Cheat"), PlayerState ? *PlayerState->GetPlayerName() : TEXT("?"), *Command);
	// cheats for anyone allowed; the rest (this controller's and the pawn's commands, the
	// game mode's, the engine's: servertravel, kick ...) for admins only
	FStringOutputDevice Out;
	APawn* P = GetPawn();
	bool bDone = CheatManager && CheatManager->ProcessConsoleExec(*Command, Out, P);
	if (!bDone && GM->IsAdmin(this))
		bDone = ProcessConsoleExec(*Command, Out, P) || (P && P->ProcessConsoleExec(*Command, Out, P)) || GM->ProcessConsoleExec(*Command, Out, P) || GEngine->Exec(GetWorld(), *Command, Out);
	if (!bDone || !Out.IsEmpty())
		ClientMessage(bDone ? FString(Out).TrimEnd() : FString::Printf(TEXT("Unknown command: %s"), *Command));
}

void ADFPlayerController::CallVote(const FString& Args) {
	ServerCallVote(Args.Left(200));
}

void ADFPlayerController::Vote(const FString& Choice) {
	ServerVote(Choice.StartsWith(TEXT("y"), ESearchCase::IgnoreCase));
}

void ADFPlayerController::Mutate(const FString& Args) {
	const FString A = Args.TrimStartAndEnd();
	if (A.StartsWith(TEXT("callvote"), ESearchCase::IgnoreCase))
		CallVote(A.Mid(8).TrimStart());
	else if (A.StartsWith(TEXT("vote "), ESearchCase::IgnoreCase))
		Vote(A.Mid(5).TrimStart());
	else
		ClientMessage(TEXT("mutate callvote <travel <map> [mode] | nextmap | restart | addbots <n> | kickbots | shuffle | balance>, mutate vote y / n"));
}

void ADFPlayerController::ServerCallVote_Implementation(const FString& Args) {
	if (ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>())
		GM->CallVote(this, Args);
}

void ADFPlayerController::ServerVote_Implementation(bool bYes) {
	if (ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>())
		GM->CastVote(this, bYes);
}

void ADFPlayerController::ListBots() {
	for (TActorIterator<ADFBotController> It(GetWorld()); It; ++It) {
		const APlayerState* PS = It->PlayerState;
		ClientMessage(FString::Printf(TEXT("%s skill %.2f"), PS ? *PS->GetPlayerName() : TEXT("?"), It->Skill));
	}
}

void ADFPlayerController::GrowHUD() {
	if (ADFHUD* HUD = GetHUD<ADFHUD>())
		HUD->ChangeHUDSize(+1);
}

void ADFPlayerController::ChangeCrosshair() {
	if (ADFHUD* HUD = GetHUD<ADFHUD>())
		HUD->ChangeCrosshair(+1);
}

// --- chat, orders, views (MF T / Y / V / F5 / NumPad) ----------------------------------

void ADFPlayerController::StartTyping(const TCHAR* Prefix) {
	// UE1 Console.Talk: the console's typing line with "Say " filled in
	if (const ULocalPlayer* LP = GetLocalPlayer(); LP && LP->ViewportClient && LP->ViewportClient->ViewportConsole)
		LP->ViewportClient->ViewportConsole->StartTyping(Prefix);
}

void ADFPlayerController::Talk() {
	StartTyping(TEXT("Say "));
}

void ADFPlayerController::TeamTalk() {
	StartTyping(TEXT("TeamSay "));
}

void ADFPlayerController::Say(const FString& Msg) {
	if (!Msg.TrimStartAndEnd().IsEmpty())
		ServerSay(Msg.Left(128), false);
}

void ADFPlayerController::TeamSay(const FString& Msg) {
	if (!Msg.TrimStartAndEnd().IsEmpty())
		ServerSay(Msg.Left(128), true);
}

void ADFPlayerController::ServerSay_Implementation(const FString& Msg, bool bTeam) {
	if (ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>())
		GM->BroadcastChat(this, Msg.Left(128), bTeam);
}

void ADFPlayerController::ClientMessage_Implementation(const FString& S, FName Type, float MsgLifeTime) {
	// not Super: it re-sends the line as a chat message from ourselves ("Name: text")
	UE_LOG(LogDF, Log, TEXT("Message: %s"), *S);
	if (ADFHUD* HUD = GetHUD<ADFHUD>())
		HUD->AddMessage(S, FLinearColor::White);
}

void ADFPlayerController::ClientTeamMessage_Implementation(APlayerState* SenderPlayerState, const FString& S, FName Type, float MsgLifeTime) {
	Super::ClientTeamMessage_Implementation(SenderPlayerState, S, Type, MsgLifeTime);
	if (ADFHUD* HUD = GetHUD<ADFHUD>()) {
		const FString Line = SenderPlayerState ? FString::Printf(TEXT("%s: %s"), *SenderPlayerState->GetPlayerName(), *S) : S;
		UE_LOG(LogDF, Log, TEXT("Message: %s"), *Line);
		// MF: team chat in the team colour, everything else white
		const ADFPlayerState* PS = Cast<ADFPlayerState>(SenderPlayerState);
		FLinearColor Col = Type == TEXT("TeamSay") && PS && PS->HasTeam() ? UDFGameSettings::Get().GetTeamColor(PS->GetTeam()) : FLinearColor::White;
		const FString TypeStr = Type.ToString();
		if (TypeStr.StartsWith(TEXT("Event"))) {
			// game events (MF RageSCMessage ...): in the colour of the team they concern
			const int32 Team = FCString::Atoi(*TypeStr.Mid(5));
			Col = Team >= 0 && Team < 255 ? UDFGameSettings::Get().GetTeamColor(uint8(Team)) : FLinearColor::White;
		}
		HUD->AddMessage(Line, Col);
	}
}

TArray<FString> ADFPlayerController::GetCommandMenuOrders() const {
	TArray<FString> Out;
	if (const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>())
		for (int32 i = 0; i < 5; ++i)
			Out.Add(ADFGameMode::GetOrderAbbrev(i));
	return Out;
}

TArray<APlayerState*> ADFPlayerController::GetCommandTargets() const {
	TArray<APlayerState*> Out;
	const ADFPlayerState* Me = GetPlayerState<ADFPlayerState>();
	if (const AGameStateBase* GS = GetWorld()->GetGameState(); GS && Me) {
		for (APlayerState* PS : GS->PlayerArray) {
			const ADFPlayerState* P = Cast<ADFPlayerState>(PS);
			if (P && P != Me && !P->IsSpectator() && P->GetTeam() == Me->GetTeam())
				Out.Add(PS);
		}
	}
	Out.Sort([](const APlayerState& A, const APlayerState& B) { return A.GetPlayerName() < B.GetPlayerName(); });
	return Out;
}

void ADFPlayerController::ShowCommandMenu() {
	SetCommandMenuStage(CommandMenuStage > 0 ? 0 : 1);
	PlayUISound(TEXT("/MobileForcesClassic/Sounds/RageMenuSFX/Click.Click"));
}

void ADFPlayerController::SetCommandMenuStage(int32 Stage) {
	// open: a mouse cursor to click orders with, the view stays put
	CommandMenuStage = Stage;
	bShowMouseCursor = Stage > 0;
	if (Stage > 0) {
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		SetInputMode(Mode);
	} else {
		SetInputMode(FInputModeGameOnly());
	}
}

void ADFPlayerController::Type() {
	// MF Tab = Type: the one-line console (the console key opens the full one, UDFConsole)
	if (UGameViewportClient* VC = GetWorld()->GetGameViewport(); VC && VC->ViewportConsole)
		VC->ViewportConsole->FakeGotoState(FName(TEXT("Typing")));
}

void ADFPlayerController::CommandMenuKey(int32 Slot) {
	// MF OrdersWindow.KeyEvent: 1-9 pick, 0 = the tenth
	const int32 Index = Slot - 1;
	const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
	if (CommandMenuStage == 1) {
		if (!GetCommandMenuOrders().IsValidIndex(Index))
			return;
		CommandMenuOrder = Index;
		if (GS && GS->IsTeamGame()) {
			CommandMenuStage = 2; // TargetChildWindow: All, then each team-mate
		} else {
			ServerGiveOrder(Index, nullptr);
			SetCommandMenuStage(0);
		}
	} else if (CommandMenuStage == 2) {
		const TArray<APlayerState*> Targets = GetCommandTargets();
		if (Index == 0 || Targets.IsValidIndex(Index - 1)) {
			ServerGiveOrder(CommandMenuOrder, Index == 0 ? nullptr : Targets[Index - 1]);
			SetCommandMenuStage(0);
		}
	}
	PlayUISound(TEXT("/MobileForcesClassic/Sounds/RageMenuSFX/Click.Click"));
}

void ADFPlayerController::ServerGiveOrder_Implementation(int32 Order, APlayerState* Target) {
	if (ADFGameMode* GM = GetWorld()->GetAuthGameMode<ADFGameMode>(); GM && Order >= 0 && Order < 5)
		GM->GiveOrder(this, Order, Target);
}

void ADFPlayerController::ViewTeam() {
	ViewTeammate(ViewedTeammate + 1);
}

void ADFPlayerController::ViewPlayerNum(int32 N) {
	// MF: NumPad1 = ViewPlayerNum 0 = back to yourself, NumPad2.. = team-mates
	ViewTeammate(N - 1);
}

void ADFPlayerController::ViewTeammate(int32 Index) {
	const TArray<APlayerState*> Mates = GetCommandTargets();
	if (!Mates.IsValidIndex(Index) || !Mates[Index]->GetPawn()) {
		ViewedTeammate = -1;
		SetViewTargetWithBlend(GetPawn());
		if (Index >= 0)
			ClientMessage(TEXT("Now viewing from own camera"));
		return;
	}
	ViewedTeammate = Index;
	SetViewTargetWithBlend(Mates[Index]->GetPawn());
	ClientMessage(FString::Printf(TEXT("Now viewing from %s"), *Mates[Index]->GetPlayerName()));
}

void ADFPlayerController::CenterView() {
	FRotator R = GetControlRotation();
	R.Pitch = 0.f;
	SetControlRotation(R);
}

void ADFPlayerController::TossFlag() {
	ServerTossFlag();
}

void ADFPlayerController::ServerTossFlag_Implementation() {
	if (AFlag* Flag = AFlag::FindCarriedBy(GetMFCharacter()))
		Flag->Drop();
}

void ADFPlayerController::ShowMenu() {
	if (DFMenu::IsOpen())
		DFMenu::Close(this);
	else
		DFMenu::Open(this, true);
}

void ADFPlayerController::Brightness(float Value) {
	UDFUserSettings& S = UDFUserSettings::Get();
	if (Value > 0.f) {
		S.Brightness = FMath::Clamp(Value, 0.25f, 3.f);
	} else {
		static const float Steps[] = { 0.5f, 0.75f, 1.f, 1.25f, 1.5f, 2.f };
		int32 Next = 0;
		while (Next < UE_ARRAY_COUNT(Steps) && Steps[Next] <= S.Brightness + 0.01f)
			++Next;
		S.Brightness = Steps[Next % UE_ARRAY_COUNT(Steps)];
	}
	S.SaveSettings();
	ADFMapInfo::ApplyAmbient(GetWorld());
	ClientMessage(FString::Printf(TEXT("Brightness %.2f"), S.Brightness));
}

void ADFPlayerController::RenderScale(float Value) {
	UDFUserSettings& S = UDFUserSettings::Get();
	S.RenderScale = Value;
	S.ApplyNonResolutionSettings();
	S.SaveSettings();
	ClientMessage(FString::Printf(TEXT("Render scale %d%%"), FMath::RoundToInt(S.RenderScale * 100.f)));
}

void ADFPlayerController::DFApplyUserSettings() {
	if (ADFCharacter* C = GetMFCharacter())
		C->ApplyUserSettings();
	ApplyKeyBindings();
}

void ADFPlayerController::ShrinkHUD() {
	if (ADFHUD* HUD = GetHUD<ADFHUD>())
		HUD->ChangeHUDSize(-1);
}

void ADFPlayerController::ServerLoadoutPick_Implementation(TSubclassOf<ADFWeapon> Item) {
	ADFCharacter* C = GetMFCharacter();
	const ADFLoadoutSource* Src = C ? UDFLoadoutLibrary::FindLoadoutSourceFor(C) : nullptr;
	const ADFGameState* GS = GetWorld()->GetGameState<ADFGameState>();
	if (!C || !C->IsInLoadout() || !Item || (GS && !GS->IsItemAllowed(Item) && !C->FindWeaponOfClass(Item)))
		return;
	if (!Src) {
		// ground loadout: take the nearest one of that kind lying around
		for (ADFWeapon* W : GetGroundWeapons())
			if (W->GetClass() == Item && C->PickupWeapon(W))
				return;
		return;
	}
	if (!Src->GetItems().Contains(Item))
		return;
	// Selecting an item you carry puts it back; otherwise take one if it fits
	if (C->CanCarry(Item)) {
		ADFWeapon* W = C->GiveWeapon(Item, false);
		if (W && W->bSelectable && W->AutoSwitchPriority > 0.f)
			C->EquipWeapon(W);
		return;
	}
	for (ADFWeapon* W : C->GetInventory()) {
		if (W && W->GetClass() == Item) {
			C->RemoveWeapon(W, true);
			C->SwitchToBestWeapon();
			return;
		}
	}
}

void ADFPlayerController::DFLoadoutPick(int32 Index) {
	const TArray<TSubclassOf<ADFWeapon>> Items = GetLoadoutItems();
	if (Items.IsValidIndex(Index)) {
		LoadoutCursor = Index;
		ServerLoadoutPick(Items[Index]);
	}
}

void ADFPlayerController::DFLoadout() {
	OnLoadout();
}

void ADFPlayerController::DFSelect(int32 Group) {
	OnWeaponSlot(Group);
}

void ADFPlayerController::DFShot(const FString& Name) {
	FScreenshotRequest::RequestScreenshot(Name.IsEmpty() ? TEXT("mf_shot") : Name, false, false);
}

void ADFPlayerController::DFScores() {
	bShowScores = !bShowScores;
}

void ADFPlayerController::ServerDropWeapon_Implementation() {
	ADFCharacter* C = GetMFCharacter();
	ADFWeapon* W = C ? C->GetCurrentWeapon() : nullptr;
	if (W && !W->bUndroppable)
		C->DropWeapon(W); // lands on the ground; anyone takes it through their loadout
}
