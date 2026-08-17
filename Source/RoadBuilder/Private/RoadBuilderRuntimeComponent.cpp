// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#include "RoadBuilderRuntimeComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
bool ArePointsSeparated2D(const FVector& A, const FVector& B)
{
	return FVector2D::Distance(FVector2D(A.X, A.Y), FVector2D(B.X, B.Y)) > KINDA_SMALL_NUMBER;
}
}

URoadBuilderRuntimeComponent::URoadBuilderRuntimeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	RoadSceneClass = ARoadScene::StaticClass();
}

ARoadScene* URoadBuilderRuntimeComponent::ResolveRoadScene(bool bCreateIfMissing)
{
	if (IsValid(RoadScene))
	{
		return RoadScene;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ARoadScene> It(World); It; ++It)
	{
		RoadScene = *It;
		return RoadScene;
	}

	if (!bCreateIfMissing)
	{
		return nullptr;
	}

	UClass* SceneClass = RoadSceneClass ? RoadSceneClass.Get() : ARoadScene::StaticClass();
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = MakeUniqueObjectName(World, SceneClass, TEXT("RuntimeRoadScene"));
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	RoadScene = World->SpawnActor<ARoadScene>(SceneClass, FTransform::Identity, SpawnParams);
	return RoadScene;
}

bool URoadBuilderRuntimeComponent::BeginRoad(const FVector& StartPoint, bool bCreateSceneIfMissing)
{
	CancelRoad(false);

	if (!ResolveRoadScene(bCreateSceneIfMissing))
	{
		return false;
	}

	DraftPoints.Reset();
	DraftPoints.Add(StartPoint);
	bHasPreviewPoint = false;
	PreviewPoint = FVector::ZeroVector;
	return true;
}

bool URoadBuilderRuntimeComponent::BeginRoadFromCursor(APlayerController* PlayerController)
{
	FVector WorldPoint;
	FHitResult HitResult;
	return GetCursorRoadPlacementPoint(PlayerController, WorldPoint, HitResult) && BeginRoad(WorldPoint);
}

bool URoadBuilderRuntimeComponent::SetPreviewPoint(const FVector& WorldPoint)
{
	if (DraftPoints.Num() == 0)
	{
		return BeginRoad(WorldPoint);
	}

	PreviewPoint = WorldPoint;
	bHasPreviewPoint = true;
	return RefreshPreviewRoad(false);
}

bool URoadBuilderRuntimeComponent::SetPreviewPointFromCursor(APlayerController* PlayerController)
{
	FVector WorldPoint;
	FHitResult HitResult;
	return GetCursorRoadPlacementPoint(PlayerController, WorldPoint, HitResult) && SetPreviewPoint(WorldPoint);
}

bool URoadBuilderRuntimeComponent::AppendRoadPoint(const FVector& WorldPoint)
{
	if (DraftPoints.Num() == 0)
	{
		return BeginRoad(WorldPoint);
	}

	if (DraftPoints.Num() >= MaxDraftPoints || !ArePointsSeparated2D(DraftPoints.Last(), WorldPoint))
	{
		return false;
	}

	DraftPoints.Add(WorldPoint);
	bHasPreviewPoint = false;
	return RefreshPreviewRoad(false);
}

bool URoadBuilderRuntimeComponent::AppendRoadPointFromCursor(APlayerController* PlayerController)
{
	FVector WorldPoint;
	FHitResult HitResult;
	return GetCursorRoadPlacementPoint(PlayerController, WorldPoint, HitResult) && AppendRoadPoint(WorldPoint);
}

bool URoadBuilderRuntimeComponent::RemoveLastRoadPoint()
{
	if (DraftPoints.Num() == 0)
	{
		return false;
	}

	DraftPoints.Pop();
	bHasPreviewPoint = false;
	return RefreshPreviewRoad(true);
}

ARoadActor* URoadBuilderRuntimeComponent::CommitRoad()
{
	if (HasUsablePreviewPoint())
	{
		DraftPoints.Add(PreviewPoint);
		bHasPreviewPoint = false;
	}

	if (DraftPoints.Num() < 2 || !RefreshPreviewRoad(true))
	{
		return nullptr;
	}

	ARoadActor* CommittedRoad = PreviewRoad;
	PreviewRoad = nullptr;
	DraftPoints.Reset();
	bHasPreviewPoint = false;
	PreviewPoint = FVector::ZeroVector;
	return CommittedRoad;
}

void URoadBuilderRuntimeComponent::CancelRoad(bool bRebuildScene)
{
	if (IsValid(PreviewRoad))
	{
		if (ARoadScene* Scene = ResolveRoadScene(false))
		{
			Scene->RuntimeDestroyRoad(PreviewRoad, bRebuildScene);
		}
		else
		{
			PreviewRoad->Destroy();
		}
	}

	PreviewRoad = nullptr;
	DraftPoints.Reset();
	bHasPreviewPoint = false;
	PreviewPoint = FVector::ZeroVector;
}

