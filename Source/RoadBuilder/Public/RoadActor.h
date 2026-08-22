// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "RoadCurve.h"
#include "RoadBoundary.h"
#include "RoadLane.h"
#include "RoadMarking.h"
#include "LaneShape.h"
#include "Components/StaticMeshComponent.h"
#include "RoadActor.generated.h"

#define RD_RIGHT	0
#define RD_LEFT		1

struct FJunctionSlot;
class AJunctionActor;
class ARoadActor;
class ARoadScene;

USTRUCT()
struct FRoadLaneStyle
{
	GENERATED_USTRUCT_BODY()
	UPROPERTY(EditAnywhere, Category = Style)
	double Width = 0;
	
	UPROPERTY(EditAnywhere, Category = Style)
	ELaneType LaneType = ELaneType::None;

	UPROPERTY(EditAnywhere, Category = Style)
	ULaneShape* LaneShape = nullptr;

	UPROPERTY(EditAnywhere, Category = Style)
	ULaneMarkStyle* LaneMarking = nullptr;

	UPROPERTY(EditAnywhere, Category = Style)
	URoadProps* Props = nullptr;
};

UCLASS(BlueprintType)
class ROADBUILDER_API URoadStyle : public UObject
{
	GENERATED_BODY()
public:
	static URoadStyle* Create(URoadBoundary* SrcBoundary, int SrcSide, URoadBoundary* DstBoundary, int DstSide, bool SkipSidewalks, bool KeepLeftLanes, uint32 LeftLaneMarkingMask, uint32 RightLaneMarkingMask);
	int NumDrivingLanes()
	{
		int Count = 0;
		for (FRoadLaneStyle& Lane : LeftLanes)
			if (Lane.LaneType == ELaneType::Driving)
				Count++;
		for (FRoadLaneStyle& Lane : RightLanes)
			if (Lane.LaneType == ELaneType::Driving)
				Count++;
		return Count;
	}

	UPROPERTY(EditAnywhere, Category = Style)
	TArray<FRoadLaneStyle> LeftLanes;

	UPROPERTY(EditAnywhere, Category = Style)
	TArray<FRoadLaneStyle> RightLanes;

	UPROPERTY(EditAnywhere, Category = Style)
	ULaneMarkStyle* BaseCurveMark = nullptr;

	UPROPERTY(EditAnywhere, Category = Style)
	URoadProps* BaseCurveProps = nullptr;

	UPROPERTY(EditAnywhere, Category = Style)
	bool bHasGround = false;
};

USTRUCT()
struct FRoadPoint
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Point)
	FVector2D Pos = FVector2D::ZeroVector;

	UPROPERTY()
	double Dist = 0;

	UPROPERTY(EditAnywhere, Category = Point)
	double MaxRadius = 50000;

	UPROPERTY(EditAnywhere, Category = Point)
	double CurvatureBlend = 0;
};

USTRUCT()
struct FRoadSegment
{
	GENERATED_USTRUCT_BODY()
	void ExportXodr(FXmlNode* PlanViewNode);
	double GetG() { return (EndCurv - StartCurv) / Length; }
	double GetC(double S) { return StartCurv + GetG() * S; }
	double GetR(double S) { return GetSpiralRadian(StartRadian, StartCurv, GetG(), S); }
	double GetDiff() { return WrapRadian(GetR(Length) - StartRadian); }
	FVector2D GetPos(double S) { return GetSpiralPos(StartPos.X, StartPos.Y, StartRadian, StartCurv, GetG(), S); }
	FVector2D GetDir(double S)
	{
		double R = GetR(S);
		return FVector2D(FMath::Cos(R), FMath::Sin(R));
	}
	FRoadSegment Reverse();
	FRoadSegment ApplyOffset(double Offset);

	UPROPERTY(EditAnywhere, Category = Segment)
	double Dist = 0;

	UPROPERTY(EditAnywhere, Category = Segment)
	double Length = 0;

	UPROPERTY(EditAnywhere, Category = Segment)
	FVector2D StartPos = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Segment)
	double StartRadian = 0;

	UPROPERTY(EditAnywhere, Category = Segment)
	double StartCurv = 0;

	UPROPERTY(EditAnywhere, Category = Segment)
	double EndCurv = 0;
};

USTRUCT()
struct FHeightPoint
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Point)
	double Dist = 0;

	UPROPERTY(EditAnywhere, Category = Point)
	double Height = 0;

	UPROPERTY(EditAnywhere, Category = Point)
	double Range = 3000;
};

USTRUCT()
struct FHeightSegment
{
	GENERATED_USTRUCT_BODY()
	void ExportXodr(FXmlNode* ElevationProfileNode);
	double Get(double S)
	{
		double EndHeight = (this + 1)->Height;
		double EndDir = (this + 1)->Dir;
		double Length = (this + 1)->Dist - Dist;
		return FMath::CubicInterp(Height, Dir * Length, EndHeight, EndDir * Length, S / Length);
	}
	FHeightSegment Reverse();

