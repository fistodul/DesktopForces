#include "Player/MFPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Core/MFTypes.h"
#include "Core/MFGameSettings.h"
#include "Game/MFGameState.h"
#include "Game/MFPlayerState.h"
#include "Engine/Console.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "UI/MFHUD.h"
#include "UI/MFMenu.h"
#include "Vehicles/MFVehicle.h"
#include "Game/MFFlag.h"
#include "Core/MFUserSettings.h"
#include "World/MFMapActors.h"
#include "Camera/CameraComponent.h"
#include "Game/MFGameMode.h"
#include "AI/MFBotController.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MFCheatManager.h"
#include "Player/MFCharacter.h"
#include "Player/MFCharacterMovement.h"
#include "Weapons/MFWeapons.h"
#include "Loadout/MFLoadout.h"
#include "DesktopForces.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "UnrealClient.h"
#include "EngineUtils.h"
#include "DesktopForces.h"

AMFPlayerController::AMFPlayerController()
{
	CheatClass = UMFCheatManager::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

AMFCharacter* AMFPlayerController::GetMFCharacter() const
{
	return Cast<AMFCharacter>(GetPawn());
}

void AMFPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

void AMFPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	SetControlRotation(InPawn ? InPawn->GetActorRotation() : FRotator::ZeroRotator);
}

// --- input setup ---------------------------------------------------------------------

void AMFPlayerController::BuildInput()
{
	auto Make = [this](FName Name, EInputActionValueType Type) -> UInputAction*
	{
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = Type;
		Actions.Add(Name, A);
		return A;
	};
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_MobileForces"));

	UInputAction* Move = Make(TEXT("Move"), EInputActionValueType::Axis2D);
	UInputAction* Look = Make(TEXT("Look"), EInputActionValueType::Axis2D);

	// WASD -> (X = right, Y = forward)
	auto Swizzle = [this]() { UInputModifierSwizzleAxis* S = NewObject<UInputModifierSwizzleAxis>(this); S->Order = EInputAxisSwizzle::YXZ; return S; };
	auto Negate = [this]() { return NewObject<UInputModifierNegate>(this); };
	MappingContext->MapKey(Move, EKeys::W).Modifiers.Add(Swizzle());
	{
		FEnhancedActionKeyMapping& M = MappingContext->MapKey(Move, EKeys::S);
		M.Modifiers.Add(Swizzle());
		M.Modifiers.Add(Negate());
	}
	MappingContext->MapKey(Move, EKeys::A).Modifiers.Add(Negate());
	MappingContext->MapKey(Move, EKeys::D);
	// MF DefUser.ini: arrows move and strafe, , and . strafe too
	MappingContext->MapKey(Move, EKeys::Up).Modifiers.Add(Swizzle());
	{
		FEnhancedActionKeyMapping& M = MappingContext->MapKey(Move, EKeys::Down);
		M.Modifiers.Add(Swizzle());
		M.Modifiers.Add(Negate());
	}
	MappingContext->MapKey(Move, EKeys::Left).Modifiers.Add(Negate());
	MappingContext->MapKey(Move, EKeys::Right);
	MappingContext->MapKey(Move, EKeys::Comma).Modifiers.Add(Negate());
	MappingContext->MapKey(Move, EKeys::Period);
	// MF Delete = LookUp, PageDown = LookDown (aLookUp, held)
	UInputAction* LookKey = Make(TEXT("LookKey"), EInputActionValueType::Axis1D);
	MappingContext->MapKey(LookKey, EKeys::Delete);
	MappingContext->MapKey(LookKey, EKeys::PageDown).Modifiers.Add(Negate());
	MappingContext->MapKey(Move, EKeys::Gamepad_Left2D);
	{
		FEnhancedActionKeyMapping& M = MappingContext->MapKey(Look, EKeys::Mouse2D);
		UInputModifierNegate* N = Negate();
		N->bX = false;
		N->bY = true;
		N->bZ = false;
		M.Modifiers.Add(N);
	}
	MappingContext->MapKey(Look, EKeys::Gamepad_Right2D);

	// MF default controls (Manual.pdf, "Controls")
	struct FBind { const TCHAR* Name; FKey Key; FKey Alt; };
	const FBind Buttons[] = {
		{ TEXT("Fire"), EKeys::LeftMouseButton, EKeys::Gamepad_RightTrigger },
		{ TEXT("AltFire"), EKeys::RightMouseButton, EKeys::Gamepad_LeftTrigger },
		{ TEXT("Jump"), EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom },
		// MF DefUser.ini has Ctrl and C = Duck; C opens the orders menu here (with V, MF's key)
		{ TEXT("Crouch"), EKeys::LeftControl, EKeys::Gamepad_FaceButton_Right },
		{ TEXT("CrouchAlt"), EKeys::RightControl, FKey() },
		{ TEXT("Walk"), EKeys::LeftShift, FKey() },
		{ TEXT("Reload"), EKeys::R, EKeys::Gamepad_FaceButton_Left },
		{ TEXT("Loadout"), EKeys::Q, EKeys::Gamepad_Special_Right },
		{ TEXT("Use"), EKeys::E, EKeys::Gamepad_FaceButton_Top },
		// MF DefUser.ini: MouseWheelDown / ] = NextWeapon, MouseWheelUp / [ = PrevWeapon
		{ TEXT("NextWeapon"), EKeys::MouseScrollDown, EKeys::Gamepad_RightShoulder },
		{ TEXT("PrevWeapon"), EKeys::MouseScrollUp, EKeys::Gamepad_LeftShoulder },
		{ TEXT("NextWeaponKey"), EKeys::RightBracket, FKey() },
		{ TEXT("PrevWeaponKey"), EKeys::LeftBracket, FKey() },
		{ TEXT("Throw"), EKeys::Semicolon, FKey() },
		{ TEXT("LoadoutAlt"), EKeys::BackSpace, FKey() },
		{ TEXT("Scores"), EKeys::F1, EKeys::Gamepad_Special_Left },
		{ TEXT("ScoresTab"), EKeys::Tab, FKey() },
		// in loadout W/S walk the cursor too (MF MoveForward = LoadoutPrev, MoveBackward = LoadoutNext)
		{ TEXT("MenuUp"), EKeys::Up, EKeys::W },
		{ TEXT("MenuDown"), EKeys::Down, EKeys::S },
		{ TEXT("MenuSelect"), EKeys::Enter, FKey() },
	};
	for (const FBind& B : Buttons)
	{
		UInputAction* A = Make(B.Name, EInputActionValueType::Boolean);
		MappingContext->MapKey(A, B.Key);
		if (B.Alt.IsValid())
		{
			MappingContext->MapKey(A, B.Alt);
		}
	}
	// Number keys 1..9, 0 -> inventory groups 1..10 (knife .. adrenaline); X -> everything
	// above (armour and mod gadgets; unbound in MF's default controls)
	const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero, EKeys::X };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Numbers); ++i)
	{
		UInputAction* A = Make(*FString::Printf(TEXT("Slot%d"), i + 1), EInputActionValueType::Boolean);
		MappingContext->MapKey(A, Numbers[i]);
	}
	MappingContext->MapKey(Actions.FindRef(TEXT("Jump")), EKeys::NumPadZero);   // MF NumPad0 = Jump
	// keys that run console commands (config, MF User.ini style)
	for (int32 i = 0; i < CommandBindings.Num(); ++i)
	{
		if (CommandBindings[i].Key.IsValid() && !CommandBindings[i].Command.IsEmpty())
		{
			MappingContext->MapKey(Make(*FString::Printf(TEXT("Cmd%d"), i), EInputActionValueType::Boolean), CommandBindings[i].Key);
		}
	}
}

void AMFPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	BuildInput();

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Sub->ClearAllMappings();
			Sub->AddMappingContext(MappingContext, 0);
		}
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogMF, Error, TEXT("Enhanced Input component missing; check DefaultInput.ini"));
		return;
	}
	auto A = [this](const TCHAR* Name) { return Actions.FindRef(Name).Get(); };
	EIC->BindAction(A(TEXT("Move")), ETriggerEvent::Triggered, this, &AMFPlayerController::OnMove);
	EIC->BindAction(A(TEXT("Look")), ETriggerEvent::Triggered, this, &AMFPlayerController::OnLook);
	EIC->BindAction(A(TEXT("Jump")), ETriggerEvent::Started, this, &AMFPlayerController::OnJump);
	EIC->BindAction(A(TEXT("Jump")), ETriggerEvent::Completed, this, &AMFPlayerController::OnJumpReleased);
	EIC->BindAction(A(TEXT("Crouch")), ETriggerEvent::Started, this, &AMFPlayerController::OnCrouch);
	EIC->BindAction(A(TEXT("Crouch")), ETriggerEvent::Completed, this, &AMFPlayerController::OnCrouchReleased);
	EIC->BindAction(A(TEXT("CrouchAlt")), ETriggerEvent::Started, this, &AMFPlayerController::OnCrouch);
	EIC->BindAction(A(TEXT("CrouchAlt")), ETriggerEvent::Completed, this, &AMFPlayerController::OnCrouchReleased);
	EIC->BindActionValueLambda(A(TEXT("LookKey")), ETriggerEvent::Triggered, [this](const FInputActionValue& V)
	{
		if (!IsLoadoutOpen())
		{
			// UE1 PlayerPawn.UpdateRotation: 32 * aLookUp (100) uu/s = 17.6 deg/s
			FRotator R = GetControlRotation();
			R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch + V.Get<float>() * 17.6f * GetWorld()->GetDeltaSeconds()), -89.f, 89.f);
			SetControlRotation(R);
		}
	});
	for (int32 i = 0; i < CommandBindings.Num(); ++i)
	{
		if (UInputAction* Cmd = A(*FString::Printf(TEXT("Cmd%d"), i)))
		{
			const FString Command = CommandBindings[i].Command;
			EIC->BindActionValueLambda(Cmd, ETriggerEvent::Started, [this, Command](const FInputActionValue&) { ConsoleCommand(Command); });
		}
	}
	EIC->BindAction(A(TEXT("Walk")), ETriggerEvent::Started, this, &AMFPlayerController::OnWalk);
	EIC->BindAction(A(TEXT("Walk")), ETriggerEvent::Completed, this, &AMFPlayerController::OnWalkReleased);
	EIC->BindAction(A(TEXT("Fire")), ETriggerEvent::Started, this, &AMFPlayerController::OnFire);
	EIC->BindAction(A(TEXT("Fire")), ETriggerEvent::Completed, this, &AMFPlayerController::OnFireReleased);
	EIC->BindAction(A(TEXT("AltFire")), ETriggerEvent::Started, this, &AMFPlayerController::OnAltFire);
	EIC->BindAction(A(TEXT("AltFire")), ETriggerEvent::Completed, this, &AMFPlayerController::OnAltFireReleased);
	EIC->BindAction(A(TEXT("Reload")), ETriggerEvent::Started, this, &AMFPlayerController::OnReload);
	EIC->BindAction(A(TEXT("Loadout")), ETriggerEvent::Started, this, &AMFPlayerController::OnLoadout);
	EIC->BindAction(A(TEXT("LoadoutAlt")), ETriggerEvent::Started, this, &AMFPlayerController::OnLoadout);
	EIC->BindAction(A(TEXT("Throw")), ETriggerEvent::Started, this, &AMFPlayerController::ThrowWeapon);
	EIC->BindAction(A(TEXT("NextWeapon")), ETriggerEvent::Started, this, &AMFPlayerController::OnNextWeapon);
	EIC->BindAction(A(TEXT("PrevWeapon")), ETriggerEvent::Started, this, &AMFPlayerController::OnPrevWeapon);
	EIC->BindAction(A(TEXT("NextWeaponKey")), ETriggerEvent::Started, this, &AMFPlayerController::OnNextWeapon);
	EIC->BindAction(A(TEXT("PrevWeaponKey")), ETriggerEvent::Started, this, &AMFPlayerController::OnPrevWeapon);
	EIC->BindAction(A(TEXT("Scores")), ETriggerEvent::Started, this, &AMFPlayerController::OnScores);
	EIC->BindAction(A(TEXT("Scores")), ETriggerEvent::Completed, this, &AMFPlayerController::OnScoresReleased);
	EIC->BindAction(A(TEXT("ScoresTab")), ETriggerEvent::Started, this, &AMFPlayerController::OnScores);
	EIC->BindAction(A(TEXT("ScoresTab")), ETriggerEvent::Completed, this, &AMFPlayerController::OnScoresReleased);
	EIC->BindAction(A(TEXT("MenuUp")), ETriggerEvent::Started, this, &AMFPlayerController::OnMenuUp);
	EIC->BindAction(A(TEXT("MenuDown")), ETriggerEvent::Started, this, &AMFPlayerController::OnMenuDown);
	EIC->BindAction(A(TEXT("MenuSelect")), ETriggerEvent::Started, this, &AMFPlayerController::OnMenuSelect);
	EIC->BindAction(A(TEXT("Use")), ETriggerEvent::Started, this, &AMFPlayerController::OnMenuSelect);
	for (int32 i = 1; i <= 11; ++i)
	{
		EIC->BindAction(A(*FString::Printf(TEXT("Slot%d"), i)), ETriggerEvent::Started, this, &AMFPlayerController::OnWeaponSlot, i);
	}
}

