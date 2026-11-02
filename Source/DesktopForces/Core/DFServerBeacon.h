#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DFServerBeacon.generated.h"

class FSocket;
class FInternetAddr;

/**
 * What makes a server findable, the way UE1 servers were: a UDP query port (game port + 1)
 * answering the GameSpy queries \basic\ \info\ \rules\ \players\ \status\ \echo\, and \secure\
 * with \validate\ (gsmsalg with our cipher), and a \heartbeat\ to each master server every few
 * minutes (333networks' MasterServer speaks this; list in DFGameSettings MasterServers).
 * Dedicated and listen servers; -nomaster or ?Public=0 keeps a server off the masters, the query
 * port still answers for LAN browsing. Hosts forward the game and query ports.
 */
UCLASS()
class DESKTOPFORCES_API UDFServerBeacon : public UGameInstanceSubsystem {
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	/** The game mode has started a map on a server. */
	void Start(int32 GamePort, bool bPublic);
	void Stop();
	/** The masters to use: -master=host:port alone, or the settings' list. */
	static TArray<FString> MasterList();
	/** The masters' name for this game and its validate key (Mobile Forces' own). */
	static constexpr const TCHAR* GameName = TEXT("desktopforces");
	static constexpr const TCHAR* Cipher = TEXT("g3H6eR");
	/** The \validate\ answer to a \secure\ challenge (enctype 0), for tests. */
	static FString ValidateResponse(const FString& Secure, const FString& Cipher);

private:
	bool Tick(float DeltaTime);
	void HandleQuery(const FString& Query, const FInternetAddr& From);
	void ResolveMasters();
	void SendHeartbeat(bool bStateChanged);
	FString Status(const TMap<FString, FString>& Query) const;
	void Reply(const FString& Text, const FInternetAddr& To);

	FSocket* Socket = nullptr;
	FTSTicker::FDelegateHandle TickHandle;
	TArray<TSharedPtr<FInternetAddr>> Masters;
	int32 GamePort = 0, QueryPort = 0;
	bool bPublic = false, bAnnounced = false;
	double NextHeartbeat = 0.0;
	int32 QueryId = 0;
};
