// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "RoadScene.h"
#include "RoadBuilderRuntimeComponent.generated.h"

class APlayerController;

UCLASS(BlueprintType, Blueprintable, ClassGroup = (RoadBuilder), meta = (BlueprintSpawnableComponent))
class ROADBUILDER_API URoadBuilderRuntimeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URoadBuilderRuntimeComponent();

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement")
	ARoadScene* ResolveRoadScene(bool bCreateIfMissing = true);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement")
	bool BeginRoad(const FVector& StartPoint, bool bCreateSceneIfMissing = true);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement")
	bool BeginRoadFromCursor(APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement")
	bool SetPreviewPoint(const FVector& WorldPoint);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement")
	bool SetPreviewPointFromCursor(APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement")
	bool AppendRoadPoint(const FVector& WorldPoint);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement")
	bool AppendRoadPointFromCursor(APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement")
	bool RemoveLastRoadPoint();

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement")
	ARoadActor* CommitRoad();

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement")
	void CancelRoad(bool bRebuildScene = true);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement")
	bool RefreshPreviewRoad(bool bForceRebuild = false);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement|Input")
	bool GetCursorRoadPlacementPoint(APlayerController* PlayerController, FVector& OutWorldPoint, FHitResult& OutHitResult) const;

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime Placement|Input")
	bool GetScreenRoadPlacementPoint(APlayerController* PlayerController, const FVector2D& ScreenPosition, FVector& OutWorldPoint, FHitResult& OutHitResult) const;

	UFUNCTION(BlueprintPure, Category = "RoadBuilder|Runtime Placement|Input")
	FVector SnapRoadPlacementPoint(const FVector& WorldPoint) const;

	UFUNCTION(BlueprintPure, Category = "RoadBuilder|Runtime Placement")
	bool IsBuildingRoad() const { return DraftPoints.Num() > 0 || IsValid(PreviewRoad); }

	UFUNCTION(BlueprintPure, Category = "RoadBuilder|Runtime Placement")
	TArray<FVector> GetDraftPoints(bool bIncludePreviewPoint = true) const;

	UFUNCTION(BlueprintPure, Category = "RoadBuilder|Runtime Placement")
	ARoadActor* GetPreviewRoad() const { return PreviewRoad; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoadBuilder|Runtime Placement")
	ARoadScene* RoadScene = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoadBuilder|Runtime Placement")
	TSubclassOf<ARoadScene> RoadSceneClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoadBuilder|Runtime Placement")
	URoadStyle* RoadStyle = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoadBuilder|Runtime Placement")
	bool bLivePreview = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoadBuilder|Runtime Placement", meta = (ClampMin = 2))
	int32 MaxDraftPoints = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoadBuilder|Runtime Placement|Input", meta = (ClampMin = 0))
	double PlacementGridSize = 100.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoadBuilder|Runtime Placement|Input")
	bool bSnapPlacementZ = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoadBuilder|Runtime Placement|Input")
	double PlacementPlaneZ = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoadBuilder|Runtime Placement|Input")
	bool bFallbackToPlacementPlane = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoadBuilder|Runtime Placement|Input", meta = (ClampMin = 1))
	double PlacementTraceDistance = 1000000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoadBuilder|Runtime Placement|Input")
	TEnumAsByte<ECollisionChannel> PlacementTraceChannel = ECC_Visibility;

private:
	APlayerController* ResolvePlayerController(APlayerController* PlayerController) const;
	bool GetRayRoadPlacementPoint(const FVector& RayOrigin, const FVector& RayDirection, FVector& OutWorldPoint, FHitResult& OutHitResult) const;
	bool BuildRoadPointList(TArray<FVector>& OutPoints, bool bIncludePreviewPoint) const;
	bool HasUsablePreviewPoint() const;

	UPROPERTY()
	ARoadActor* PreviewRoad = nullptr;

	UPROPERTY()
	TArray<FVector> DraftPoints;

	UPROPERTY()
	FVector PreviewPoint = FVector::ZeroVector;

	UPROPERTY()
	bool bHasPreviewPoint = false;
};