// --- movement / look -----------------------------------------------------------------

void AMFPlayerController::OnMove(const FInputActionValue& Value)
{
	if (AMFVehicle* V = Cast<AMFVehicle>(GetPawn()))
	{
		const FVector2D In = Value.Get<FVector2D>();
		V->SetDriveInput(In.Y, In.X, bVehicleHandbrake);   // forward = throttle, strafe = steer
		return;
	}
	AMFCharacter* C = GetMFCharacter();
	if (!C || C->IsInLoadout())
	{
		return;
	}
	const FVector2D V = Value.Get<FVector2D>();
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	C->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V.Y);
	C->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V.X);
}

void AMFPlayerController::OnLook(const FInputActionValue& Value)
{
	if (IsLoadoutOpen())
	{
		return;   // MF: the view is frozen in the loadout screen
	}
	const FVector2D V = Value.Get<FVector2D>();
	// Slower look while zoomed, like MF (FOV-scaled mouse)
	const UMFUserSettings* User = UMFUserSettings::Get();
	float Scale = MouseSensitivity * (User ? User->MouseSensitivity : 1.f);
	if (const AMFCharacter* C = GetMFCharacter())
	{
		Scale *= C->GetDesiredFOV() / C->DefaultFOV;
	}
	AddYawInput(V.X * Scale);
	AddPitchInput(V.Y * Scale * (User && User->bInvertMouse ? -1.f : 1.f));
}

void AMFPlayerController::OnJump()
{
	bVehicleHandbrake = true;   // jump = handbrake while driving
	if (AMFCharacter* C = GetMFCharacter(); C && !C->IsInLoadout())
	{
		C->Jump();
	}
}

void AMFPlayerController::OnJumpReleased()
{
	bVehicleHandbrake = false;
	if (AMFCharacter* C = GetMFCharacter())
	{
		C->StopJumping();
	}
}

void AMFPlayerController::OnCrouch()
{
	if (AMFCharacter* C = GetMFCharacter(); C && !C->IsInLoadout())
	{
		C->Crouch();
	}
}

void AMFPlayerController::OnCrouchReleased()
{
	if (AMFCharacter* C = GetMFCharacter())
	{
		C->UnCrouch();
	}
}

void AMFPlayerController::OnWalk()
{
	if (AMFCharacter* C = GetMFCharacter())
	{
		C->SetWalking(true);
	}
}

void AMFPlayerController::OnWalkReleased()
{
	if (AMFCharacter* C = GetMFCharacter())
	{
		C->SetWalking(false);
	}
}

// --- weapons -------------------------------------------------------------------------

void AMFPlayerController::OnFire()
{
	AMFCharacter* C = GetMFCharacter();
	if (C && C->IsInLoadout())
	{
		OnMenuSelect();   // MF: left mouse button picks in the loadout screen
		return;
	}
	if (C)
	{
		C->StartFire(false);
	}
}

void AMFPlayerController::OnFireReleased()
{
	if (AMFCharacter* C = GetMFCharacter())
	{
		C->StopFire(false);
	}
}

void AMFPlayerController::OnAltFire()
{
	if (AMFCharacter* C = GetMFCharacter(); C && !C->IsInLoadout())
	{
		C->StartFire(true);
	}
}

void AMFPlayerController::OnAltFireReleased()
{
	if (AMFCharacter* C = GetMFCharacter())
	{
		C->StopFire(true);
	}
}

void AMFPlayerController::OnReload()
{
	if (AMFCharacter* C = GetMFCharacter())
	{
		C->Reload();
	}
}

