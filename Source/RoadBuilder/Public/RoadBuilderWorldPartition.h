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
	ROADBUILDER_API AActor* SpawnGeneratedActor(UWorld* World, UClass* ActorClass, const FTransform& Transform, AActor* ParentActor);
	ROADBUILDER_API void SynchronizeGeneratedActor(AActor* Actor, const AActor* ParentActor);
	ROADBUILDER_API void SynchronizeGeneratedChildren(AActor* ParentActor);
}
