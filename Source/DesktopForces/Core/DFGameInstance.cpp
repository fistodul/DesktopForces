#include "Core/DFGameInstance.h"
#include "Core/DFUserSettings.h"
#include "DesktopForces.h"
#include "Core/DFGameSettings.h"
#include "UI/DFMenu.h"
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

void UDFGameInstance::Init() {
	Super::Init();
	UE_LOG(LogDF, Log, TEXT("Desktop Forces game instance up"));
}

void UDFGameInstance::OnStart() {
	Super::OnStart();
	UDFUserSettings::Get().ApplyGamma();
	UDFUserSettings::Get().ApplyConsole(); // the viewport is up now
	ScheduleAutoTest();
	if (PLATFORM_ANDROID || PLATFORM_IOS || FParse::Param(FCommandLine::Get(), TEXT("DFStepDown")))
		StepDownTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDFGameInstance::StepDownTick));
}

void UDFGameInstance::Shutdown() {
	FTSTicker::GetCoreTicker().RemoveTicker(StepDownTicker);
	Super::Shutdown();
}

bool UDFGameInstance::StepDownTick(float DeltaTime) {
	const APlayerController* PC = GetFirstLocalPlayerController();
	if (!PC || !PC->GetPawn()) {
		// menus, loading, dead: not a fair sample
		WindowTime = 0.f;
		WindowFrames = 0;
		SlowWindows = 0;
		return true;
	}
	WindowTime += DeltaTime;
	++WindowFrames;
	if (WindowTime < 10.f)
		return true;
	const float Fps = WindowFrames / WindowTime;
	WindowTime = 0.f;
	WindowFrames = 0;
	const float Cap = StepDownCap > 0.f ? StepDownCap : UDFUserSettings::Get().GetFrameRateLimit();
	SlowWindows = Cap > 0.f && Fps < Cap * 0.9f ? SlowWindows + 1 : 0;
	if (SlowWindows < 3)
		return true;
	SlowWindows = 0;
	float Next = 0.f;
	for (const float Rate : UDFUserSettings::RecommendedFrameRates(Cap))
		if (Rate > 0.f && Rate < Cap * 0.95f)
			Next = FMath::Max(Next, Rate);
	if (Next > 0.f) {
		UE_LOG(LogDF, Display, TEXT("Frame cap %.0f -> %.0f FPS: %.1f FPS for 30 s"), Cap, Next, Fps);
		StepDownCap = Next;
		GEngine->SetMaxFPS(Next);
	}
	return true;
}

void UDFGameInstance::ScheduleAutoTest() {
	// Automated smoke test, e.g. from scripts / CI:
	//   -DFAutoShot=6 -DFShotName=warehouse -DFQuitAfter=8 [-DFExec="cmd1|cmd2"]
	// takes a screenshot (Saved/Screenshots/<Platform>/<name>.png) and quits.
	float ShotAt = 0.f, QuitAt = 0.f;
	FParse::Value(FCommandLine::Get(), TEXT("DFAutoShot="), ShotAt);
	FParse::Value(FCommandLine::Get(), TEXT("DFQuitAfter="), QuitAt);
	FParse::Value(FCommandLine::Get(), TEXT("DFShotName="), AutoShotName);
	FString Exec;
	if (FParse::Value(FCommandLine::Get(), TEXT("DFExec="), Exec, false))
		Exec.ParseIntoArray(AutoExec, TEXT("|"));
	UWorld* World = GetWorld();
	if (!World)
		return;
	// Each command may start with "@<seconds>:" to run later (default 1 s), e.g.
	//   -DFExec="@3:DFLoadoutPick 1|@4:DFLoadout|@20:DFShot fight"
	for (const FString& Entry : AutoExec) {
		FString Cmd = Entry;
		float Delay = 1.f;
		FString Time, Rest;
		if (Cmd.StartsWith(TEXT("@")) && Cmd.Mid(1).Split(TEXT(":"), &Time, &Rest)) {
			Delay = FMath::Max(0.1f, FCString::Atof(*Time));
			Cmd = Rest;
		}
		FTimerHandle H;
		World->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Cmd]() {
			if (APlayerController* PC = GetFirstLocalPlayerController())
				PC->ConsoleCommand(Cmd);
		}),
			Delay, false);
	}
	if (ShotAt > 0.f) {
		FTimerHandle H;
		World->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]() {
			const FString Name = AutoShotName.IsEmpty() ? TEXT("mf_autoshot") : AutoShotName;
			// -DFShotUI: include Slate widgets (menus); the HUD canvas is always in the shot
			FScreenshotRequest::RequestScreenshot(Name, FParse::Param(FCommandLine::Get(), TEXT("DFShotUI")), false);
			UE_LOG(LogDF, Display, TEXT("DFAutoShot: requested %s"), *Name);
		}),
			ShotAt, false);
	}
	if (QuitAt > 0.f) {
		FTimerHandle H;
		World->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, []() {
			UE_LOG(LogDF, Display, TEXT("DFQuitAfter: exiting"));
			FPlatformMisc::RequestExit(false);
		}),
			QuitAt, false);
	}
}
