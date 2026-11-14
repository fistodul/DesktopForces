#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DFEvents.generated.h"

class UStaticMeshComponent;
class USoundBase;

/**
 * UE1's events: an actor fires its Event, and every actor whose Tag is that name is Triggered
 * (Actor.Trigger). Maps wire touch triggers to kill floors, sounds and messages with it.
 * Everything here happens on the server; what players see and hear goes out replicated
 * (damage, ADFEffectActor sounds, client messages).
 */
UCLASS(Abstract, NotBlueprintable)
class DESKTOPFORCES_API ADFEventActor : public AActor {
	GENERATED_BODY()

public:
	ADFEventActor();

	/** UE1 Tag: the event this actor answers. */
	UPROPERTY(EditAnywhere, Category = "Event")
	FName EventTag;
	/** UE1 Event: what it fires. */
	UPROPERTY(EditAnywhere, Category = "Event")
	FName Event;

	/** Trigger every actor that answers Name. Other: what set it off; By: whose doing it is. */
	static void Fire(const UObject* WorldContext, FName Name, AActor* Other, APawn* By);
	/** Tell By's player something (a driver's controller sits on the vehicle: found by the player state). */
	static void Tell(const APawn* By, const FString& Message);

protected:
	virtual void Triggered(AActor* Other, APawn* By) {}
};

/** UE1 Trigger.TriggerType. */
UENUM()
enum class EDFTriggerBy : uint8 {
	Player, // TT_PlayerProximity: soldiers, bots too (bIsPlayer)
	Human, // TT_ClassProximity of PlayerPawn: not bots
	Pawn, // TT_PawnProximity
	Any, // TT_AnyProximity
	Shot, // TT_Shoot: damage of DamageThreshold or more
};

/** UE1 Trigger states: what being Triggered itself does to it. */
UENUM()
enum class EDFTriggerToggle : uint8 {
	None, // NormalTrigger
	Toggles, // OtherTriggerToggles
	TurnsOn, // OtherTriggerTurnsOn
	TurnsOff, // OtherTriggerTurnsOff
};

/** UE1 Trigger: fires its Event when someone walks into its cylinder (or shoots it). */
UCLASS()
class DESKTOPFORCES_API ADFTrigger : public ADFEventActor {
	GENERATED_BODY()

public:
	ADFTrigger();
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	/** UE1 CollisionRadius / CollisionHeight (cm). */
	void SetCylinder(float Radius, float HalfHeight);

	UPROPERTY(VisibleAnywhere, Category = "Event")
	TObjectPtr<UStaticMeshComponent> Shape;
	UPROPERTY(EditAnywhere, Category = "Event")
	EDFTriggerBy Who = EDFTriggerBy::Player;
	UPROPERTY(EditAnywhere, Category = "Event")
	EDFTriggerToggle Toggle = EDFTriggerToggle::None;
	/** bInitiallyActive, then what the toggle states make of it. */
	UPROPERTY(EditAnywhere, Category = "Event")
	bool bActive = true;
	UPROPERTY(EditAnywhere, Category = "Event")
	bool bOnce = false;
	/** Least time between two firings (s). */
	UPROPERTY(EditAnywhere, Category = "Event")
	float ReTriggerDelay = 0.f;
	/** Above 0: fires again this often while whoever set it off stays inside. */
	UPROPERTY(EditAnywhere, Category = "Event")
	float RepeatTime = 0.f;
	UPROPERTY(EditAnywhere, Category = "Event")
	float DamageThreshold = 0.f;
	/** Shown to whoever sets it off. */
	UPROPERTY(EditAnywhere, Category = "Event")
	FString Message;

protected:
	virtual void BeginPlay() override;
	virtual void Triggered(AActor* Other, APawn* By) override;
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit);
	bool IsRelevant(const AActor* Other) const;
	void Touch(AActor* Other);

	double LastFired = -1e9;
	FTimerHandle RepeatTimer;
};

/** What an ADFEventRelay does when Triggered (UE1 SpecialEvent states, Dispatcher, Counter). */
UENUM()
enum class EDFEventKind : uint8 {
	Message, // SpecialEvent DisplayMessage
	Damage, // DamageInstigator
	Kill, // KillInstigator
	Sound, // PlaySoundEffect: heard around it
	SoundForAll, // PlayersPlaySoundEffect: heard by everyone
	Dispatcher, // fires OutEvents one after the other
	Counter, // fires Event the Count-th time
	Explode, // ExplodingWall / BreakingGlass: fires Event, BreakingSound and a dust puff, once
};

USTRUCT()
struct FDFOutEvent {
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Event")
	FName Event;
	/** Seconds after the one before it (Dispatcher.OutDelays). */
	UPROPERTY(EditAnywhere, Category = "Event")
	float Delay = 0.f;
};

/** UE1 SpecialEvent, Dispatcher and Counter: answers an event with an effect or more events. */
UCLASS()
class DESKTOPFORCES_API ADFEventRelay : public ADFEventActor {
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Event")
	EDFEventKind Kind = EDFEventKind::Message;
	UPROPERTY(EditAnywhere, Category = "Event")
	float Damage = 0.f;
	UPROPERTY(EditAnywhere, Category = "Event")
	TObjectPtr<USoundBase> Sound;
	/** To whoever set it off, or to everyone (bBroadcast). A counter's: when it is done. */
	UPROPERTY(EditAnywhere, Category = "Event")
	FString Message;
	UPROPERTY(EditAnywhere, Category = "Event")
	bool bBroadcast = false;
	UPROPERTY(EditAnywhere, Category = "Event")
	TArray<FDFOutEvent> OutEvents;
	UPROPERTY(EditAnywhere, Category = "Event")
	int32 Count = 0;

protected:
	virtual void Triggered(AActor* Other, APawn* By) override;
};
