#include "Core/MFGameInstance.h"
#include "MobileForces.h"
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
	UE_LOG(LogMF, Log, TEXT("Mobile Forces game instance up"));
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
	if (AutoExec.Num() > 0)
	{
		FTimerHandle H;
		World->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (APlayerController* PC = GetFirstLocalPlayerController())
			{
				for (const FString& Cmd : AutoExec)
				{
					PC->ConsoleCommand(Cmd);
				}
			}
		}), 1.f, false);
	}
	if (ShotAt > 0.f)
	{
		FTimerHandle H;
		World->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const FString Name = AutoShotName.IsEmpty() ? TEXT("mf_autoshot") : AutoShotName;
			FScreenshotRequest::RequestScreenshot(Name, false, false);
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
