// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Math/GenericOctree.h"
#include "RoadBuilderSettings.h"
#include "TrafficControl.h"
#include "GroundActor.h"
#include "RoadScene.generated.h"

#define DefaultJunctionExtent	800.0

struct FRoadOctreeElement
{
	FRoadOctreeElement(URoadBoundary* B, int I) :Boundary(B), Index(I) {}
	FBox GetBounds() const
	{
		return Boundary->Curve.GetSegmentBounds(Index);
	}
	bool operator == (const FRoadOctreeElement& Other) const
	{
		return Boundary == Other.Boundary && Index == Other.Index;
	}
	bool IsBase() const { return Boundary->GetRoad()->BaseCurve == Boundary; }
	bool IsBoundary() const { return Boundary->GetRoad()->BaseCurve != Boundary; }
	bool Adjacent(const FRoadOctreeElement& Other) const
	{
		return Boundary == Other.Boundary && FMath::Abs(Index - Other.Index) <= 1;
	}
	bool Adjacent(URoadBoundary* OtherBoundary, int OtherIndex) const
	{
		return Boundary == OtherBoundary && FMath::Abs(Index - OtherIndex) <= 1;
	}
	int GetSide() { return !Boundary->LeftLane ? 1 : (!Boundary->RightLane ? 0 : -1); }
	URoadBoundary* Boundary;
	int Index;
};

struct FRoadOctreeSemantics
{
	enum { MaxElementsPerLeaf = 16 };
	enum { MinInclusiveElementsPerNode = 7 };
	enum { MaxNodeDepth = 12 };

	typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

	FORCEINLINE static FBoxCenterAndExtent GetBoundingBox(const FRoadOctreeElement& Element)
	{
		return FBoxCenterAndExtent(Element.GetBounds());
	}

	FORCEINLINE static bool AreElementsEqual(const FRoadOctreeElement& A, const FRoadOctreeElement& B)
	{
		return A == B;
	}
	static void SetElementId(const FRoadOctreeElement& Element, FOctreeElementId2 Id)
	{
		Element.Boundary->OctreeIds.SetNum(Element.Boundary->Curve.Points.Num() - 1);
		Element.Boundary->OctreeIds[Element.Index] = Id;
	}
};

USTRUCT()
struct FJunctionLink
{
	GENERATED_USTRUCT_BODY()
	void CreateRoad(AJunctionActor* Parent);
	void Destroy();
	UPROPERTY()
	ARoadActor* Road = nullptr;

	UPROPERTY()
	ARoadActor* InputRoad = nullptr;

	UPROPERTY()
	ARoadActor* OutputRoad = nullptr;

	UPROPERTY(EditAnywhere, Category = Link)
	double Radius = 1000;
};

USTRUCT()
struct FTurnRestriction
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Restriction)
	int32 FromGateIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, Category = Restriction)
	int32 ToGateIndex = INDEX_NONE;
};

USTRUCT()
struct FJunctionGate
{
	GENERATED_USTRUCT_BODY()
	bool operator < (const FJunctionGate& Other) const
	{
		return Radian > Other.Radian;
	}
	bool Contains(double D)
	{
		return (Sign < 0 && D >= Dist && D <= FMath::Max(CornerDists[0], CornerDists[1])) || (Sign > 0 && D <= Dist && D >= FMath::Min(CornerDists[0], CornerDists[1]));
	}
	void Clear()
	{
		for (FJunctionLink& Link : Links)
			Link.Destroy();
		Links.Empty();
	}
	bool IsRampOf(FJunctionGate& Other)
	{
#if 0
		FVector Dir = Road->GetDir(InitDist) * Sign;
		FVector NextDir = Other.Road->GetDir(Other.InitDist) * Other.Sign;
		if ((Dir|NextDir) < -0.996194698)
#else
		if (FMath::Abs(WrapRadian(Other.Radian - Radian)) > DOUBLE_HALF_PI)
#endif
		{
			int SrcIndex = Sign > 0 ? 0 : 1;
			if (Road->ConnectedParents[SrcIndex] == Other.Road)
			{
				int DstIndex = Other.Sign > 0 ? 0 : 1;
				if (Other.Road->ConnectedParents[DstIndex] == Road)
					return Road->Lanes.Num() < Other.Road->Lanes.Num();
				return true;
			}
		}
		return false;
	}
	bool IsConnected(FJunctionGate& Other)
	{
		int SrcIndex = Sign > 0 ? 0 : 1;
		int DstIndex = Other.Sign > 0 ? 0 : 1;
		return Road->ConnectedParents[SrcIndex] == Other.Road && Road == Other.Road->ConnectedParents[DstIndex];
	}
	FVector2D GetCross(int Side)
	{
		return Road->GetPos(CornerDists[Side]);
	}
	bool IsExpired() { return Radian > HALF_WORLD_MAX; }
	bool IsInput() { return Sign < 0; }
	bool IsOutput() { return Sign > 0; }
	void MarkExpired()
	{
		Dist = InitDist;
		Radian = WORLD_MAX;
	}
	void Renew(double D, double S);

