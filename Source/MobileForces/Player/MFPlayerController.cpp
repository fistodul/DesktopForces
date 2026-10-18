#include "Player/MFPlayerController.h"
#include "Player/MFCharacter.h"
#include "Player/MFCharacterMovement.h"
#include "Weapons/MFWeapons.h"
#include "Loadout/MFLoadout.h"
#include "MobileForces.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

AMFPlayerController::AMFPlayerController()
{
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
		{ TEXT("Crouch"), EKeys::C, EKeys::Gamepad_FaceButton_Right },
		{ TEXT("Walk"), EKeys::LeftShift, FKey() },
		{ TEXT("Reload"), EKeys::R, EKeys::Gamepad_FaceButton_Left },
		{ TEXT("Loadout"), EKeys::Q, EKeys::Gamepad_Special_Right },
		{ TEXT("Use"), EKeys::E, EKeys::Gamepad_FaceButton_Top },
		{ TEXT("NextWeapon"), EKeys::MouseScrollUp, EKeys::Gamepad_RightShoulder },
		{ TEXT("PrevWeapon"), EKeys::MouseScrollDown, EKeys::Gamepad_LeftShoulder },
		{ TEXT("NextWeaponKey"), EKeys::LeftBracket, FKey() },
		{ TEXT("PrevWeaponKey"), EKeys::RightBracket, FKey() },
		{ TEXT("Scores"), EKeys::F1, EKeys::Gamepad_Special_Left },
		{ TEXT("ScoresTab"), EKeys::Tab, FKey() },
		{ TEXT("MenuUp"), EKeys::Up, EKeys::Gamepad_DPad_Up },
		{ TEXT("MenuDown"), EKeys::Down, EKeys::Gamepad_DPad_Down },
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
	// Number keys 1..9, 0 -> inventory groups 1..10 (knife .. adrenaline)
	const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero };
	for (int32 i = 0; i < 10; ++i)
	{
		UInputAction* A = Make(*FString::Printf(TEXT("Slot%d"), i + 1), EInputActionValueType::Boolean);
		MappingContext->MapKey(A, Numbers[i]);
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
	EIC->BindAction(A(TEXT("Walk")), ETriggerEvent::Started, this, &AMFPlayerController::OnWalk);
	EIC->BindAction(A(TEXT("Walk")), ETriggerEvent::Completed, this, &AMFPlayerController::OnWalkReleased);
	EIC->BindAction(A(TEXT("Fire")), ETriggerEvent::Started, this, &AMFPlayerController::OnFire);
	EIC->BindAction(A(TEXT("Fire")), ETriggerEvent::Completed, this, &AMFPlayerController::OnFireReleased);
	EIC->BindAction(A(TEXT("AltFire")), ETriggerEvent::Started, this, &AMFPlayerController::OnAltFire);
	EIC->BindAction(A(TEXT("AltFire")), ETriggerEvent::Completed, this, &AMFPlayerController::OnAltFireReleased);
	EIC->BindAction(A(TEXT("Reload")), ETriggerEvent::Started, this, &AMFPlayerController::OnReload);
	EIC->BindAction(A(TEXT("Loadout")), ETriggerEvent::Started, this, &AMFPlayerController::OnLoadout);
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
	for (int32 i = 1; i <= 10; ++i)
	{
		EIC->BindAction(A(*FString::Printf(TEXT("Slot%d"), i)), ETriggerEvent::Started, this, &AMFPlayerController::OnWeaponSlot, i);
	}
}

// --- movement / look -----------------------------------------------------------------

void AMFPlayerController::OnMove(const FInputActionValue& Value)
{
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
	const FVector2D V = Value.Get<FVector2D>();
	// Slower look while zoomed, like MF (FOV-scaled mouse)
	float Scale = MouseSensitivity;
	if (const AMFCharacter* C = GetMFCharacter())
	{
		Scale *= C->GetDesiredFOV() / C->DefaultFOV;
	}
	AddYawInput(V.X * Scale);
	AddPitchInput(V.Y * Scale);
}

void AMFPlayerController::OnJump()
{
	if (AMFCharacter* C = GetMFCharacter(); C && !C->IsInLoadout())
	{
		C->Jump();
	}
}

void AMFPlayerController::OnJumpReleased()
{
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
		OnMenuUp();
		return;
	}
	if (AMFWeapon_Rifle* Rifle = Cast<AMFWeapon_Rifle>(C->GetCurrentWeapon()); Rifle && Rifle->IsZoomed())
	{
		Rifle->AdjustZoom(+1);   // MF: mouse wheel changes sniper magnification while zoomed
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
		OnMenuDown();
		return;
	}
	if (AMFWeapon_Rifle* Rifle = Cast<AMFWeapon_Rifle>(C->GetCurrentWeapon()); Rifle && Rifle->IsZoomed())
	{
		Rifle->AdjustZoom(-1);
		return;
	}
	C->CycleWeapon(-1);
}

void AMFPlayerController::OnWeaponSlot(int32 Slot)
{
	if (AMFCharacter* C = GetMFCharacter(); C && !C->IsInLoadout())
	{
		C->SelectInventoryGroup(Slot);
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

TArray<TSubclassOf<AMFWeapon>> AMFPlayerController::GetLoadoutItems() const
{
	if (const AMFLoadoutSource* Src = UMFLoadoutLibrary::FindLoadoutSourceFor(GetMFCharacter()))
	{
		return Src->Items;
	}
	return UMFLoadoutLibrary::DefaultItems();
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
	else if (UMFLoadoutLibrary::FindLoadoutSourceFor(C) && C->GetCharacterMovement()->IsMovingOnGround())
	{
		EnterLoadout();   // MF: "Can only get loadout when walking"
	}
	else
	{
		ServerDropWeapon_Implementation();
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
	C->RefillAmmo();   // MF: entering loadout replenishes the ammunition of carried weapons
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
	if (IsLoadoutOpen())
	{
		const int32 N = GetLoadoutItems().Num();
		LoadoutCursor = N > 0 ? (LoadoutCursor - 1 + N) % N : 0;
	}
}

void AMFPlayerController::OnMenuDown()
{
	if (IsLoadoutOpen())
	{
		const int32 N = GetLoadoutItems().Num();
		LoadoutCursor = N > 0 ? (LoadoutCursor + 1) % N : 0;
	}
}

void AMFPlayerController::OnMenuSelect()
{
	if (!IsLoadoutOpen())
	{
		return;
	}
	const TArray<TSubclassOf<AMFWeapon>> Items = GetLoadoutItems();
	if (Items.IsValidIndex(LoadoutCursor))
	{
		ServerLoadoutPick(Items[LoadoutCursor]);
	}
}

void AMFPlayerController::ServerLoadoutPick_Implementation(TSubclassOf<AMFWeapon> Item)
{
	AMFCharacter* C = GetMFCharacter();
	const AMFLoadoutSource* Src = C ? UMFLoadoutLibrary::FindLoadoutSourceFor(C) : nullptr;
	if (!C || !C->IsInLoadout() || !Src || !Src->Items.Contains(Item))
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

void AMFPlayerController::ServerDropWeapon_Implementation()
{
	AMFCharacter* C = GetMFCharacter();
	AMFWeapon* W = C ? C->GetCurrentWeapon() : nullptr;
	if (W && !W->bUndroppable)
	{
		// TODO: spawn a pickup others can grab (MF DropFrom); for now the item is discarded.
		C->RemoveWeapon(W, true);
		C->SwitchToBestWeapon();
	}
}