	UPROPERTY(EditAnywhere, Category = Segment)
	double Dist = 0;

	UPROPERTY(EditAnywhere, Category = Segment)
	double Height = 0;

	UPROPERTY(EditAnywhere, Category = Segment)
	double Dir = 0;
};

USTRUCT()
struct FConnectInfo
{
	GENERATED_USTRUCT_BODY()
	FVector2D GetPos();
	void UpdateChild(ARoadActor* Parent);
	double ConnectionSign(ARoadActor* Parent);

	UPROPERTY()
	ARoadActor* Child = nullptr;

	UPROPERTY()
	int Index = INDEX_NONE;

	UPROPERTY()
	FVector2D UV = FVector2D::ZeroVector;

	/** Chop seam that preserves authored control points instead of adding direction handles. */
	UPROPERTY()
	bool bPreserveGeometry = false;
};

/**
 * Streaming-safe endpoint attachment to a road owned by another RoadScene.
 * The parent is deliberately soft so World Partition does not bundle every
 * connected expressway sector into one hard-reference cluster.
 */
USTRUCT()
struct FCrossRoadSceneConnection
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Road|Streaming")
	TSoftObjectPtr<ARoadActor> Parent;

	UPROPERTY(VisibleAnywhere, Category = "Road|Streaming")
	int32 Index = INDEX_NONE;

	/** 0/1 pins to a target endpoint; INDEX_NONE keeps an interior spline distance. */
	UPROPERTY(VisibleAnywhere, Category = "Road|Streaming")
	int32 ParentEndpoint = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Road|Streaming")
	FVector2D UV = FVector2D::ZeroVector;
};

UCLASS(BlueprintType, Blueprintable)
class ROADBUILDER_API ARoadActor : public AActor
{
	GENERATED_UCLASS_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime")
	bool RuntimeSetRoadPoints(const TArray<FVector>& WorldPoints, bool bRebuildScene = true);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime")
	bool RuntimeAddRoadPoint(const FVector& WorldPoint, int32 InsertIndex = -1, bool bRebuildScene = true);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime")
	bool RuntimeSetRoadPoint(int32 PointIndex, const FVector& WorldPoint, bool bRebuildScene = true);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime")
	bool RuntimeRemoveRoadPoint(int32 PointIndex, bool bRebuildScene = true);

	UFUNCTION(BlueprintCallable, Category = "RoadBuilder|Runtime")
	void RuntimeRefresh(bool bRebuildScene = true);

	UFUNCTION(BlueprintPure, Category = "RoadBuilder|Runtime")
	int32 RuntimeGetRoadPointCount() const { return RoadPoints.Num(); }

	UFUNCTION(BlueprintPure, Category = "RoadBuilder|Runtime")
	FVector RuntimeGetRoadPoint(int32 PointIndex) const;

	UFUNCTION(BlueprintPure, Category = "RoadBuilder|Runtime")
	double RuntimeGetRoadLength() const;

