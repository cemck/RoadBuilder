// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;
class UClass;

/**
 * Keeps editor-generated RoadBuilder actors in the same World Partition
 * authoring context as their RoadScene, junction, ground, or road parent.
 */
namespace RoadBuilderWorldPartition
{
	/**
	 * Returns false when an editor/runtime mutation would touch an unloading,
	 * saving, garbage-collecting, undo/redo, or mismatched World Partition
	 * actor graph.  Callers must treat false as a no-op; it is deliberately a
	 * guard, not an attempt to repair partially loaded actor data.
	 */
	ROADBUILDER_API bool CanMutateGeneratedActorGraph(
		UWorld* World,
		const AActor* ParentActor = nullptr,
		FString* OutFailureReason = nullptr);

	/**
	 * World Partition stores every actor in its own package.  An attachment
	 * serializes a hard reference from the child package back to its parent,
	 * which becomes an invalid import if a junction/link is regenerated.  Keep
	 * generated actors detached in that case and let their RoadBuilder owner
	 * retain the relationship instead.
	 */
	ROADBUILDER_API bool UsesDetachedGeneratedActors(const AActor* ParentActor);
	ROADBUILDER_API void ParentGeneratedActor(AActor* Actor, AActor* ParentActor);
	/**
	 * Road, junction, and ground mesh vertices are authored in world space. Keep
	 * their actor transform at identity and lock editor movement so a detached
	 * OFPA actor cannot accidentally offset its generated mesh.
	 */
	ROADBUILDER_API void PrepareGeneratedGeometryActor(AActor* Actor);
	/** Destroy all legacy attached descendants before deleting a generated owner. */
	ROADBUILDER_API void DestroyAttachedGeneratedActors(AActor* ParentActor);

	ROADBUILDER_API AActor* SpawnGeneratedActor(UWorld* World, UClass* ActorClass, const FTransform& Transform, AActor* ParentActor);
	ROADBUILDER_API void SynchronizeGeneratedActor(AActor* Actor, const AActor* ParentActor);
	ROADBUILDER_API void SynchronizeGeneratedChildren(AActor* ParentActor);
}
