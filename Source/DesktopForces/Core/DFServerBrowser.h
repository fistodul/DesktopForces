#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DFServerBrowser.generated.h"

class FSocket;

/** A server as its query port describes it. */
struct FDFServerEntry {
	FString Address; // ip:query port
	FString Join; // ip:game port
	FString Name, Map, Mode;
	int32 Players = 0, MaxPlayers = 0;
	int32 PingMs = 0;
};

/**
 * The client half of UDFServerBeacon: asks each master for its list of desktopforces servers over
 * TCP (\basic\\secure\ challenge, \validate\, \list\), then every listed server for its \status\
 * over UDP. A failing master is skipped, so a backup keeps the list alive.
 */
UCLASS()
class DESKTOPFORCES_API UDFServerBrowser : public UGameInstanceSubsystem {
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	void Refresh();
	bool IsRefreshing() const { return bBusy; }
	const TArray<FDFServerEntry>& GetServers() const { return Servers; }
	/** Bumped when the list changed (the menu rebuilds its rows). */
	int32 GetVersion() const { return Version; }

private:
	bool Tick(float DeltaTime);
	void QueryServers(const TArray<FString>& Addresses);
	void Parse(const FString& Reply, const FString& From);
	void Close();

	FSocket* Socket = nullptr;
	FTSTicker::FDelegateHandle TickHandle;
	TArray<FDFServerEntry> Servers;
	TMap<FString, double> Asked;
	TMap<FString, FString> Partial;
	double GiveUpAt = 0.0;
	bool bBusy = false;
	int32 Version = 0;
};