	URoadBoundary* AddBoundary(double Offset, ULaneMarkStyle* LaneMarking, URoadProps* Props);
	URoadBoundary* GetRoadEdge(int Side);
	URoadBoundary* GetRoadBorder(int Side);
	URoadBoundary* GetBoundary(const FVector2D& UV);
	TArray<URoadBoundary*> GetBoundaries(const FVector2D& UV);
	TArray<URoadBoundary*> GetBoundaries(URoadBoundary* Boundary, int Side, const TArray<ELaneMarkType>& Types = {});
	TArray<URoadBoundary*> GetBoundaries(int Side, const TArray<ELaneMarkType>& Types = {})
	{
		return GetBoundaries(BaseCurve, Side, Types);
	}
	URoadLane* AddLane(URoadBoundary* LeftBoundary, URoadBoundary* RightBoundary, const FRoadLaneStyle& LaneStyle);
	URoadLane* CopyLane(URoadLane* SrcL, int TargetSide);
	URoadLane* GetLane(const FVector2D& UV);
	FVector2D GetUV(const FVector2D& Pos);
	FVector2D GetUV(const FVector& Pos);
	TArray<URoadLane*> GetLanes(URoadBoundary* Boundary, int Side, const TArray<ELaneType>& Types = {});
	TArray<URoadLane*> GetLanes(int Side, const TArray<ELaneType>& Types = {})
	{
		return GetLanes(BaseCurve, Side, Types);
	}
	UMarkingPoint* AddMarkingPoint(const FVector2D& UV);
	UMarkingCurve* AddMarkingCurve(bool ClosedLoop = false);
	UMarkingCurve* GetMarkingCurve(UPolygonMarkStyle* FillStyle);
	/** Returns only an automatically generated world-space marking, never an authored editable curve. */
	UMarkingCurve* GetGeneratedMarkingCurve(UPolygonMarkStyle* FillStyle);
	void DeleteMarking(URoadMarking* Marking);
	void DeleteAllMarkings();
	void AddDirPoint(int Index);
	void DelDirPoint(int Index);
	void ConnectTo(int& PointIndex, ARoadActor* Parent);
	void ConnectTo(ARoadActor* Parent, const FVector2D& UV, int Index, bool bPreserveGeometry = false);
	bool HasCrossRoadSceneConnection(int32 Index) const;
	bool HasCrossRoadSceneParentInScene(const ARoadScene* Scene) const;
	void RefreshCrossRoadSceneConnections();
	void DisconnectFrom(ARoadActor* Child, int Index);
	void DisconnectAll();
	void DisconnectAll(int& PointIndex);
	void DisconnectAt(int PointIndex);
	void DeleteBoundary(URoadBoundary* Boundary);
	void DeleteLane(URoadLane* Lane);
	void Join(ARoadActor* Road);
	void JoinLanes(ARoadActor* Road);
	void CutLanes(double R_Start, double R_End);
	ARoadActor* Chop(double Dist);
	ARoadActor* Split(URoadBoundary* Boundary);
	TArray<FRoadPoint> CalcRoadPoints(double R_Start, double R_End);
	TArray<FHeightPoint> CalcHeightPoints(double R_Start, double R_End);
	TArray<FRoadSegment> CutRoadSegments(double R_Start, double R_End);
	TArray<FHeightSegment> CutHeightSegments(double R_Start, double R_End);
	void InsertPoint(const FVector2D& Pos, int& Index)
	{
		if (RoadPoints.IsEmpty())
		{
			Index = RoadPoints.AddDefaulted();
		}
		else
		{
			// Undo/redo or switching roads can leave the editor's selected point
			// at INDEX_NONE or at an index from the previous road. Extending the
			// road must never use that stale value as a TArray index. In that case,
			// continue from whichever world-space endpoint was actually clicked.
			const bool bAppend = Index == RoadPoints.Num() - 1 ||
				(!RoadPoints.IsValidIndex(Index) &&
				 FVector2D::DistSquared(Pos, RoadPoints.Last().Pos) < FVector2D::DistSquared(Pos, RoadPoints[0].Pos));
			if (bAppend)
			{
				Index = RoadPoints.AddDefaulted();
			}
			else
			{
				RoadPoints.InsertDefaulted(0);
				Index = 0;
			}
		}
		RoadPoints[Index].Pos = Pos;
	}
	int AddPoint(double Dist)
	{
		int Index = GetPointIndex(RoadPoints, Dist);
		FRoadPoint& Start = RoadPoints[Index];
		FRoadPoint& End = RoadPoints[Index + 1];
		FRoadPoint NewPt = { FMath::Lerp(Start.Pos, End.Pos, (Dist - Start.Dist) / (End.Dist - Start.Dist)), Dist };
		RoadPoints.Insert(NewPt, Index + 1);
		return Index + 1;
	}
	int AddHeight(double Dist)
	{
		int Index = GetPointIndex(HeightPoints, Dist);
		FHeightPoint NewPt = {Dist, HeightPoints[Index].Height};
		HeightPoints.Insert(NewPt, Index+1);
		return Index + 1;
	}
	FRoadSegment& AddRoadSegment(double Dist, double Length, const FVector2D& StartPos, double StartRadian, double StartCurv, double EndCurv)
	{
		FRoadSegment& Segment = RoadSegments[RoadSegments.AddDefaulted()];
		Segment.Dist = Dist;
		Segment.Length = Length;
		Segment.StartPos = StartPos;
		Segment.StartRadian = StartRadian;
		Segment.StartCurv = StartCurv;
		Segment.EndCurv = EndCurv;
		return Segment;
	}
	FHeightSegment& AddHeightSegment(double Dist, double Height, double Dir)
	{
		FHeightSegment& Segment = HeightSegments[HeightSegments.AddDefaulted()];
		Segment.Dist = Dist;
		Segment.Height = Height;
		Segment.Dir = Dir;
		return Segment;
	}
	void AddArcs(const FVector2D& SrcPos, const FVector2D& SrcDir, const FVector2D& DstPos, const FVector2D& DstDir, double Radius, double StartH, double EndH);
	void AddSubRoad(URoadBoundary* Boundary, double Start, double End, double BaseOffset = 0);
	void ClearSegments();
	void ClearLanes();
	void Init(double Height);
	void InitWithStyle(URoadStyle* Style, double Height = 0);
	void InitWithRoads(URoadBoundary* Base, int Side, bool SkipSidewalks, bool KeepLeftLanes, uint32 LeftLaneMarkingMask, uint32 RightLaneMarkingMask);
	void UpdateCurve() { TSet<ARoadActor*> UpdatedRoads = { this }; UpdateCurve(UpdatedRoads); }
	void UpdateCurve(TSet<ARoadActor*>& UpdatedRoads);
	void UpdateCurveBySegments();
	void UpdateLanes();
	void BuildMesh(const TArray<FJunctionSlot>& Slots);
	bool IsLink();
	bool IsRamp();
	FConnectInfo& GetConnectedChild(ARoadActor* Child, int Index)
	{
		static FConnectInfo Tmp;
		for (FConnectInfo& Info : ConnectedChildren)
			if (Info.Child == Child && Info.Index == Index)
				return Info;
		return Tmp;
	}
	FVector2D GetPos(double Dist);
	FVector GetPos(int PointIndex);
	FVector GetPos(const FVector2D& UV);
	FVector2D GetDir2D(double Dist);
	FVector GetDir(double Dist);
	FVector GetRight(double Dist);
	double GetRadian(double Dist);
	double GetHeight(double Dist);
	double LeftWidth() { return 800; }
	double RightWidth() { return 800; }
	double Length() { return RoadSegments.Num() ? RoadSegments.Last().Dist + RoadSegments.Last().Length : 0; }
	ARoadScene* GetScene();
	AJunctionActor* GetJunction();
	void ExportXodr(FXmlNode* XmlNode, int& RoadId, int& ObjectId, int JunctionId);
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;
	void CreateStyle();
#endif