bool URoadBuilderRuntimeComponent::RefreshPreviewRoad(bool bForceRebuild)
{
	TArray<FVector> RoadPoints;
	if (!BuildRoadPointList(RoadPoints, true))
	{
		if (IsValid(PreviewRoad))
		{
			if (ARoadScene* Scene = ResolveRoadScene(false))
			{
				Scene->RuntimeDestroyRoad(PreviewRoad, bForceRebuild || bLivePreview);
			}
			else
			{
				PreviewRoad->Destroy();
			}
			PreviewRoad = nullptr;
		}
		return true;
	}

	ARoadScene* Scene = ResolveRoadScene(true);
	if (!Scene)
	{
		return false;
	}

	if (!IsValid(PreviewRoad))
	{
		PreviewRoad = Scene->RuntimeCreateRoad(RoadPoints, RoadStyle, false);
	}
	else if (!PreviewRoad->RuntimeSetRoadPoints(RoadPoints, false))
	{
		return false;
	}

	if (!PreviewRoad)
	{
		return false;
	}

	if (bForceRebuild || bLivePreview)
	{
		Scene->RuntimeRebuild();
	}

	return true;
}

bool URoadBuilderRuntimeComponent::GetCursorRoadPlacementPoint(APlayerController* PlayerController, FVector& OutWorldPoint, FHitResult& OutHitResult) const
{
	APlayerController* ResolvedPlayerController = ResolvePlayerController(PlayerController);
	if (!ResolvedPlayerController)
	{
		return false;
	}

	float ScreenX = 0.0f;
	float ScreenY = 0.0f;
	if (!ResolvedPlayerController->GetMousePosition(ScreenX, ScreenY))
	{
		return false;
	}

	return GetScreenRoadPlacementPoint(ResolvedPlayerController, FVector2D(ScreenX, ScreenY), OutWorldPoint, OutHitResult);
}

bool URoadBuilderRuntimeComponent::GetScreenRoadPlacementPoint(APlayerController* PlayerController, const FVector2D& ScreenPosition, FVector& OutWorldPoint, FHitResult& OutHitResult) const
{
	APlayerController* ResolvedPlayerController = ResolvePlayerController(PlayerController);
	if (!ResolvedPlayerController)
	{
		return false;
	}

	FVector RayOrigin;
	FVector RayDirection;
	if (!ResolvedPlayerController->DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, RayOrigin, RayDirection))
	{
		return false;
	}

	return GetRayRoadPlacementPoint(RayOrigin, RayDirection, OutWorldPoint, OutHitResult);
}

FVector URoadBuilderRuntimeComponent::SnapRoadPlacementPoint(const FVector& WorldPoint) const
{
	if (PlacementGridSize <= KINDA_SMALL_NUMBER)
	{
		return WorldPoint;
	}

	FVector SnappedPoint = WorldPoint;
	SnappedPoint.X = FMath::GridSnap(SnappedPoint.X, PlacementGridSize);
	SnappedPoint.Y = FMath::GridSnap(SnappedPoint.Y, PlacementGridSize);
	if (bSnapPlacementZ)
	{
		SnappedPoint.Z = FMath::GridSnap(SnappedPoint.Z, PlacementGridSize);
	}
	return SnappedPoint;
}

TArray<FVector> URoadBuilderRuntimeComponent::GetDraftPoints(bool bIncludePreviewPoint) const
{
	TArray<FVector> Points;
	BuildRoadPointList(Points, bIncludePreviewPoint);
	return Points;
}

APlayerController* URoadBuilderRuntimeComponent::ResolvePlayerController(APlayerController* PlayerController) const
{
	if (PlayerController)
	{
		return PlayerController;
	}

	if (APlayerController* OwnerController = Cast<APlayerController>(GetOwner()))
	{
		return OwnerController;
	}

	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(OwnerPawn->GetController());
	}

	return nullptr;
}

bool URoadBuilderRuntimeComponent::GetRayRoadPlacementPoint(const FVector& RayOrigin, const FVector& RayDirection, FVector& OutWorldPoint, FHitResult& OutHitResult) const
{
	OutHitResult = FHitResult();
	const FVector Direction = RayDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	const double TraceDistance = FMath::Max(PlacementTraceDistance, 1.0);
	if (UWorld* World = GetWorld())
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RoadBuilderRuntimePlacement), true);
		if (AActor* Owner = GetOwner())
		{
			QueryParams.AddIgnoredActor(Owner);
		}
		if (IsValid(PreviewRoad))
		{
			QueryParams.AddIgnoredActor(PreviewRoad);
		}

		const FVector TraceEnd = RayOrigin + Direction * TraceDistance;
		if (World->LineTraceSingleByChannel(OutHitResult, RayOrigin, TraceEnd, PlacementTraceChannel.GetValue(), QueryParams))
		{
			OutWorldPoint = SnapRoadPlacementPoint(OutHitResult.ImpactPoint);
			return true;
		}
	}

	if (!bFallbackToPlacementPlane || FMath::IsNearlyZero(Direction.Z))
	{
		return false;
	}

	const double PlaneT = (PlacementPlaneZ - RayOrigin.Z) / Direction.Z;
	if (PlaneT < 0.0 || PlaneT > TraceDistance)
	{
		return false;
	}

	OutWorldPoint = SnapRoadPlacementPoint(RayOrigin + Direction * PlaneT);
	return true;
}

bool URoadBuilderRuntimeComponent::BuildRoadPointList(TArray<FVector>& OutPoints, bool bIncludePreviewPoint) const
{
	OutPoints = DraftPoints;
	if (bIncludePreviewPoint && HasUsablePreviewPoint())
	{
		OutPoints.Add(PreviewPoint);
	}
	return OutPoints.Num() >= 2;
}

bool URoadBuilderRuntimeComponent::HasUsablePreviewPoint() const
{
	return bHasPreviewPoint && DraftPoints.Num() > 0 && ArePointsSeparated2D(DraftPoints.Last(), PreviewPoint);
}