void AMFPlayerController::OnNextWeapon()
{
	AMFCharacter* C = GetMFCharacter();
	if (!C)
	{
		return;
	}
	if (C->IsInLoadout())
	{
		LoadoutNext();
		return;
	}
	if (AMFWeapon_Rifle* Rifle = Cast<AMFWeapon_Rifle>(C->GetCurrentWeapon()); Rifle && Rifle->IsZoomed())
	{
		Rifle->AdjustZoom(-1);   // MF NextWeapon alias: ZoomOutScope
		return;
	}
	C->CycleWeapon(+1);
}

void AMFPlayerController::OnPrevWeapon()
{
	AMFCharacter* C = GetMFCharacter();
	if (!C)
	{
		return;
	}
	if (C->IsInLoadout())
	{
		LoadoutPrev();
		return;
	}
	if (AMFWeapon_Rifle* Rifle = Cast<AMFWeapon_Rifle>(C->GetCurrentWeapon()); Rifle && Rifle->IsZoomed())
	{
		Rifle->AdjustZoom(+1);   // MF PrevWeapon alias: ZoomInScope
		return;
	}
	C->CycleWeapon(-1);
}

void AMFPlayerController::OnWeaponSlot(int32 Slot)
{
	if (CommandMenuStage > 0)
	{
		CommandMenuKey(Slot);
		return;
	}
	if (AMFCharacter* C = GetMFCharacter())
	{
		if (C->IsInLoadout())
		{
			LoadoutKey(Slot);
		}
		else
		{
			C->SelectInventoryGroup(Slot);
		}
	}
}

int32 AMFPlayerController::GetLoadoutOpenSlot() const
{
	return LoadoutOpenSlot != 0 && GetWorld()->GetRealTimeSeconds() - LoadoutOpenTime < 4.f ? LoadoutOpenSlot : 0;
}

void AMFPlayerController::LoadoutKey(int32 Slot)
{
	const TArray<TSubclassOf<AMFWeapon>> Items = GetLoadoutItems();
	auto RowOf = [&Items](int32 Key)
	{
		TArray<int32> Row;
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Items[i] && Items[i]->GetDefaultObject<AMFWeapon>()->GetSlotKey() == Key)
			{
				Row.Add(i);
			}
		}
		return Row;
	};
	auto Pick = [this, &Items](int32 Index)
	{
		LoadoutCursor = Index;
		LoadoutOpenSlot = 0;
		ServerLoadoutPick(Items[Index]);
	};
	if (const int32 Open = GetLoadoutOpenSlot())
	{
		// Second key: item number inside the open row (1..9, 0 = 10th)
		const TArray<int32> Row = RowOf(Open);
		if (Slot <= 10 && Row.IsValidIndex(Slot - 1))
		{
			Pick(Row[Slot - 1]);
			return;
		}
		LoadoutOpenSlot = 0;   // not an item of the row: treat it as a new row key
	}
	const TArray<int32> Row = RowOf(Slot);
	if (Row.Num() == 1)
	{
		Pick(Row[0]);
	}
	else if (Row.Num() > 1)
	{
		LoadoutOpenSlot = Slot;
		LoadoutOpenTime = GetWorld()->GetRealTimeSeconds();
		LoadoutCursor = Row[0];
	}
}

void AMFPlayerController::OnScores()
{
	bShowScores = true;
}

void AMFPlayerController::OnScoresReleased()
{
	bShowScores = false;
}

// --- loadout -------------------------------------------------------------------------

bool AMFPlayerController::IsLoadoutOpen() const
{
	const AMFCharacter* C = GetMFCharacter();
	return C && C->IsInLoadout();
}

bool AMFPlayerController::IsGroundLoadout() const
{
	return !UMFLoadoutLibrary::FindLoadoutSourceFor(GetMFCharacter());
}

TArray<AMFWeapon*> AMFPlayerController::GetGroundWeapons() const
{
	TArray<AMFWeapon*> Out;
	const AMFCharacter* C = GetMFCharacter();
	if (!C)
	{
		return Out;
	}
	const float Radius = 562.5f;   // MF TryLoadoutDropped: 250 uu
	for (TActorIterator<AMFWeapon> It(GetWorld()); It; ++It)
	{
		if (It->IsDropped() && FVector::DistSquared(It->GetActorLocation(), C->GetActorLocation()) < Radius * Radius)
		{
			Out.Add(*It);
		}
	}
	Out.StableSort([](const AMFWeapon& A, const AMFWeapon& B) { return A.GetSlotKey() < B.GetSlotKey(); });
	return Out;
}

TArray<TSubclassOf<AMFWeapon>> AMFPlayerController::GetLoadoutItems() const
{
	TArray<TSubclassOf<AMFWeapon>> Items;
	if (const AMFLoadoutSource* Src = UMFLoadoutLibrary::FindLoadoutSourceFor(GetMFCharacter()))
	{
		Items = Src->GetItems();
	}
	else
	{
		for (const AMFWeapon* W : GetGroundWeapons())
		{
			Items.Add(W->GetClass());
		}
	}
	Items.RemoveAll([](const TSubclassOf<AMFWeapon>& W) { return !W; });
	Items.StableSort([](const TSubclassOf<AMFWeapon>& A, const TSubclassOf<AMFWeapon>& B)
	{
		return A->GetDefaultObject<AMFWeapon>()->GetSlotKey() < B->GetDefaultObject<AMFWeapon>()->GetSlotKey();
	});
	return Items;
}

void AMFPlayerController::OnLoadout()
{
	ServerToggleLoadout();
}