	UPROPERTY()
	ARoadActor* Road = nullptr;

	UPROPERTY()
	double Radian = 0;

	UPROPERTY()
	double Sign = 0;

	UPROPERTY()
	double InitDist = 0;

	UPROPERTY()
	double Dist = 0;

	UPROPERTY()
	double CornerDists[2] = { 0, 0 };

	UPROPERTY()
	double CutDists[2] = { 0, 0 };

	UPROPERTY()
	TArray<FJunctionLink> Links;
};

/** A boundary segment expressed relative to its junction-link length. */
USTRUCT()
struct FJunctionBoundarySegmentOverride
{
	GENERATED_USTRUCT_BODY()

	/** [0, 1] along the generated junction connector. */
	UPROPERTY()
	double NormalizedDist = 0.0;

	UPROPERTY()
	ULaneMarkStyle* LaneMarking = nullptr;

	UPROPERTY()
	URoadProps* Props = nullptr;
};

/** Marking/prop styling for one generated connector boundary. */
USTRUCT()
struct FJunctionBoundaryOverride
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	int32 BoundaryIndex = INDEX_NONE;

	UPROPERTY()
	TArray<FJunctionBoundarySegmentOverride> Segments;
};

/** An authored point marking stored in connector-relative coordinates. */
USTRUCT()
struct FJunctionPointMarkingOverride
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	UStaticMesh* Mesh = nullptr;

	UPROPERTY()
	double NormalizedDistance = 0.0;

	UPROPERTY()
	double LateralOffset = 0.0;
};

/** An authored curve control point stored in connector-relative coordinates. */
USTRUCT()
struct FJunctionMarkingCurvePointOverride
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	double NormalizedDistance = 0.0;

	UPROPERTY()
	double LateralOffset = 0.0;

	UPROPERTY()
	double NormalizedInDistance = 0.0;

	UPROPERTY()
	double InLateralOffset = 0.0;

	UPROPERTY()
	double NormalizedOutDistance = 0.0;

	UPROPERTY()
	double OutLateralOffset = 0.0;
};

/** A user-authored marking attached to a junction connector. */
USTRUCT()
struct FJunctionAuthoredMarkingOverride
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	bool bIsPointMarking = false;

	UPROPERTY()
	FJunctionPointMarkingOverride Point;

	UPROPERTY()
	UBaseMarkStyle* MarkStyle = nullptr;

	UPROPERTY()
	UPolygonMarkStyle* FillStyle = nullptr;

	UPROPERTY()
	double Orientation = 0.0;

	UPROPERTY()
	bool bClosedLoop = false;

	UPROPERTY()
	TArray<FJunctionMarkingCurvePointOverride> CurvePoints;
};

/** Style-only record for a generated gore polygon. Its geometry still regenerates. */
USTRUCT()
struct FJunctionGeneratedMarkingOverride
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	UPolygonMarkStyle* FillStyle = nullptr;

	UPROPERTY()
	double Orientation = 0.0;
};

