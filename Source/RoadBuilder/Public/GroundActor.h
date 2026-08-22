// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "RoadActor.h"
#include "GroundActor.generated.h"

USTRUCT()
struct ROADBUILDER_API FGroundPoint
{
	GENERATED_USTRUCT_BODY()
	bool SegmentStart()
	{
		return (Index % 2) ^ (!Side);
	}
	bool SegmentEnd()
	{
		return (Index % 2) ^ Side;
	}
	double GetDist(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots);
	FVector GetPos(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots);
	FVector GetDir(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots);
	FGroundPoint PrevPoint(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots);
	FGroundPoint NextPoint(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots);
	bool operator == (const FGroundPoint& Other)const
	{
		return Road == Other.Road && Side == Other.Side && Index == Other.Index;
	}
	friend uint32 GetTypeHash(const FGroundPoint& Point)
	{
		return HashCombineFast(GetTypeHash(Point.Road), GetTypeHash(Point.Side << 16 | Point.Index));
	}
	UPROPERTY()
	ARoadActor* Road = nullptr;

	UPROPERTY()
	int Side = 0;

	UPROPERTY()
	int Index = INDEX_NONE;
};

UCLASS()
class ROADBUILDER_API AGroundActor : public AActor
{
	GENERATED_UCLASS_BODY()
public:
	void AddManualPoint(const FVector& Pos, int& Index);
	void Join(AGroundActor* Other, int& Index);
	void BuildMesh(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots);
	bool Contains(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots, const TArray<FGroundPoint>& OtherPoints);
	void SetOwningScene(ARoadScene* Scene);
	void RegisterGeneratedChild(AActor* Actor);
	void ClearGeneratedChildren();
	void SynchronizeWorldPartitionChildren();
	ARoadScene* GetScene();
	TArray<FVector> GetVertices(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots);
	virtual void Destroyed() override;
	
	UFUNCTION(CallInEditor, Category = Ground)
	void CreatePCGSpline();
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif
	bool IsEndPoint(int Index)
	{
		return Index == 0 || Index == Points.Num() - 1;
	}
	bool IsExpired()
	{
		return Tags.Contains(TEXT("Expired"));
	}
	void MarkExpired()
	{
		Tags.Add(TEXT("Expired"));
	}
	void Renew()
	{
		Tags.Remove(TEXT("Expired"));
	}
	UPROPERTY(EditAnywhere, Category = Ground)
	UMaterialInterface* Material;

	UPROPERTY()
	TArray<FVector> ManualPoints;

	UPROPERTY()
	TArray<FGroundPoint> Points;

	UPROPERTY()
	bool bClosedLoop = false;

	UPROPERTY()
	TSoftObjectPtr<ARoadScene> OwningScene;

	UPROPERTY()
	TArray<TSoftObjectPtr<AActor>> GeneratedChildren;
};
