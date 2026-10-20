#include "Core/MFGameInstance.h"
#include "DesktopForces.h"
#include "Core/MFGameSettings.h"
#include "UI/MFMenu.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UnrealClient.h"

void UMFGameInstance::Init()
{
	Super::Init();
	UE_LOG(LogMF, Log, TEXT("Desktop Forces game instance up"));
}

void UMFGameInstance::OnStart()
{
	Super::OnStart();
	ScheduleAutoTest();
}

void UMFGameInstance::ScheduleAutoTest()
{
	// Automated smoke test, e.g. from scripts / CI:
	//   -MFAutoShot=6 -MFShotName=warehouse -MFQuitAfter=8 [-MFExec="cmd1|cmd2"]
	// takes a screenshot (Saved/Screenshots/<Platform>/<name>.png) and quits.
	float ShotAt = 0.f, QuitAt = 0.f;
	FParse::Value(FCommandLine::Get(), TEXT("MFAutoShot="), ShotAt);
	FParse::Value(FCommandLine::Get(), TEXT("MFQuitAfter="), QuitAt);
	FParse::Value(FCommandLine::Get(), TEXT("MFShotName="), AutoShotName);
	FString Exec;
	if (FParse::Value(FCommandLine::Get(), TEXT("MFExec="), Exec, false))
	{
		Exec.ParseIntoArray(AutoExec, TEXT("|"));
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Each command may start with "@<seconds>:" to run later (default 1 s), e.g.
	//   -MFExec="@3:MFLoadoutPick 1|@4:MFLoadout|@20:MFShot fight"
	for (const FString& Entry : AutoExec)
	{
		FString Cmd = Entry;
		float Delay = 1.f;
		FString Time, Rest;
		if (Cmd.StartsWith(TEXT("@")) && Cmd.Mid(1).Split(TEXT(":"), &Time, &Rest))
		{
			Delay = FMath::Max(0.1f, FCString::Atof(*Time));
			Cmd = Rest;
		}
		FTimerHandle H;
		World->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Cmd]()
		{
			if (APlayerController* PC = GetFirstLocalPlayerController())
			{
				PC->ConsoleCommand(Cmd);
			}
		}), Delay, false);
	}
	if (ShotAt > 0.f)
	{
		FTimerHandle H;
		World->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const FString Name = AutoShotName.IsEmpty() ? TEXT("mf_autoshot") : AutoShotName;
			// -MFShotUI: include Slate widgets (menus); the HUD canvas is always in the shot
			FScreenshotRequest::RequestScreenshot(Name, FParse::Param(FCommandLine::Get(), TEXT("MFShotUI")), false);
			UE_LOG(LogMF, Display, TEXT("MFAutoShot: requested %s"), *Name);
		}), ShotAt, false);
	}
	if (QuitAt > 0.f)
	{
		FTimerHandle H;
		World->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, []()
		{
			UE_LOG(LogMF, Display, TEXT("MFQuitAfter: exiting"));
			FPlatformMisc::RequestExit(false);
		}), QuitAt, false);
	}
}