void AMFPlayerController::ServerToggleLoadout_Implementation()
{
	AMFCharacter* C = GetMFCharacter();
	if (!C || !C->IsAlive())
	{
		return;
	}
	if (C->IsInLoadout())
	{
		ExitLoadout();
	}
	else if (C->GetCharacterMovement()->IsMovingOnGround())
	{
		// MF: "Can only get loadout when walking". Away from crates and loadout zones it still
		// opens: take weapons lying around, put carried ones down (MF only opened it there).
		EnterLoadout();
	}
}

void AMFPlayerController::EnterLoadout()
{
	AMFCharacter* C = GetMFCharacter();
	if (!HasAuthority() || !C)
	{
		return;
	}
	C->SetInLoadout(true);
	if (!IsGroundLoadout())
	{
		C->RefillAmmo();   // MF ServerTryLoadout: a crate or zone refills carried weapons
	}
}

void AMFPlayerController::ExitLoadout()
{
	if (AMFCharacter* C = GetMFCharacter(); C && HasAuthority())
	{
		C->SetInLoadout(false);
		if (!C->GetCurrentWeapon())
		{
			C->SwitchToBestWeapon();
		}
	}
}

void AMFPlayerController::OnMenuUp()
{
	LoadoutPrev();
}

void AMFPlayerController::OnMenuDown()
{
	LoadoutNext();
}

void AMFPlayerController::OnMenuSelect()
{
	if (IsLoadoutOpen())
	{
		LoadoutSelect();
	}
	else
	{
		TryEnterExit();   // MF E = TryEnterExit
	}
}

void AMFPlayerController::TryEnterExit()
{
	ServerTryEnterExit();
}

void AMFPlayerController::ServerTryEnterExit_Implementation()
{
	if (AMFVehicle* V = Cast<AMFVehicle>(GetPawn()))
	{
		V->Exit();
		return;
	}
	AMFCharacter* C = GetMFCharacter();
	if (!C || !C->IsAlive() || C->IsInLoadout())
	{
		return;
	}
	// the nearest vehicle with a door in reach
	AMFVehicle* Best = nullptr;
	float BestDist = 1e9f;
	for (TActorIterator<AMFVehicle> It(GetWorld()); It; ++It)
	{
		const float D = FVector::Dist(It->GetActorLocation(), C->GetActorLocation());
		if (D < BestDist && It->IsNear(C))
		{
			BestDist = D;
			Best = *It;
		}
	}
	if (Best)
	{
		Best->TryEnter(C);
	}
}

TArray<AMFWeapon*> AMFPlayerController::GetLoadoutInventory() const
{
	TArray<AMFWeapon*> Out;
	if (const AMFCharacter* C = GetMFCharacter())
	{
		for (AMFWeapon* W : C->GetInventory())
		{
			if (W && W->bLoadoutSelectable && W->InventoryGroup != 1)   // MF: SwitchWeapon F != 1
			{
				Out.Add(W);
			}
		}
	}
	Out.StableSort([](const AMFWeapon& A, const AMFWeapon& B) { return A.InventoryGroup < B.InventoryGroup; });
	return Out;
}

AMFWeapon* AMFPlayerController::GetLoadoutCursorWeapon() const
{
	const TArray<AMFWeapon*> Inv = GetLoadoutInventory();
	const int32 i = LoadoutCursor - GetLoadoutItems().Num();
	return Inv.IsValidIndex(i) ? Inv[i] : nullptr;
}

bool AMFPlayerController::IsLoadoutEntryValid(int32 Index) const
{
	const AMFCharacter* C = GetMFCharacter();
	if (!C)
	{
		return false;
	}
	const TArray<TSubclassOf<AMFWeapon>> Items = GetLoadoutItems();
	if (Items.IsValidIndex(Index))
	{
		return C->CanCarry(Items[Index]);   // RagePlayer LoadoutAvailable.IsSelectionValid: CanIPickup
	}
	return GetLoadoutInventory().IsValidIndex(Index - Items.Num());
}

void AMFPlayerController::LoadoutMove(int32 Dir)
{
	const int32 N = GetLoadoutItems().Num() + GetLoadoutInventory().Num();
	for (int32 Step = 0; Step < N; ++Step)
	{
		LoadoutCursor = ((LoadoutCursor + Dir) % N + N) % N;
		if (IsLoadoutEntryValid(LoadoutCursor))
		{
			return;
		}
	}
}

void AMFPlayerController::LoadoutNext()
{
	if (IsLoadoutOpen())
	{
		LoadoutOpenSlot = 0;
		LoadoutMove(+1);
	}
}

void AMFPlayerController::LoadoutPrev()
{
	if (IsLoadoutOpen())
	{
		LoadoutOpenSlot = 0;
		LoadoutMove(-1);
	}
}

void AMFPlayerController::LoadoutSelect()
{
	if (!IsLoadoutOpen())
	{
		return;
	}
	const TArray<TSubclassOf<AMFWeapon>> Items = GetLoadoutItems();
	if (Items.IsValidIndex(LoadoutCursor))
	{
		if (IsLoadoutEntryValid(LoadoutCursor))
		{
			ServerLoadoutPick(Items[LoadoutCursor]);
			PlayUISound(TEXT("/MobileForcesClassic/Sounds/MiscSFX/LoadoutGet.LoadoutGet"));
		}
	}
	else if (AMFWeapon* W = GetLoadoutCursorWeapon())
	{
		ServerLoadoutDrop(W);
		PlayUISound(TEXT("/MobileForcesClassic/Sounds/MiscSFX/LoadoutDrop.LoadoutDrop"));
	}
}

void AMFPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	// RagePlayer Loadout.PlayerTick -> CheckLoadoutState: keep the cursor on something usable
	const bool bOpen = IsLoadoutOpen();
	if (bOpen && (!bLoadoutWasOpen || !IsLoadoutEntryValid(LoadoutCursor)))
	{
		if (!bLoadoutWasOpen)
		{
			LoadoutCursor = -1;   // StartLoadout: LoadoutCurrent = 0 -> first usable entry
			LoadoutOpenSlot = 0;
		}
		LoadoutMove(+1);
	}
	bLoadoutWasOpen = bOpen;

	if (ForcedMoveUntil > 0.f)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		APawn* P = GetPawn();
		if (Now > ForcedMoveUntil || !P)
		{
			ForcedMoveUntil = 0.f;
		}
		else if (AMFVehicle* V = Cast<AMFVehicle>(P))
		{
			V->SetDriveInput(ForcedMove.X, ForcedMove.Y, false);
		}
		else
		{
			const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
			P->AddMovementInput(Yaw.Vector(), ForcedMove.X);
			P->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), ForcedMove.Y);
		}
		if (P && Now >= ForcedMoveNextLog)
		{
			ForcedMoveNextLog = Now + 0.25f;
			const UCharacterMovementComponent* M = Cast<ACharacter>(P) ? Cast<ACharacter>(P)->GetCharacterMovement() : nullptr;
			UE_LOG(LogMF, Display, TEXT("MFWalk %.1f: %s mode %s speed %.0f"), Now, *P->GetActorLocation().ToCompactString(),
				M ? *M->GetMovementName() : *P->GetClass()->GetName(), P->GetVelocity().Size());
		}
	}
}

void AMFPlayerController::ForceMove(const FVector2D& Input, float Seconds)
{
	ForcedMove = Input;
	ForcedMoveUntil = GetWorld()->GetTimeSeconds() + Seconds;
	ForcedMoveNextLog = 0.f;
}

void AMFPlayerController::PlayUISound(const TCHAR* Path)
{
	UGameplayStatics::PlaySound2D(this, LoadObject<USoundBase>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet));
}

void AMFPlayerController::ServerLoadoutDrop_Implementation(AMFWeapon* Item)
{
	AMFCharacter* C = GetMFCharacter();
	if (!C || !C->IsInLoadout() || !Item || !Item->bLoadoutSelectable || !C->GetInventory().Contains(Item))
	{
		return;
	}
	if (IsGroundLoadout())
	{
		C->DropWeapon(Item);   // RagePlayer.DropItem: away from loadout it lands on the ground
		return;
	}
	// at a crate or in a loadout zone it goes back into the store
	const bool bWasCurrent = C->GetCurrentWeapon() == Item;
	C->RemoveWeapon(Item, true);
	if (bWasCurrent)
	{
		C->SwitchToBestWeapon();
	}
}

// --- MF console commands -------------------------------------------------------------

void AMFPlayerController::Suicide()
{
	ServerSuicide();
}

void AMFPlayerController::ServerSuicide_Implementation()
{
	if (AMFCharacter* C = GetMFCharacter(); C && C->IsAlive())
	{
		C->Die(this, UMFDamage_Suicide::StaticClass(), nullptr);   // PlayerPawn.Suicide: KilledBy(None)
	}
}

void AMFPlayerController::TryLoadout()
{
	OnLoadout();
}

void AMFPlayerController::SwitchWeapon(int32 Group)
{
	OnWeaponSlot(Group);
}

void AMFPlayerController::NextWeapon()
{
	OnNextWeapon();
}

void AMFPlayerController::PrevWeapon()
{
	OnPrevWeapon();
}

void AMFPlayerController::ThrowWeapon()
{
	if (!IsLoadoutOpen())
	{
		ServerDropWeapon();
	}
}

void AMFPlayerController::Drop()
{
	ThrowWeapon();
}

void AMFPlayerController::ShowScores()
{
	bShowScores = !bShowScores;
}

void AMFPlayerController::SShot()
{
	FScreenshotRequest::RequestScreenshot(false);
}

void AMFPlayerController::AddBots(int32 N)
{
	ServerAddBots(N, 0.f);
}

void AMFPlayerController::GoodBot(int32 N)
{
	ServerAddBots(N, 0.3f);
}

void AMFPlayerController::BadBot(int32 N)
{
	ServerAddBots(N, -0.3f);
}

void AMFPlayerController::ServerAddBots_Implementation(int32 N, float SkillOffset)
{
	// PlayerPawn.ServerAddBots: admin or single player only; here: the host
	AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>();
	if (!GM || (GetNetMode() != NM_Standalone && !IsLocalController()))
	{
		return;
	}
	GM->AddBots(FMath::Clamp(N, 1, 64), FMath::Clamp(GM->BotSkill + SkillOffset, 0.f, 1.f));
}

void AMFPlayerController::ListBots()
{
	for (TActorIterator<AMFBotController> It(GetWorld()); It; ++It)
	{
		const APlayerState* PS = It->PlayerState;
		ClientMessage(FString::Printf(TEXT("%s skill %.2f"), PS ? *PS->GetPlayerName() : TEXT("?"), It->Skill));
	}
}

void AMFPlayerController::GrowHUD()
{
	if (AMFHUD* HUD = GetHUD<AMFHUD>())
	{
		HUD->ChangeHUDSize(+1);
	}
}

void AMFPlayerController::ChangeCrosshair()
{
	if (AMFHUD* HUD = GetHUD<AMFHUD>())
	{
		HUD->ChangeCrosshair(+1);
	}
}

// --- chat, orders, views (MF T / Y / V / F5 / NumPad) ----------------------------------

