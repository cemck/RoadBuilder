// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#include "RoadBuilderWorldPartition.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#endif

namespace RoadBuilderWorldPartition
{
	AActor* SpawnGeneratedActor(UWorld* World, UClass* ActorClass, const FTransform& Transform, AActor* ParentActor)
	{
		if (!World || !ActorClass)
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParameters;
		// A World Partition editor can have a different CurrentLevel while cells
		// are being loaded. Generated children must always use their parent's level.
		SpawnParameters.OverrideLevel = ParentActor ? ParentActor->GetLevel() : World->GetCurrentLevel();
#if WITH_EDITOR
		SpawnParameters.bCreateActorPackage = true;
#endif

		AActor* Actor = World->SpawnActor<AActor>(ActorClass, Transform, SpawnParameters);
		SynchronizeGeneratedActor(Actor, ParentActor);
		return Actor;
	}

	void SynchronizeGeneratedActor(AActor* Actor, const AActor* ParentActor)
	{
		if (!Actor)
		{
			return;
		}

#if WITH_EDITOR
		if (ULevel* Level = Actor->GetLevel();
			Level && Level->ShouldCreateNewExternalActors() && Actor->SupportsExternalPackaging() && !Actor->IsPackageExternal())
		{
			// StaticDuplicateObject does not go through UWorld::SpawnActor, so it
			// needs the same OFPA setup explicitly.
			Actor->SetPackageExternal(true);
		}

		if (ParentActor)
		{
			if (Actor->CanChangeIsSpatiallyLoadedFlag())
			{
				Actor->SetIsSpatiallyLoaded(ParentActor->GetIsSpatiallyLoaded());
			}

			UWorld* World = Actor->GetWorld();
			UDataLayerManager* DataLayerManager = World ? World->GetDataLayerManager() : nullptr;
			if (DataLayerManager)
			{
				const TArray<const UDataLayerAsset*> ParentDataLayers = ParentActor->GetDataLayerAssets(false);
				const TArray<const UDataLayerAsset*> ActorDataLayers = Actor->GetDataLayerAssets(false);

				for (const UDataLayerAsset* DataLayerAsset : ActorDataLayers)
				{
					if (!ParentDataLayers.Contains(DataLayerAsset))
					{
						if (const UDataLayerInstance* DataLayerInstance = DataLayerManager->GetDataLayerInstanceFromAsset(DataLayerAsset))
						{
							Actor->RemoveDataLayer(DataLayerInstance);
						}
					}
				}

				for (const UDataLayerAsset* DataLayerAsset : ParentDataLayers)
				{
					if (!Actor->ContainsDataLayer(DataLayerAsset))
					{
						if (const UDataLayerInstance* DataLayerInstance = DataLayerManager->GetDataLayerInstanceFromAsset(DataLayerAsset))
						{
							Actor->AddDataLayer(DataLayerInstance);
						}
					}
				}
			}
		}
#endif
	}

	void SynchronizeGeneratedChildren(AActor* ParentActor)
	{
		if (!ParentActor)
		{
			return;
		}

		TArray<AActor*> AttachedActors;
		ParentActor->GetAttachedActors(AttachedActors, true, true);
		for (AActor* AttachedActor : AttachedActors)
		{
			SynchronizeGeneratedActor(AttachedActor, ParentActor);
		}
	}
}
