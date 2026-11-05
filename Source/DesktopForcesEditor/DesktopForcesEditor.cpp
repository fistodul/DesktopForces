#include "DesktopForcesEditor.h"
#include "AssetCompilingManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY(LogDFEditor);

class FDesktopForcesEditorModule : public FDefaultModuleImpl {
public:
	virtual void StartupModule() override {
		LightingStarted = FEditorDelegates::OnLightingBuildStarted.AddStatic(&RefreshInstanceBounds);
	}

	virtual void ShutdownModule() override {
		FEditorDelegates::OnLightingBuildStarted.Remove(LightingStarted);
	}

private:
	/** Instanced components registered while their mesh was still compiling (async, as a map
	 *  loads) keep zero bounds at the world origin, and the lighting build takes the lights near
	 *  them from those: the instanced props got the sun but none of their point lights, and the
	 *  game reported those as unbuilt (mf-western's benches, 2026-11-04). */
	static void RefreshInstanceBounds() {
		FAssetCompilingManager::Get().FinishAllCompilation();
		int32 Refreshed = 0;
		for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It) {
			if (It->IsTemplate() || !It->IsRegistered() || !It->GetWorld())
				continue;
			It->UpdateBounds();
			It->MarkRenderStateDirty();
			++Refreshed;
		}
		UE_LOG(LogDFEditor, Display, TEXT("Lighting build: bounds of %d instanced components refreshed"), Refreshed);
	}

	FDelegateHandle LightingStarted;
};

IMPLEMENT_MODULE(FDesktopForcesEditorModule, DesktopForcesEditor);
