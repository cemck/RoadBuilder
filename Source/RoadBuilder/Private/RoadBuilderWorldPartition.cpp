// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#include "RoadBuilderWorldPartition.h"

#include "RoadBuilder.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#endif

namespace RoadBuilderWorldPartition
{
	bool UsesDetachedGeneratedActors(const AActor* ParentActor)
	{
#if WITH_EDITOR
		if (!IsValid(ParentActor))
		{
			return false;
		}

		const ULevel* ParentLevel = ParentActor->GetLevel();
		return ParentLevel && ParentLevel->ShouldCreateNewExternalActors();
#else
		return false;
#endif
	}

	void ParentGeneratedActor(AActor* Actor, AActor* ParentActor)
	{
		if (!IsValid(Actor) || !IsValid(ParentActor))
		{
			return;
		}

		Actor->Tags.AddUnique(TEXT("RoadBuilder.GeneratedActor"));
		SynchronizeGeneratedActor(Actor, ParentActor);
		if (UsesDetachedGeneratedActors(ParentActor))
		{
			// Do not serialize an AttachmentParent import into an OFPA package.
			Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
		else
		{
			Actor->AttachToActor(ParentActor, FAttachmentTransformRules::KeepWorldTransform);
		}
	}

	void PrepareGeneratedGeometryActor(AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return;
		}

		// RoadBuilder's procedural vertices are absolute world coordinates.  The
		// old attachment hierarchy incidentally kept these actors at world
		// identity.  Detached World Partition actors are individually selectable,
		// so restore that invariant before every build and prevent editor gizmos
		// from introducing a second transform on top of the authored vertices.
		if (!Actor->GetActorTransform().Equals(FTransform::Identity))
		{
			Actor->SetActorTransform(
				FTransform::Identity,
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}
#if WITH_EDITOR
		Actor->SetLockLocation(true);
#endif
	}

	void DestroyAttachedGeneratedActors(AActor* ParentActor)
	{
		if (!IsValid(ParentActor))
		{
			return;
		}

		TArray<AActor*> AttachedActors;
		ParentActor->GetAttachedActors(AttachedActors, true, true);
		for (int32 Index = AttachedActors.Num() - 1; Index >= 0; --Index)
		{
			AActor* AttachedActor = AttachedActors[Index];
			if (IsValid(AttachedActor) && !AttachedActor->IsActorBeingDestroyed())
			{
				AttachedActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
				AttachedActor->Destroy();
			}
		}
	}

	bool CanMutateGeneratedActorGraph(UWorld* World, const AActor* ParentActor, FString* OutFailureReason)
	{
		auto Reject = [OutFailureReason](const TCHAR* Reason)
		{
			if (OutFailureReason)
			{
				*OutFailureReason = Reason;
			}
			return false;
		};

		if (!IsInGameThread())
		{
			return Reject(TEXT("the request was not made on the game thread"));
		}
		if (!IsValid(World))
		{
			return Reject(TEXT("the target world is no longer valid"));
		}
		if (World->bIsTearingDown)
		{
			return Reject(TEXT("the target world is tearing down"));
		}
		if (IsGarbageCollecting())
		{
			return Reject(TEXT("garbage collection is running"));
		}
		if (UE::IsSavingPackage())
		{
			return Reject(TEXT("a package save is in progress"));
		}
#if WITH_EDITOR
		if (GIsTransacting)
		{
			return Reject(TEXT("an undo or redo transaction is in progress"));
		}
#endif
		if (!IsValid(World->GetCurrentLevel()))
		{
			return Reject(TEXT("the world has no current loaded level"));
		}

		if (!ParentActor)
		{
			return true;
		}
		if (!IsValid(ParentActor) || ParentActor->IsActorBeingDestroyed())
		{
			return Reject(TEXT("the generated actor's parent is invalid or being destroyed"));
		}
		if (ParentActor->GetWorld() != World)
		{
			return Reject(TEXT("the generated actor's parent belongs to another world"));
		}
		ULevel* ParentLevel = ParentActor->GetLevel();
		if (!IsValid(ParentLevel) || !World->GetLevels().Contains(ParentLevel))
		{
			return Reject(TEXT("the generated actor's parent level is not loaded"));
		}
		return true;
	}

	AActor* SpawnGeneratedActor(UWorld* World, UClass* ActorClass, const FTransform& Transform, AActor* ParentActor)
	{
		FString FailureReason;
		if (!ActorClass || !CanMutateGeneratedActorGraph(World, ParentActor, &FailureReason))
		{
			UE_LOG(LogRoadBuilder, Warning, TEXT("RoadBuilder rejected generated actor creation: %s."),
				ActorClass ? *FailureReason : TEXT("no actor class was supplied"));
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
		if (Actor)
		{
			Actor->Tags.AddUnique(TEXT("RoadBuilder.GeneratedActor"));
		}
		SynchronizeGeneratedActor(Actor, ParentActor);
		return Actor;
	}

	void SynchronizeGeneratedActor(AActor* Actor, const AActor* ParentActor)
	{
		if (!Actor)
		{
			return;
		}
		FString FailureReason;
		if (!CanMutateGeneratedActorGraph(Actor->GetWorld(), ParentActor, &FailureReason))
		{
			UE_LOG(LogRoadBuilder, Warning, TEXT("RoadBuilder rejected World Partition synchronization for %s: %s."),
				*Actor->GetName(), *FailureReason);
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
		FString FailureReason;
		if (!ParentActor || !CanMutateGeneratedActorGraph(ParentActor->GetWorld(), ParentActor, &FailureReason))
		{
			if (ParentActor)
			{
				UE_LOG(LogRoadBuilder, Warning, TEXT("RoadBuilder rejected World Partition child synchronization for %s: %s."),
					*ParentActor->GetName(), *FailureReason);
			}
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