void AMFPlayerController::StartTyping(const TCHAR* Prefix)
{
	// UE1 Console.Talk: the console's typing line with "Say " filled in
	if (const ULocalPlayer* LP = GetLocalPlayer(); LP && LP->ViewportClient && LP->ViewportClient->ViewportConsole)
	{
		LP->ViewportClient->ViewportConsole->StartTyping(Prefix);
	}
}

void AMFPlayerController::Talk()
{
	StartTyping(TEXT("Say "));
}

void AMFPlayerController::TeamTalk()
{
	StartTyping(TEXT("TeamSay "));
}

void AMFPlayerController::Say(const FString& Msg)
{
	if (!Msg.TrimStartAndEnd().IsEmpty())
	{
		ServerSay(Msg.Left(128), false);
	}
}

void AMFPlayerController::TeamSay(const FString& Msg)
{
	if (!Msg.TrimStartAndEnd().IsEmpty())
	{
		ServerSay(Msg.Left(128), true);
	}
}

void AMFPlayerController::ServerSay_Implementation(const FString& Msg, bool bTeam)
{
	if (AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>())
	{
		GM->BroadcastChat(this, Msg.Left(128), bTeam);
	}
}

void AMFPlayerController::ClientMessage_Implementation(const FString& S, FName Type, float MsgLifeTime)
{
	// not Super: it re-sends the line as a chat message from ourselves ("Name: text")
	UE_LOG(LogMF, Log, TEXT("Message: %s"), *S);
	if (AMFHUD* HUD = GetHUD<AMFHUD>())
	{
		HUD->AddMessage(S, FLinearColor::White);
	}
}

void AMFPlayerController::ClientTeamMessage_Implementation(APlayerState* SenderPlayerState, const FString& S, FName Type, float MsgLifeTime)
{
	Super::ClientTeamMessage_Implementation(SenderPlayerState, S, Type, MsgLifeTime);
	if (AMFHUD* HUD = GetHUD<AMFHUD>())
	{
		const FString Line = SenderPlayerState ? FString::Printf(TEXT("%s: %s"), *SenderPlayerState->GetPlayerName(), *S) : S;
		// MF: team chat in the team colour, everything else white
		const AMFPlayerState* PS = Cast<AMFPlayerState>(SenderPlayerState);
		FLinearColor Col = Type == TEXT("TeamSay") && PS && PS->HasTeam() ? UMFGameSettings::Get().GetTeamColor(PS->GetTeam()) : FLinearColor::White;
		const FString TypeStr = Type.ToString();
		if (TypeStr.StartsWith(TEXT("Event")))
		{
			// game events (MF RageSCMessage ...): in the colour of the team they concern
			const int32 Team = FCString::Atoi(*TypeStr.Mid(5));
			Col = Team >= 0 && Team < 255 ? UMFGameSettings::Get().GetTeamColor(uint8(Team)) : FLinearColor::White;
		}
		HUD->AddMessage(Line, Col);
	}
}

TArray<FString> AMFPlayerController::GetCommandMenuOrders() const
{
	TArray<FString> Out;
	if (const AMFGameState* GS = GetWorld()->GetGameState<AMFGameState>())
	{
		for (int32 i = 0; i < 5; ++i)
		{
			Out.Add(AMFGameMode::GetOrderAbbrev(i));
		}
	}
	return Out;
}

TArray<APlayerState*> AMFPlayerController::GetCommandTargets() const
{
	TArray<APlayerState*> Out;
	const AMFPlayerState* Me = GetPlayerState<AMFPlayerState>();
	if (const AGameStateBase* GS = GetWorld()->GetGameState(); GS && Me)
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			const AMFPlayerState* P = Cast<AMFPlayerState>(PS);
			if (P && P != Me && !P->IsSpectator() && P->GetTeam() == Me->GetTeam())
			{
				Out.Add(PS);
			}
		}
	}
	Out.Sort([](const APlayerState& A, const APlayerState& B) { return A.GetPlayerName() < B.GetPlayerName(); });
	return Out;
}

void AMFPlayerController::ShowCommandMenu()
{
	CommandMenuStage = CommandMenuStage > 0 ? 0 : 1;
	PlayUISound(TEXT("/MobileForcesClassic/Sounds/RageMenuSFX/click.click"));
}

void AMFPlayerController::CommandMenuKey(int32 Slot)
{
	// MF OrdersWindow.KeyEvent: 1-9 pick, 0 = the tenth
	const int32 Index = Slot - 1;
	const AMFGameState* GS = GetWorld()->GetGameState<AMFGameState>();
	if (CommandMenuStage == 1)
	{
		if (!GetCommandMenuOrders().IsValidIndex(Index))
		{
			return;
		}
		CommandMenuOrder = Index;
		if (GS && GS->IsTeamGame())
		{
			CommandMenuStage = 2;   // TargetChildWindow: All, then each team-mate
		}
		else
		{
			ServerGiveOrder(Index, nullptr);
			CommandMenuStage = 0;
		}
	}
	else if (CommandMenuStage == 2)
	{
		const TArray<APlayerState*> Targets = GetCommandTargets();
		if (Index == 0 || Targets.IsValidIndex(Index - 1))
		{
			ServerGiveOrder(CommandMenuOrder, Index == 0 ? nullptr : Targets[Index - 1]);
			CommandMenuStage = 0;
		}
	}
	PlayUISound(TEXT("/MobileForcesClassic/Sounds/RageMenuSFX/click.click"));
}

void AMFPlayerController::ServerGiveOrder_Implementation(int32 Order, APlayerState* Target)
{
	if (AMFGameMode* GM = GetWorld()->GetAuthGameMode<AMFGameMode>(); GM && Order >= 0 && Order < 5)
	{
		GM->GiveOrder(this, Order, Target);
	}
}