	UPROPERTY(EditAnywhere, Category = Road)
	double Smoothness = 800;

	UPROPERTY(EditAnywhere, Category = Road)
	TArray<FRoadPoint> RoadPoints;

	UPROPERTY(EditAnywhere, Category = Road)
	TArray<FHeightPoint> HeightPoints;

	UPROPERTY(EditAnywhere, Category = Road)
	TArray<FRoadSegment> RoadSegments;

	UPROPERTY(EditAnywhere, Category = Road)
	TArray<FHeightSegment> HeightSegments;

	UPROPERTY(EditAnywhere, Category = Road)
	TArray<URoadLane*> Lanes;

	UPROPERTY(EditAnywhere, Category = Road)
	TArray<URoadBoundary*> Boundaries;

	UPROPERTY(EditAnywhere, Category = Road)
	TArray<URoadMarking*> Markings;

	UPROPERTY()
	URoadBoundary* BaseCurve;

	UPROPERTY(EditAnywhere, Category = Road)
	ARoadActor* ConnectedParents[2] = { nullptr, nullptr };

	UPROPERTY(EditAnywhere, Category = Road)
	bool bHasGround = false;

	UPROPERTY(EditAnywhere, Category = Road)
	TArray<FConnectInfo> ConnectedChildren;

	UPROPERTY(VisibleAnywhere, Category = "Road|Streaming")
	TArray<FCrossRoadSceneConnection> CrossRoadSceneConnections;
};

UCLASS()
class ROADBUILDER_API URoadMeshComponent : public UStaticMeshComponent
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif
};

inline FXmlNode* XmlNode_CreateChild(FXmlNode* ParentNode, const TCHAR* Tag)
{
	ParentNode->AppendChildNode(Tag);
	return ParentNode->GetChildrenNodes().Last();
}

inline void XmlNode_AddAttribute(FXmlNode* Node, const TCHAR* Key, const FString& Value)
{
	TArray<FXmlAttribute>& Attrs = (TArray<FXmlAttribute>&)Node->GetAttributes();
	Attrs.Add(FXmlAttribute(Key, Value));
}

inline void XmlNode_AddAttribute(FXmlNode* Node, const TCHAR* Key, int Value)
{
	XmlNode_AddAttribute(Node, Key, FString::Printf(TEXT("%d"), Value));
}

inline void XmlNode_AddAttribute(FXmlNode* Node, const TCHAR* Key, double Value)
{
	XmlNode_AddAttribute(Node, Key, FString::Printf(TEXT("%e"), Value));
}

inline void XmlNode_AddABCD(FXmlNode* Node, const FVector4& ABCD)
{
	XmlNode_AddAttribute(Node, TEXT("a"), ABCD[0]);
	XmlNode_AddAttribute(Node, TEXT("b"), ABCD[1]);
	XmlNode_AddAttribute(Node, TEXT("c"), ABCD[2]);
	XmlNode_AddAttribute(Node, TEXT("d"), ABCD[3]);
}

DECLARE_CYCLE_STAT(TEXT("BuildLane"), STAT_BuildLane, STATGROUP_RoadBuilder);
DECLARE_CYCLE_STAT(TEXT("BuildBoundary"), STAT_BuildBoundary, STATGROUP_RoadBuilder);