/**
 * Durable authored state for one directed junction link.  It is keyed by the
 * two source gates rather than by a generated actor, because connector actors
 * are intentionally destroyed and recreated when junction topology changes.
 */
USTRUCT()
struct FJunctionLinkOverride
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	ARoadActor* InputRoad = nullptr;

	UPROPERTY()
	ARoadActor* OutputRoad = nullptr;

	UPROPERTY()
	double InputGateInitDist = 0.0;

	UPROPERTY()
	double OutputGateInitDist = 0.0;

	UPROPERTY()
	int32 LinkIndex = INDEX_NONE;

	UPROPERTY()
	double Radius = 1000.0;

	UPROPERTY()
	TArray<FJunctionBoundaryOverride> Boundaries;

	UPROPERTY()
	TArray<FJunctionAuthoredMarkingOverride> AuthoredMarkings;

	UPROPERTY()
	TArray<FJunctionGeneratedMarkingOverride> GeneratedMarkings;
};

struct FJunctionSlot
{
	FJunctionGate& InputGate() const;
	FJunctionGate& OutputGate() const;
	int InputGateIndex() const;
	int OutputGateIndex() const;
	bool HasInput() const { return InputGateIndex() != INDEX_NONE; }
	bool HasOutput() const { return OutputGateIndex() != INDEX_NONE; }
	double InputDist() const;
	double OutputDist() const;
	double CrossDist() const;
	void Combine(FJunctionSlot& Other);
	bool IsValid() const
	{
		return Junction && Road;
	}
	bool operator < (const FJunctionSlot& Other) const
	{
		return CrossDist() < Other.CrossDist();
	}
	AJunctionActor* Junction;
	ARoadActor* Road;
	double InitInputDist = -MAX_dbl;
	double InitOutputDist = MAX_dbl;
};

enum class EGoreDiagnosticState : uint8
{
	Blocked,
	ReadyExactIntersection,
	ReadyNoseGapFallback,
};

/** Transient editor feedback captured by the actual gore generation path. */
struct ROADBUILDER_API FGoreDiagnostic
{
	static constexpr int32 RequirementCount = 10;

	bool IsReady() const { return State != EGoreDiagnosticState::Blocked; }

	int32 GateIndex = INDEX_NONE;
	int32 NextGateIndex = INDEX_NONE;
	int32 RequirementsMet = 0;
	int32 PolygonPointCount = 0;
	EGoreDiagnosticState State = EGoreDiagnosticState::Blocked;
	FString Status;
	FVector LabelLocation = FVector::ZeroVector;
	FVector SourceNose = FVector::ZeroVector;
	FVector DestinationNose = FVector::ZeroVector;
	FVector Intersection = FVector::ZeroVector;
	bool bHasNosePair = false;
	bool bHasIntersection = false;
	FPolyline SourceBoundary;
	FPolyline DestinationBoundary;
	FPolyline CornerBoundary;
	TWeakObjectPtr<UMarkingCurve> Marking;
};

UCLASS()
class ROADBUILDER_API AJunctionActor : public AActor
{
	GENERATED_UCLASS_BODY()
public:
	static const int CornerIndex = 1;
	void AddRoad(ARoadActor* Road, double Dist);
	void Build(bool bRegenerateDerivedMarkings = true);
	void BuildGoreMarkings();
	void BuildLink(FJunctionGate& Gate, FJunctionGate& Next, int Index);
	bool Contains(ARoadActor* Road, double Dist);
	void FixHeight(FPolyline& Polyline);
	void FixHeight(TArray<FVector>& Points);
	void Join(AJunctionActor* Junction);
	void Update(TOctree2<FRoadOctreeElement, FRoadOctreeSemantics>& Octree);
	void UpdateCorner(FJunctionGate& SrcGate, double SrcDist, FJunctionGate& DstGate, double DstDist);
	FJunctionGate& AddGate(ARoadActor* Road, double Dist, double Sign);
	int GetGate(const FVector& Pos);
	int GetRampConnection(FJunctionGate& Gate);
	FJunctionSlot GetSlot(ARoadActor* Road, double Dist);
	TArray<FJunctionSlot> GetSlots(ARoadActor* Road);
	bool IsTurnAllowed(int FromGate, int ToGate) const;
	void AddTurnRestriction(int FromGate, int ToGate);
	void RemoveTurnRestriction(int FromGate, int ToGate);
	void SetOwningScene(ARoadScene* Scene);
	void RegisterGeneratedChild(AActor* Actor);
	void ClearGeneratedChildren();
	void SynchronizeWorldPartitionChildren();
	ARoadScene* GetScene();
	void ExportXodr(FXmlNode* XmlNode, int& RoadId, int& ObjectId);
	virtual void Destroyed();
	