void AMFPlayerController::ViewTeam()
{
	ViewTeammate(ViewedTeammate + 1);
}

void AMFPlayerController::ViewPlayerNum(int32 N)
{
	// MF: NumPad1 = ViewPlayerNum 0 = back to yourself, NumPad2.. = team-mates
	ViewTeammate(N - 1);
}

void AMFPlayerController::ViewTeammate(int32 Index)
{
	const TArray<APlayerState*> Mates = GetCommandTargets();
	if (!Mates.IsValidIndex(Index) || !Mates[Index]->GetPawn())
	{
		ViewedTeammate = -1;
		SetViewTargetWithBlend(GetPawn());
		if (Index >= 0)
		{
			ClientMessage(TEXT("Now viewing from own camera"));
		}
		return;
	}
	ViewedTeammate = Index;
	SetViewTargetWithBlend(Mates[Index]->GetPawn());
	ClientMessage(FString::Printf(TEXT("Now viewing from %s"), *Mates[Index]->GetPlayerName()));
}

void AMFPlayerController::CenterView()
{
	FRotator R = GetControlRotation();
	R.Pitch = 0.f;
	SetControlRotation(R);
}

void AMFPlayerController::TossFlag()
{
	ServerTossFlag();
}

void AMFPlayerController::ServerTossFlag_Implementation()
{
	if (AMFFlag* Flag = AMFFlag::FindCarriedBy(GetMFCharacter()))
	{
		Flag->Drop();
	}
}

void AMFPlayerController::ShowMenu()
{
	if (MFMenu::IsOpen())
	{
		MFMenu::Close(this);
	}
	else
	{
		MFMenu::Open(this, true);
	}
}

void AMFPlayerController::Brightness(float Value)
{
	UMFUserSettings* S = UMFUserSettings::Get();
	if (!S)
	{
		return;
	}
	if (Value > 0.f)
	{
		S->Brightness = FMath::Clamp(Value, 0.25f, 3.f);
	}
	else
	{
		static const float Steps[] = { 0.5f, 0.75f, 1.f, 1.25f, 1.5f, 2.f };
		int32 Next = 0;
		while (Next < UE_ARRAY_COUNT(Steps) && Steps[Next] <= S->Brightness + 0.01f)
		{
			++Next;
		}
		S->Brightness = Steps[Next % UE_ARRAY_COUNT(Steps)];
	}
	S->SaveSettings();
	AMFMapInfo::ApplyAmbient(GetWorld());
	ClientMessage(FString::Printf(TEXT("Brightness %.2f"), S->Brightness));
}

void AMFPlayerController::RenderScale(float Value)
{
	if (UMFUserSettings* S = UMFUserSettings::Get())
	{
		S->RenderScale = Value;
		S->ApplyNonResolutionSettings();
		S->SaveSettings();
		ClientMessage(FString::Printf(TEXT("Render scale %d%%"), FMath::RoundToInt(S->RenderScale * 100.f)));
	}
}

void AMFPlayerController::MFApplyUserSettings()
{
	if (AMFCharacter* C = GetMFCharacter())
	{
		C->ApplyUserSettings();
	}
}

void AMFPlayerController::ShrinkHUD()
{
	if (AMFHUD* HUD = GetHUD<AMFHUD>())
	{
		HUD->ChangeHUDSize(-1);
	}
}

void AMFPlayerController::ServerLoadoutPick_Implementation(TSubclassOf<AMFWeapon> Item)
{
	AMFCharacter* C = GetMFCharacter();
	const AMFLoadoutSource* Src = C ? UMFLoadoutLibrary::FindLoadoutSourceFor(C) : nullptr;
	if (!C || !C->IsInLoadout() || !Item)
	{
		return;
	}
	if (!Src)
	{
		// ground loadout: take the nearest one of that kind lying around
		for (AMFWeapon* W : GetGroundWeapons())
		{
			if (W->GetClass() == Item && C->PickupWeapon(W))
			{
				return;
			}
		}
		return;
	}
	if (!Src->GetItems().Contains(Item))
	{
		return;
	}
	// Selecting an item you carry puts it back; otherwise take one if it fits
	if (C->CanCarry(Item))
	{
		AMFWeapon* W = C->GiveWeapon(Item, false);
		if (W && W->bSelectable && W->AutoSwitchPriority > 0.f)
		{
			C->EquipWeapon(W);
		}
		return;
	}
	for (AMFWeapon* W : C->GetInventory())
	{
		if (W && W->GetClass() == Item)
		{
			C->RemoveWeapon(W, true);
			C->SwitchToBestWeapon();
			return;
		}
	}
}

void AMFPlayerController::MFLoadoutPick(int32 Index)
{
	const TArray<TSubclassOf<AMFWeapon>> Items = GetLoadoutItems();
	if (Items.IsValidIndex(Index))
	{
		LoadoutCursor = Index;
		ServerLoadoutPick(Items[Index]);
	}
}

void AMFPlayerController::MFLoadout()
{
	OnLoadout();
}

void AMFPlayerController::MFSelect(int32 Group)
{
	OnWeaponSlot(Group);
}

void AMFPlayerController::MFShot(const FString& Name)
{
	FScreenshotRequest::RequestScreenshot(Name.IsEmpty() ? TEXT("mf_shot") : Name, false, false);
}

void AMFPlayerController::MFScores()
{
	bShowScores = !bShowScores;
}

void AMFPlayerController::ServerDropWeapon_Implementation()
{
	AMFCharacter* C = GetMFCharacter();
	AMFWeapon* W = C ? C->GetCurrentWeapon() : nullptr;
	if (W && !W->bUndroppable)
	{
		C->DropWeapon(W);   // lands on the ground; anyone takes it through their loadout
	}
}