	UPROPERTY()
	TArray<FJunctionGate> Gates;

	UPROPERTY(EditAnywhere, Category = Junction)
	TArray<FTurnRestriction> TurnRestrictions;

	UPROPERTY(EditAnywhere, Category = "Traffic Control")
	ETrafficControlType TrafficControlType = ETrafficControlType::None;

	UPROPERTY()
	TArray<ATrafficLightActor*> TrafficLights;

	UPROPERTY()
	TArray<ATrafficSignActor*> TrafficSigns;

	UPROPERTY()
	TArray<ATurnArrowActor*> TurnArrows;

	void GenerateTrafficControl();
	void CleanupTrafficControl();
	void GenerateTurnArrows();
	void CleanupTurnArrows();
	/** Snapshot styling before a rebuild may replace generated link actors. */
	void CaptureLinkOverrides();
	/** Restore styling after regenerated link geometry and gore polygons exist. */
	void ApplyLinkOverrides();
	/** Preserve compatible link overrides when two generated junctions merge. */
	void AbsorbLinkOverrides(const AJunctionActor* Other);

	TArray<FVector> DebugPoints;
	TArray<FPolyline> DebugCurves;
	TArray<FGoreDiagnostic> GoreDiagnostics;
	/** Source triangles emitted by the most recent accepted junction build. */
	int32 LastBuildTriangleCount = 0;

	/** Saved automatically from junction-link edits; not editable as a preset. */
	UPROPERTY()
	TArray<FJunctionLinkOverride> PersistentLinkOverrides;

	/** Soft ownership prevents this OFPA junction package from importing the RoadScene. */
	UPROPERTY()
	TSoftObjectPtr<ARoadScene> OwningScene;

	/** Detached World Partition children still need an explicit deletion path. */
	UPROPERTY()
	TArray<TSoftObjectPtr<AActor>> GeneratedChildren;
};

UCLASS(BlueprintType, Blueprintable)
class ROADBUILDER_API ARoadScene : public AActor
{
	GENERATED_UCLASS_BODY()
public:
	UFUNCTION(BlueprintPure, Category = "RoadBuilder|Traffic")
	ERoadTrafficHandedness GetResolvedTrafficHandedness() const;

	UFUNCTION(BlueprintPure, Category = "RoadBuilder|Traffic")
	bool IsLeftHandTraffic() const { return GetResolvedTrafficHandedness() == ERoadTrafficHandedness::LeftHandTraffic; }

	UFUNCTION(BlueprintPure, Category = "RoadBuilder|Traffic")
	bool IsTrafficHandednessApplied() const;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "RoadBuilder|Traffic", meta = (DisplayName = "Apply Traffic Handedness"))
	void ApplyTrafficHandedness();

	int32 GetForwardTrafficSide() const;
	int32 GetReverseTrafficSide() const;
	int32 GetTrafficSideForDirection(double DirectionSign) const;
	double GetTrafficDirectionSignForSide(int32 Side) const;
	FString GetOpenDriveTrafficRule() const;

	static int32 ResolveTrafficSide(ERoadTrafficHandedness Handedness, double DirectionSign);
	static double ResolveTrafficDirectionSign(ERoadTrafficHandedness Handedness, int32 Side);
	static ERoadTrafficHandedness ResolveTrafficHandedness(
		bool bUseSceneOverride,
		ERoadTrafficHandedness SceneHandedness,
		ERoadTrafficHandedness ProjectHandedness);
	static FString ToOpenDriveTrafficRule(ERoadTrafficHandedness Handedness);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime")
	ARoadActor* RuntimeCreateRoad(const TArray<FVector>& WorldPoints, URoadStyle* Style = nullptr, bool bRebuildScene = true);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime")
	bool RuntimeDestroyRoad(ARoadActor* Road, bool bRebuildScene = true);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime")
	void RuntimeRebuild();

	UFUNCTION(BlueprintPure, Category = "RoadBuilder|Runtime")
	TArray<ARoadActor*> RuntimeGetRoads() const { return Roads; }

	ARoadActor* AddRoad();
	ARoadActor* AddRoad(URoadStyle* Style, double Height);
	ARoadActor* DuplicateRoad(ARoadActor* Source);
	ARoadActor* PickRoad(const FVector& Pos, ARoadActor* IgnoredRoad = nullptr);
	AGroundActor* AddGround(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots, const TArray<FGroundPoint>& Points);
	AJunctionActor* AddJunction(ARoadActor* R0, double D0, ARoadActor* R1, double D1);
	TMap<ARoadActor*, TArray<FJunctionSlot>> GetAllJunctionSlots();
	TArray<FJunctionSlot> GetJunctionSlots(ARoadActor* Road);
//	FVector2D GetRoadUV(ARoadActor* Road, const FVector& Pos);
	void Rebuild();
	/**
	 * Refresh elevation-dependent road and junction meshes without resolving
	 * junction topology or recreating generated/authored junction markings.
	 */
	void RebuildHeightOnly(ARoadActor* ChangedRoad);
	void RemoveInvalidReferences();
	void GenerateGrounds(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots);
	void GenerateMassGraph(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots);
	void CleanupMassGraph();
	void OctreeAddBoundary(URoadBoundary* Boundary);
	void OctreeRemoveBoundary(URoadBoundary* Boundary);
	void OctreeAddRoad(ARoadActor* Road);
	void OctreeRemoveRoad(ARoadActor* Road);
	void DestroyRoad(ARoadActor* Road);
	void ResetTrafficDerivedData();
	/** Make all generated descendants use this RoadScene's WP spatial/data-layer state. */
	void SynchronizeWorldPartitionChildren();
	virtual void PostLoad() override;
#if WITH_EDITOR
	void ExportXodr();
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(BlueprintReadOnly, Category = "RoadBuilder|Runtime")
	TArray<ARoadActor*> Roads;

	UPROPERTY(BlueprintReadOnly, Category = "RoadBuilder|Runtime")
	TArray<AJunctionActor*> Junctions;

	UPROPERTY(BlueprintReadOnly, Category = "RoadBuilder|Runtime")
	TArray<AGroundActor*> Grounds;

	UPROPERTY(BlueprintReadOnly, Category = "RoadBuilder|MassGraph")
	TArray<AActor*> MassGraphActors;

	UPROPERTY(EditAnywhere, Category = "RoadBuilder|Traffic", meta = (DisplayName = "Override Project Traffic Handedness"))
	bool bOverrideTrafficHandedness = false;

	UPROPERTY(EditAnywhere, Category = "RoadBuilder|Traffic", meta = (EditCondition = "bOverrideTrafficHandedness"))
	ERoadTrafficHandedness TrafficHandedness = ERoadTrafficHandedness::RightHandTraffic;

	UPROPERTY()
	bool bTrafficHandednessInitialized = false;

	UPROPERTY()
	ERoadTrafficHandedness LastBuiltTrafficHandedness = ERoadTrafficHandedness::RightHandTraffic;

	/** Prevents nested property callbacks from rebuilding a partially regenerated graph. */
	UPROPERTY(Transient)
	bool bIsRebuilding = false;

	TOctree2<FRoadOctreeElement, FRoadOctreeSemantics> Octree;
};
