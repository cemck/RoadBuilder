// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#include "RoadScene.h"
#include "RoadBuilder.h"
#include "Algo/Reverse.h"
#include "XmlFile.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "ZoneShapeComponent.h"
#include "ZoneGraphTypes.h"
#include "ZoneGraphSettings.h"
#include "Components/InstancedStaticMeshComponent.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#endif

namespace
{
	struct FPolylineClosestApproach
	{
		bool bValid = false;
		double SourceDist = 0.0;
		double DestinationDist = 0.0;
		FVector SourcePosition = FVector::ZeroVector;
		FVector DestinationPosition = FVector::ZeroVector;
		double DistanceSquared2D = TNumericLimits<double>::Max();
	};

	void ClosestPointsOnSegments2D(
		const FVector2D& SourceStart,
		const FVector2D& SourceEnd,
		const FVector2D& DestinationStart,
		const FVector2D& DestinationEnd,
		double& OutSourceAlpha,
		double& OutDestinationAlpha)
	{
		const FVector2D SourceDirection = SourceEnd - SourceStart;
		const FVector2D DestinationDirection = DestinationEnd - DestinationStart;
		const FVector2D Offset = SourceStart - DestinationStart;
		const double SourceLengthSquared = SourceDirection.SizeSquared();
		const double DestinationLengthSquared = DestinationDirection.SizeSquared();
		const double DestinationProjection = FVector2D::DotProduct(DestinationDirection, Offset);

		if (SourceLengthSquared <= UE_DOUBLE_SMALL_NUMBER && DestinationLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			OutSourceAlpha = 0.0;
			OutDestinationAlpha = 0.0;
			return;
		}
		if (SourceLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			OutSourceAlpha = 0.0;
			OutDestinationAlpha = FMath::Clamp(DestinationProjection / DestinationLengthSquared, 0.0, 1.0);
			return;
		}

		const double SourceProjection = FVector2D::DotProduct(SourceDirection, Offset);
		if (DestinationLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			OutDestinationAlpha = 0.0;
			OutSourceAlpha = FMath::Clamp(-SourceProjection / SourceLengthSquared, 0.0, 1.0);
			return;
		}

		const double DirectionDot = FVector2D::DotProduct(SourceDirection, DestinationDirection);
		const double Denominator = SourceLengthSquared * DestinationLengthSquared - DirectionDot * DirectionDot;
		OutSourceAlpha = FMath::Abs(Denominator) > UE_DOUBLE_SMALL_NUMBER
			? FMath::Clamp((DirectionDot * DestinationProjection - SourceProjection * DestinationLengthSquared) / Denominator, 0.0, 1.0)
			: 0.0;
		OutDestinationAlpha = (DirectionDot * OutSourceAlpha + DestinationProjection) / DestinationLengthSquared;
		if (OutDestinationAlpha < 0.0)
		{
			OutDestinationAlpha = 0.0;
			OutSourceAlpha = FMath::Clamp(-SourceProjection / SourceLengthSquared, 0.0, 1.0);
		}
		else if (OutDestinationAlpha > 1.0)
		{
			OutDestinationAlpha = 1.0;
			OutSourceAlpha = FMath::Clamp((DirectionDot - SourceProjection) / SourceLengthSquared, 0.0, 1.0);
		}
	}

	FPolylineClosestApproach FindClosestApproach(const FPolyline& Source, const FPolyline& Destination)
	{
		FPolylineClosestApproach Result;
		for (int32 SourceIndex = 0; SourceIndex + 1 < Source.Points.Num(); ++SourceIndex)
		{
			const FPolyPoint& SourceStart = Source.Points[SourceIndex];
			const FPolyPoint& SourceEnd = Source.Points[SourceIndex + 1];
			for (int32 DestinationIndex = 0; DestinationIndex + 1 < Destination.Points.Num(); ++DestinationIndex)
			{
				const FPolyPoint& DestinationStart = Destination.Points[DestinationIndex];
				const FPolyPoint& DestinationEnd = Destination.Points[DestinationIndex + 1];
				double SourceAlpha = 0.0;
				double DestinationAlpha = 0.0;
				ClosestPointsOnSegments2D(
					FVector2D(SourceStart.Pos),
					FVector2D(SourceEnd.Pos),
					FVector2D(DestinationStart.Pos),
					FVector2D(DestinationEnd.Pos),
					SourceAlpha,
					DestinationAlpha);
				const FVector SourcePosition = FMath::Lerp(SourceStart.Pos, SourceEnd.Pos, SourceAlpha);
				const FVector DestinationPosition = FMath::Lerp(DestinationStart.Pos, DestinationEnd.Pos, DestinationAlpha);
				const double DeltaX = SourcePosition.X - DestinationPosition.X;
				const double DeltaY = SourcePosition.Y - DestinationPosition.Y;
				const double DistanceSquared2D = DeltaX * DeltaX + DeltaY * DeltaY;
				if (!Result.bValid || DistanceSquared2D < Result.DistanceSquared2D)
				{
					Result.bValid = true;
					Result.DistanceSquared2D = DistanceSquared2D;
					Result.SourcePosition = SourcePosition;
					Result.DestinationPosition = DestinationPosition;
					Result.SourceDist = FMath::Lerp(SourceStart.Dist, SourceEnd.Dist, SourceAlpha);
					Result.DestinationDist = FMath::Lerp(DestinationStart.Dist, DestinationEnd.Dist, DestinationAlpha);
				}
			}
		}
		return Result;
	}

	void AppendConnectedPolyline(
		TArray<FVector>& Polygon,
		const FPolyline& Curve,
		bool bSkipConnectedEndpoint,
		bool bSkipFarEndpoint)
	{
		if (Curve.Points.IsEmpty())
			return;
		const bool bReverse = !Polygon.IsEmpty() &&
			FVector::DistSquared2D(Polygon.Last(), Curve.Points.Last().Pos) <
			FVector::DistSquared2D(Polygon.Last(), Curve.Points[0].Pos);
		for (int32 Step = 0; Step < Curve.Points.Num(); ++Step)
		{
			if ((Step == 0 && bSkipConnectedEndpoint) ||
				(Step == Curve.Points.Num() - 1 && bSkipFarEndpoint))
				continue;
			const int32 PointIndex = bReverse ? Curve.Points.Num() - 1 - Step : Step;
			const FVector& Position = Curve.Points[PointIndex].Pos;
			if (Polygon.IsEmpty() || !Polygon.Last().Equals(Position, 1.0))
				Polygon.Add(Position);
		}
	}

	double PolygonArea2D(const TArray<FVector>& Polygon)
	{
		double TwiceArea = 0.0;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector& Current = Polygon[Index];
			const FVector& Next = Polygon[(Index + 1) % Polygon.Num()];
			TwiceArea += Current.X * Next.Y - Next.X * Current.Y;
		}
		return FMath::Abs(TwiceArea) * 0.5;
	}
}

void FJunctionLink::CreateRoad(AJunctionActor* Parent)
{
	Road = Parent->GetWorld()->SpawnActor<ARoadActor>();
	Road->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);
}

void FJunctionLink::Destroy()
{
	if (Road)
	{
		Road->DeleteAllMarkings();
		Road->Destroy();
		Road = nullptr;
	}
	Radius = 1000;
}

void FJunctionGate::Renew(double D, double S)
{
	InitDist = Dist = D;
	CornerDists[0] = CornerDists[1] = D;
	CutDists[0] = CutDists[1] = D;
	Radian = 0;
	Sign = S;
}

FJunctionGate& FJunctionSlot::InputGate() const
{
	return Junction->Gates[InputGateIndex()];
}

FJunctionGate& FJunctionSlot::OutputGate() const
{
	return Junction->Gates[OutputGateIndex()];
}

int FJunctionSlot::InputGateIndex() const
{
	for (int i = 0; i < Junction->Gates.Num(); i++)
	{
		FJunctionGate& Gate = Junction->Gates[i];
		if (Gate.Road == Road && Gate.Sign < 0 && Gate.InitDist == InitInputDist)
			return i;
	}
	return INDEX_NONE;
}

int FJunctionSlot::OutputGateIndex() const
{
	for (int i = 0; i < Junction->Gates.Num(); i++)
	{
		FJunctionGate& Gate = Junction->Gates[i];
		if (Gate.Road == Road && Gate.Sign > 0 && Gate.InitDist == InitOutputDist)
			return i;
	}
	return INDEX_NONE;
}

double FJunctionSlot::InputDist() const
{
	return HasInput() ? InputGate().Dist : 0;
}

double FJunctionSlot::OutputDist() const
{
	return HasOutput() ? OutputGate().Dist : Road->Length();
}

double FJunctionSlot::CrossDist() const
{
	double Input = FMath::Max(0, InitInputDist);
	double Output = FMath::Min(Road->Length(), InitOutputDist);
	return (Input + Output) / 2;
}

void FJunctionSlot::Combine(FJunctionSlot& Other)
{
	for (FJunctionGate& Gate : Other.Junction->Gates)
	{
		Gate.Clear();
		if (Gate.Road != Road)
		{
			bool AddGate = true;
			for (FJunctionGate& G : Junction->Gates)
			{
				if (G.Road == Gate.Road && G.Sign == Gate.Sign)
				{
					if (G.Sign < 0)
					{
						if (G.Dist > Gate.Dist)
						{
							G = Gate;
							InitInputDist = G.InitDist;
						}
					}
					else
					{
						if (G.Dist < Gate.Dist)
						{
							G = Gate;
							InitOutputDist = G.InitDist;
						}
					}
					AddGate = false;
					break;
				}
			}
			if (AddGate)
				Junction->Gates.Add(Gate);
		}
	}
	ARoadScene* Scene = Junction->GetScene();
	Other.Junction->Destroy();
	Scene->Junctions.Remove(Other.Junction);
}

AJunctionActor::AJunctionActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
#if USE_PROC_ROAD_MESH
	RootComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RootComponent"));
#else
	RootComponent = CreateDefaultSubobject<URoadMeshComponent>(TEXT("RootComponent"));
#endif
}

void AJunctionActor::AddRoad(ARoadActor* Road, double Dist)
{
	FJunctionSlot Slot = GetSlot(Road, Dist);
//	if (Dist > DefaultJunctionExtent)
	{
		if (Slot.IsValid() && Slot.HasInput())
		{
			FJunctionGate& Input = Slot.InputGate();
			if (Input.IsExpired())
				Input.Renew(Dist, -1);
			else
				Input.InitDist = FMath::Min(Input.InitDist, Dist);
		}
		else
			AddGate(Road, Dist, -1);
	}
//	if (Dist < Road->Length() - DefaultJunctionExtent)
	{
		if (Slot.IsValid() && Slot.HasOutput())
		{
			FJunctionGate& Output = Slot.OutputGate();
			if (Output.IsExpired())
				Output.Renew(Dist, 1);
			else
				Output.InitDist = FMath::Max(Output.InitDist, Dist);
		}
		else
			AddGate(Road, Dist, 1);
	}
}

void AJunctionActor::Update(TOctree2<FRoadOctreeElement, FRoadOctreeSemantics>& Octree)
{
	FVector Center(0, 0, 0);
	ARoadScene* Scene = GetScene();
	for (int i = 0; i < Gates.Num();)
	{
		FJunctionGate& Gate = Gates[i];
		if (Gate.Sign < 0 && Gate.InitDist < DefaultJunctionExtent || Gate.Sign > 0 && Gate.InitDist > Gate.Road->Length() - DefaultJunctionExtent)
		{
			Gate.Clear();
			Gates.RemoveAt(i);
		}
		else
			i++;
	}
	for (FJunctionGate& Gate : Gates)
		Center += Gate.Road->BaseCurve->GetPos(Gate.InitDist) / Gates.Num();
	for (FJunctionGate& Gate : Gates)
	{
		double Offset = Gate.Sign > 0 ? FMath::Min(12800, Gate.Road->Length() - Gate.InitDist) : FMath::Max(-12800, -Gate.InitDist);
		FVector End = Gate.Road->BaseCurve->GetPos(Gate.InitDist + Offset);
		FVector Dir = (End - Center).GetSafeNormal();
		Gate.Radian = FMath::Atan2(Dir.Y, Dir.X);
	}
	Gates.Sort();
	for (int i = 0; i < Gates.Num(); i++)
	{
		FJunctionGate& Gate = Gates[i];
		for (int j = 0; j < Gate.Links.Num(); j++)
		{
			if (j < Gates.Num())
			{
				FJunctionGate& Next = Gates[(i + j) % Gates.Num()];
				if (Gate.Links[j].InputRoad != Gate.Road || Gate.Links[j].OutputRoad != Next.Road)
					Gate.Links[j].Destroy();
			}
			else
				Gate.Links[j].Destroy();
		}
		Gate.Links.SetNum(Gates.Num());
		FJunctionGate& Next = Gates[(i + 1) % Gates.Num()];
		bool SrcRamp = Gate.IsRampOf(Next);
		bool DstRamp = Next.IsRampOf(Gate);
		if (SrcRamp || DstRamp)
		{
			double Dist1, Dist2;
			if (SrcRamp)
			{
				FVector2D UV = Next.Road->GetUV((FMath::IsNearlyZero(Gate.InitDist) ? Gate.Road->BaseCurve->Curve.Points[0] : Gate.Road->BaseCurve->Curve.Points.Last()).Pos);
				Dist1 = Gate.InitDist;
				Dist2 = UV.X;
			}
			else
			{
				FVector2D UV = Gate.Road->GetUV((FMath::IsNearlyZero(Next.InitDist) ? Next.Road->BaseCurve->Curve.Points[0] : Next.Road->BaseCurve->Curve.Points.Last()).Pos);
				Dist1 = UV.X;
				Dist2 = Next.InitDist;
			}
			UpdateCorner(Gate, Dist1, Next, Dist2);
		}
		else
		{
			int SrcSide = Gate.Sign > 0 ? 1 : 0;
			int DstSide = Next.Sign > 0 ? 0 : 1;
			URoadBoundary* SrcBoundary = Gate.Road->GetRoadEdge(SrcSide);
			URoadBoundary* DstBoundary = Next.Road->GetRoadEdge(DstSide);
			FPolyline& SrcCurve = SrcBoundary->Curve;
			FPolyline& DstCurve = DstBoundary->Curve;
#if 0
			for (int j = 0; j < SrcCurve.Points.Num() - 1; j++)
			{
				FBox SrcBox = SrcBoundary->Curve.GetSegmentBounds(j);
				for (int k = 0; k < DstCurve.Points.Num() - 1; k++)
				{
					FBox DstBox = DstBoundary->Curve.GetSegmentBounds(k);
					if (SrcBox.Intersect(DstBox))
					{
						double Seg1, Seg2;
						if (DoLineSegmentsIntersect((const FVector2D&)SrcCurve.Points[j].Pos, (const FVector2D&)SrcCurve.Points[j + 1].Pos, (const FVector2D&)DstCurve.Points[k].Pos, (const FVector2D&)DstCurve.Points[k + 1].Pos, Seg1, Seg2))
						{
							double Dist1 = FMath::Lerp(SrcCurve.Points[j].Dist, SrcCurve.Points[j + 1].Dist, Seg1);
							double Dist2 = FMath::Lerp(DstCurve.Points[k].Dist, DstCurve.Points[k + 1].Dist, Seg2);
							UpdateCorner(Gate, Dist1, Next, Dist2);
							goto _NextGate;
						}
					}
				}
			}
#else
			double EndDist = Gate.Sign > 0 ? (Gate.InitDist + Gate.Road->Length()) / 2 : Gate.InitDist / 2;
			int StartSegment = SrcCurve.GetPoint(Gate.InitDist);
			int EndSegment = SrcCurve.GetPoint(EndDist);
			int Dir = FMath::Sign(EndSegment - StartSegment);
			EndSegment += Dir;
			for (int j = StartSegment; j != EndSegment; j += Dir)
			{
				bool ResultFound = false;
				FRoadOctreeElement SrcSegment(SrcBoundary, j);
				FBox Box = SrcSegment.GetBounds();
				Box.Min.Z -= 1000;
				Box.Max.Z += 1000;
				Octree.FindElementsWithBoundsTest(Box, [&](const FRoadOctreeElement& DstSegment)
				{
					//The two boundaries may be the same one so adjacent checking is still needed
					if (ResultFound || DstSegment.Boundary != DstBoundary || SrcSegment.Adjacent(DstSegment))
						return;
					double Seg1, Seg2;
					if (DoLineSegmentsIntersect((const FVector2D&)SrcCurve.Points[SrcSegment.Index].Pos, (const FVector2D&)SrcCurve.Points[SrcSegment.Index + 1].Pos, (const FVector2D&)DstCurve.Points[DstSegment.Index].Pos, (const FVector2D&)DstCurve.Points[DstSegment.Index + 1].Pos, Seg1, Seg2))
					{
						double Dist1 = FMath::Lerp(SrcCurve.Points[SrcSegment.Index].Dist, SrcCurve.Points[SrcSegment.Index + 1].Dist, Seg1);
						double Dist2 = FMath::Lerp(DstCurve.Points[DstSegment.Index].Dist, DstCurve.Points[DstSegment.Index + 1].Dist, Seg2);
						UpdateCorner(Gate, Dist1, Next, Dist2);
						ResultFound = true;
					}
				});
				if (ResultFound)
					goto _NextGate;
			}
		//	UE_LOG(LogRoadBuilder, Warning, TEXT("Can't solve intersection"));
			Gate.CutDists[SrcSide] = Gate.CornerDists[SrcSide] = -1;
			Next.CutDists[DstSide] = Next.CornerDists[DstSide] = -1;
		_NextGate:;
		}
#endif
	}
	for (FJunctionGate& Gate : Gates)
	{
		for (int i = 0; i < 2; i++)
		{
			if (Gate.CornerDists[i] < 0)
			{
				if (Gate.CornerDists[!i] >= 0)
					Gate.CornerDists[i] = Gate.CornerDists[!i];
				else
					Gate.CornerDists[i] = ((Gate.Sign > 0) ^ i) ? 0 : Gate.Road->Length();
			}
			if (Gate.CutDists[i] < 0)
			{
				if (Gate.CutDists[!i] >= 0)
					Gate.CutDists[i] = Gate.CutDists[!i];
				else
					Gate.CutDists[i] = ((Gate.Sign > 0) ^ i) ? 0 : Gate.Road->Length();
			}
		}
	}
	for (FJunctionGate& Gate : Gates)
	{
		if (Gate.Sign > 0)
			Gate.Dist = FMath::Max(Gate.CutDists[0], Gate.CutDists[1]);
		else
			Gate.Dist = FMath::Min(Gate.CutDists[0], Gate.CutDists[1]);
	}
}

void AJunctionActor::BuildLink(FJunctionGate& Gate, FJunctionGate& Next, int Index)
{
	// Corner borders are geometric and must not flip with traffic handedness.
	// Driving links, however, must select the inbound and outbound traffic sides.
	const int GeometrySrcSide = Gate.Sign > 0 ? RD_LEFT : RD_RIGHT;
	const int GeometryDstSide = Next.Sign > 0 ? RD_RIGHT : RD_LEFT;
	ARoadScene* Scene = GetScene();
	const int TrafficSrcSide = Scene
		? Scene->GetTrafficSideForDirection(-Gate.Sign)
		: GeometrySrcSide;
	const int TrafficDstSide = Scene
		? Scene->GetTrafficSideForDirection(Next.Sign)
		: GeometryDstSide;
	int SrcSide = Index == CornerIndex ? GeometrySrcSide : TrafficSrcSide;
	int DstSide = Index == CornerIndex ? GeometryDstSide : TrafficDstSide;
	bool SrcRamp = Gate.IsRampOf(Next);
	bool DstRamp = Next.IsRampOf(Gate);
	const bool bRampGoreCorner = Index == CornerIndex && (SrcRamp || DstRamp);
	double SrcCorner, DstCorner;
	URoadBoundary *SrcBoundary, *DstBoundary;
	bool SkipSidewalks = Index != CornerIndex;
	uint32 LeftLaneMarkingMask = 0;
	uint32 RightLaneMarkingMask = 0;
	FJunctionLink& Link = Gate.Links[Index];
	Link.InputRoad = Gate.Road;
	Link.OutputRoad = Next.Road;
	TArray<ELaneType> DrivingLaneTypes = { ELaneType::Driving, ELaneType::Shoulder };
	if (Index == CornerIndex)
	{
		SrcCorner = Gate.CutDists[SrcSide];
		DstCorner = Next.CutDists[DstSide];
		SrcBoundary = Gate.Road->GetRoadEdge(SrcSide);
		DstBoundary = Next.Road->GetRoadEdge(DstSide);
		if (SrcRamp)
			LeftLaneMarkingMask = (1 << (Gate.Road->GetLanes(SrcBoundary, !SrcSide, DrivingLaneTypes).Num() + 1)) - 1;
		else if (DstRamp)
			LeftLaneMarkingMask = (1 << (Next.Road->GetLanes(DstBoundary, !DstSide, DrivingLaneTypes).Num() + 1)) - 1;
		else if (Gate.Road == Next.Road || Gate.IsConnected(Next))
			LeftLaneMarkingMask = (1 << (Gate.Road->GetLanes(SrcBoundary, !SrcSide, DrivingLaneTypes).Num() + 1)) - 1;
		else
			LeftLaneMarkingMask = 1;
	}
	else if (Index == 0)
	{
		URoadLane* TurnLane = Next.Road->BaseCurve->GetLane(!SrcSide);
		if (!TurnLane)
		{
			Link.Destroy();
			return;
		}
		SrcCorner = Gate.Dist;
		DstCorner = Next.Dist;
		SrcBoundary = Gate.Road->BaseCurve;
		DstBoundary = TurnLane->GetBoundary(!SrcSide);
	}
	else
	{
		SrcCorner = SrcRamp ? (Gate.Sign > 0 ? 0 : Gate.Road->Length()) : Gate.Dist;
		DstCorner = DstRamp ? (Next.Sign > 0 ? 0 : Next.Road->Length()) : Next.Dist;
		SrcBoundary = Gate.Road->BaseCurve;
		DstBoundary = Next.Road->BaseCurve;
		if (Gate.Road == Next.Road || Gate.IsConnected(Next) || SrcRamp || DstRamp)
		{
			int Idx = ((&Next - Gates.GetData()) - 1 + Gates.Num()) % Gates.Num();
			if (Gates[Idx].IsRampOf(Next) || Gates[Idx].IsRampOf(Gate) || SrcRamp || DstRamp)
			{
				TArray<URoadLane*> SrcLanes = Gate.Road->GetLanes(SrcBoundary, SrcSide, DrivingLaneTypes);
				TArray<URoadLane*> DstLanes = Next.Road->GetLanes(DstBoundary, DstSide, DrivingLaneTypes);
				RightLaneMarkingMask = (1 << (FMath::Min(SrcLanes.Num(), DstLanes.Num()) - 1)) - 1;
			}
		}
	}
	URoadStyle* Style = URoadStyle::Create(
		SrcBoundary,
		SrcSide,
		DstBoundary,
		DstSide,
		SkipSidewalks,
		true,
		LeftLaneMarkingMask,
		RightLaneMarkingMask);
	// Props on the geometric corner boundary are appropriate for ordinary
	// intersections, but a ramp merge/diverge corner is the painted gore nose.
	// Copying the source edge props here wraps curbs and guardrails around the
	// inside of the fork and prevents the clean road-surface joint.
	if (bRampGoreCorner)
	{
		Style->BaseCurveProps = nullptr;
		for (FRoadLaneStyle& LaneStyle : Style->LeftLanes)
		{
			LaneStyle.Props = nullptr;
		}
		for (FRoadLaneStyle& LaneStyle : Style->RightLanes)
		{
			LaneStyle.Props = nullptr;
		}
	}
	if (!Link.Road)
	{
		if (Style->NumDrivingLanes() <= 0 && Index != CornerIndex)
			return;
		Link.CreateRoad(this);
		// A junction link receives its initial markings from the connected roads,
		// but later edits on the link are authored overrides and must persist.
		Link.Road->InitWithStyle(Style);
	}
	else
	{
		// Preserve lane, boundary, and marking styles on existing junction links.
		// Reinitializing the style here replaces user overrides with the project
		// DefaultDashStyle/DefaultSolidStyle every time any marking is edited.
		Link.Road->ClearSegments();
	}
	if (Link.Road && bRampGoreCorner)
	{
		// Existing saved junction links keep their generated style between
		// rebuilds, so remove inherited props from their boundaries as well.
		for (URoadBoundary* Boundary : Link.Road->Boundaries)
		{
			if (!Boundary)
			{
				continue;
			}
			for (FBoundarySegment& Segment : Boundary->Segments)
			{
				Segment.Props = nullptr;
			}
		}
	}
	if (Link.Road)
	{
		if (SrcBoundary == DstBoundary)
		{
			if (Gate.Dist != Next.Dist)
				Link.Road->AddSubRoad(SrcBoundary, Gate.Dist, Next.Dist);
			else
				Link.Destroy();
		}
		else
		{
			if (!FMath::IsNearlyEqual(Gate.Dist, SrcCorner))
				Link.Road->AddSubRoad(SrcBoundary, Gate.Dist, SrcCorner, SrcRamp);
			FVector2D SrcPos(SrcBoundary->GetPos(SrcCorner));
			FVector2D SrcDir(SrcBoundary->GetDir(SrcCorner) * (-Gate.Sign));
			FVector2D DstPos(DstBoundary->GetPos(DstCorner));
			FVector2D DstDir(DstBoundary->GetDir(DstCorner) * (Next.Sign));
			if (!SrcPos.Equals(DstPos) && !SrcRamp && !DstRamp)
				Link.Road->AddArcs(SrcPos, SrcDir, DstPos, DstDir, Link.Radius, Gate.Road->GetHeight(SrcCorner), Next.Road->GetHeight(DstCorner));
			if (!FMath::IsNearlyEqual(Next.Dist, DstCorner))
				Link.Road->AddSubRoad(DstBoundary, DstCorner, Next.Dist, DstRamp);
			if (!Link.Road->RoadSegments.Num())
				Link.Destroy();
		}
	}
	if (Link.Road)
		Link.Road->UpdateCurveBySegments();
}

void AJunctionActor::Build()
{
	for (int i = 0; i < Gates.Num(); i++)
	{
		FJunctionGate& Gate = Gates[i];
		for (int j = 0; j < Gates.Num(); j++)
		{
			const int TargetGateIndex = (i + j) % Gates.Num();
			if (Gate.IsInput() && j != 0 && j != 1 && !IsTurnAllowed(i, TargetGateIndex))
			{
				if (j < Gate.Links.Num() && Gate.Links[j].Road)
				{
					Gate.Links[j].Destroy();
				}
				continue;
			}
			FJunctionGate& Next = Gates[TargetGateIndex];
			BuildLink(Gate, Next, j);
		}
	}
	// Place no-turn signs for restricted turns
	// First, remove any previously generated sign ISMCs
	{
		TArray<UInstancedStaticMeshComponent*> OldISMCs;
		GetComponents<UInstancedStaticMeshComponent>(OldISMCs);
		for (UInstancedStaticMeshComponent* Comp : OldISMCs)
		{
			Comp->DestroyComponent();
		}
	}
	USettings_Global* Settings = GetMutableDefault<USettings_Global>();
	if (UStaticMesh* SignMesh = Settings->NoTurnSignMesh.LoadSynchronous())
	{
		for (const FTurnRestriction& Restriction : TurnRestrictions)
		{
			if (Restriction.FromGateIndex >= 0 && Restriction.FromGateIndex < Gates.Num())
			{
				FJunctionGate& Gate = Gates[Restriction.FromGateIndex];
				ARoadScene* Scene = GetScene();
				int SrcSide = Scene
					? Scene->GetTrafficSideForDirection(-Gate.Sign)
					: (Gate.Sign > 0 ? RD_LEFT : RD_RIGHT);
				URoadBoundary* SrcEdge = Gate.Road->GetRoadEdge(SrcSide);
				FVector SignPos = SrcEdge->GetPos(Gate.Dist);
				FVector SignDir = Gate.Road->GetDir(Gate.Dist) * Gate.Sign;
				FRotator SignRot = SignDir.Rotation();
				const double OutsideSign = SrcSide == RD_LEFT ? 1.0 : -1.0;
				SignPos += Gate.Road->GetRight(Gate.Dist) * OutsideSign * 150.0;
				SignPos.Z += 300.0;
				FTransform SignTransform(SignRot, SignPos);
				UInstancedStaticMeshComponent* ISMC = nullptr;
				TArray<UInstancedStaticMeshComponent*> ISMCs;
				GetComponents<UInstancedStaticMeshComponent>(ISMCs);
				for (UInstancedStaticMeshComponent* Comp : ISMCs)
				{
					if (Comp->GetStaticMesh() == SignMesh)
					{
						ISMC = Comp;
						break;
					}
				}
				if (!ISMC)
				{
					ISMC = NewObject<UInstancedStaticMeshComponent>(this);
					ISMC->SetStaticMesh(SignMesh);
					ISMC->SetupAttachment(GetRootComponent());
					ISMC->RegisterComponent();
				}
				ISMC->AddInstance(SignTransform);
			}
		}
	}
	BuildGoreMarkings();
	FRoadMesh Builder;
	TArray<FVector> Points;
	TArray<FVector> CornerPoints;
	TMap<ULaneShape*, int> Shapes;
	for (int i = 0; i < Gates.Num(); i++)
	{
		FJunctionGate& Gate = Gates[i];
		if (Gate.Links[1].Road)
		{
			FPolyline& Corner = Gate.Links[1].Road->BaseCurve->Curve;
			for (FPolyPoint& Point : Corner.Points)
				Points.Add(Point.Pos);
			CornerPoints.Add(Corner.Points[0].Pos);
			CornerPoints.Add(Corner.Points.Last().Pos);
		}
		for (int j = 0; j < 2; j++)
		{
			TArray<URoadLane*> Lanes = Gate.Road->GetLanes(j, { ELaneType::Driving });
			for (URoadLane* Lane : Lanes)
				if (ULaneShape* Shape = Gate.Sign > 0 ? Lane->Segments[0].LaneShape : Lane->Segments.Last().LaneShape)
					Shapes.FindOrAdd(Shape)++;
		}
	}
	if (Shapes.Num() > 0 && !FMath::IsNearlyZero(CalcOBB(CornerPoints).GetSize().Y))
	{
		Shapes.ValueSort([](int A, int B)->bool {return A > B; });
		ULaneShape* Shape = TMap<ULaneShape*, int>::TIterator(Shapes)->Key;
		Builder.AddPolygon(Shape->GetSurfaceMaterial(), Shape->GetBackfaceMaterial(), Points);
		Builder.Build(GetRootComponent());
		//Markings depend on junction Mesh so build later
		for (FJunctionGate& Gate : Gates)
		{
			for (FJunctionLink& Link : Gate.Links)
				if (Link.Road)
					Link.Road->BuildMesh(TArray<FJunctionSlot>());
		}
	}
	for (FGoreDiagnostic& Diagnostic : GoreDiagnostics)
	{
		if (!Diagnostic.IsReady())
			continue;

		UMarkingCurve* Marking = Diagnostic.Marking.Get();
		const int32 MeshTriangleCount = IsValid(Marking) ? Marking->LastBuildTriangleCount : 0;
		if (MeshTriangleCount <= 0)
		{
			Diagnostic.State = EGoreDiagnosticState::Blocked;
			Diagnostic.Status = FString::Printf(
				TEXT("BLOCKED %d/%d | polygon passed but chevron style emitted 0 mesh triangles"),
				Diagnostic.RequirementsMet,
				FGoreDiagnostic::RequirementCount);
			continue;
		}

		++Diagnostic.RequirementsMet;
		Diagnostic.Status.ReplaceInline(
			*FString::Printf(TEXT("READY %d/%d"), FGoreDiagnostic::RequirementCount - 1, FGoreDiagnostic::RequirementCount),
			*FString::Printf(TEXT("READY %d/%d"), FGoreDiagnostic::RequirementCount, FGoreDiagnostic::RequirementCount));
		Diagnostic.Status += FString::Printf(TEXT(" | %d mesh triangles"), MeshTriangleCount);
	}
}

void AJunctionActor::BuildGoreMarkings()
{
	DebugCurves.Empty();
	GoreDiagnostics.Empty();
	USettings_Global* GlobalSettings = GetMutableDefault<USettings_Global>();
	UPolygonMarkStyle* FillStyle = GlobalSettings->DefaultGoreMarking.LoadSynchronous();
	auto SetBlocked = [](FGoreDiagnostic& Diagnostic, const FString& Reason)
	{
		Diagnostic.State = EGoreDiagnosticState::Blocked;
		Diagnostic.Status = FString::Printf(
			TEXT("BLOCKED %d/%d | %s"),
			Diagnostic.RequirementsMet,
			FGoreDiagnostic::RequirementCount,
			*Reason);
	};
	// A previous handedness build may have stored the generated polygon on a
	// different junction link.  Clear only generated gore-fill curves from link
	// roads before rebuilding so the obsolete stretched polygon cannot survive.
	for (FJunctionGate& ExistingGate : Gates)
	{
		for (FJunctionLink& ExistingLink : ExistingGate.Links)
		{
			if (!ExistingLink.Road)
				continue;
			for (int MarkingIndex = ExistingLink.Road->Markings.Num() - 1; MarkingIndex >= 0; --MarkingIndex)
			{
				UMarkingCurve* ExistingCurve = Cast<UMarkingCurve>(ExistingLink.Road->Markings[MarkingIndex]);
				if (ExistingCurve && ExistingCurve->bUseGeneratedWorldPoints && ExistingCurve->FillStyle == FillStyle)
					ExistingLink.Road->DeleteMarking(ExistingCurve);
			}
		}
	}
	for (int i = 0; i < Gates.Num(); i++)
	{
		int j = (i + 1) % Gates.Num();
		FJunctionGate& Gate = Gates[i];
		FJunctionGate& Next = Gates[j];
		int SrcConn = GetRampConnection(Gate);
		int DstConn = GetRampConnection(Next);
		if (SrcConn != INDEX_NONE || DstConn != INDEX_NONE)
		{
			// Direct ramp-of pairs are part of the same road-side connection and were
			// intentionally ignored by the original generator.  They are not failed
			// gore candidates, so keep them out of the diagnostics as well.
			if (Gate.IsRampOf(Next) || Next.IsRampOf(Gate))
				continue;

			FGoreDiagnostic& Diagnostic = GoreDiagnostics.AddDefaulted_GetRef();
			Diagnostic.GateIndex = i;
			Diagnostic.NextGateIndex = j;
			if (IsValid(Gate.Road) && Gate.Road->BaseCurve && IsValid(Next.Road) && Next.Road->BaseCurve)
			{
				Diagnostic.LabelLocation =
					(Gate.Road->BaseCurve->GetPos(Gate.Dist) + Next.Road->BaseCurve->GetPos(Next.Dist)) * 0.5;
			}

			++Diagnostic.RequirementsMet; // Candidate pair is not filtered.
			if (!FillStyle)
			{
				SetBlocked(Diagnostic, TEXT("Default Gore Marking style is not loaded"));
				continue;
			}
			++Diagnostic.RequirementsMet;

			int k = SrcConn != INDEX_NONE ? SrcConn : DstConn;
			FJunctionLink& Corner = Gate.Links[1];
			if (!Corner.Road)
			{
				SetBlocked(Diagnostic, TEXT("corner connector road is missing"));
				continue;
			}
			++Diagnostic.RequirementsMet;
			TArray<URoadBoundary*> CornerBoundaries = Corner.Road->GetBoundaries(1, { ELaneMarkType::Solid });
			if (!CornerBoundaries.Num())
			{
				SetBlocked(Diagnostic, TEXT("corner connector has no physical-side solid boundary"));
				continue;
			}
			Diagnostic.CornerBoundary = CornerBoundaries.Last()->Curve;
			++Diagnostic.RequirementsMet;
			// A gore is geometric: use whichever generated connector exists between
			// the ramp connection gate and each side of the adjacent junction edge.
			// Looking up the unordered gate pair is what makes this identical for RHT
			// and LHT even though the drivable connector direction is reversed.
			auto GetLinkBetween = [&](int32 FirstGateIndex, int32 SecondGateIndex)->FJunctionLink*
			{
				FJunctionLink& Forward = Gates[FirstGateIndex].Links[
					(SecondGateIndex - FirstGateIndex + Gates.Num()) % Gates.Num()];
				if (Forward.Road)
					return &Forward;
				FJunctionLink& Reverse = Gates[SecondGateIndex].Links[
					(FirstGateIndex - SecondGateIndex + Gates.Num()) % Gates.Num()];
				return Reverse.Road ? &Reverse : nullptr;
			};
			FJunctionLink* SrcLink = GetLinkBetween(k, i);
			FJunctionLink* DstLink = GetLinkBetween(k, j);
			if (!SrcLink || !DstLink)
			{
				const FString Missing = !SrcLink && !DstLink
					? TEXT("source and destination connector roads are missing")
					: (!SrcLink ? TEXT("source connector road is missing") : TEXT("destination connector road is missing"));
				SetBlocked(Diagnostic, Missing);
				continue;
			}
			++Diagnostic.RequirementsMet;
			auto GetSolidBoundaries = [](ARoadActor* Road)
			{
				TArray<URoadBoundary*> Boundaries;
				for (int32 Side = 0; Side < 2; ++Side)
				{
					for (URoadBoundary* Boundary : Road->GetBoundaries(Side, { ELaneMarkType::Solid }))
						Boundaries.AddUnique(Boundary);
				}
				return Boundaries;
			};
			TArray<URoadBoundary*> SrcBoundaries = GetSolidBoundaries(SrcLink->Road);
			TArray<URoadBoundary*> DstBoundaries = GetSolidBoundaries(DstLink->Road);
			if (!SrcBoundaries.Num() || !DstBoundaries.Num())
			{
				const FString Missing = !SrcBoundaries.Num() && !DstBoundaries.Num()
					? TEXT("source and destination solid boundaries are missing")
					: (!SrcBoundaries.Num() ? TEXT("source solid boundary is missing") : TEXT("destination solid boundary is missing"));
				SetBlocked(Diagnostic, Missing);
				continue;
			}
			++Diagnostic.RequirementsMet;
			{
				URoadBoundary* CornerBoundary = CornerBoundaries.Last();
				auto HasUsableCurve = [](const URoadBoundary* Boundary)
				{
					return Boundary && Boundary->Curve.Points.Num() >= 2 &&
						Boundary->Curve.Points[0].Dist < Boundary->Curve.Points.Last().Dist;
				};
				URoadBoundary* SrcBoundary = nullptr;
				URoadBoundary* DstBoundary = nullptr;
				FPolylineClosestApproach BoundaryApproach;
				double BestBoundaryScore = TNumericLimits<double>::Max();
				for (URoadBoundary* SourceCandidate : SrcBoundaries)
				{
					if (!HasUsableCurve(SourceCandidate))
						continue;
					for (URoadBoundary* DestinationCandidate : DstBoundaries)
					{
						if (!HasUsableCurve(DestinationCandidate))
							continue;
						const FPolylineClosestApproach Approach = FindClosestApproach(
							SourceCandidate->Curve,
							DestinationCandidate->Curve);
						if (!Approach.bValid)
							continue;
						double ConnectionScore = FMath::Sqrt(Approach.DistanceSquared2D) * 10.0;
						if (HasUsableCurve(CornerBoundary))
						{
							const FVector SourceFar = FVector::DistSquared2D(
								SourceCandidate->Curve.Points[0].Pos,
								Approach.SourcePosition) > FVector::DistSquared2D(
								SourceCandidate->Curve.Points.Last().Pos,
								Approach.SourcePosition)
								? SourceCandidate->Curve.Points[0].Pos
								: SourceCandidate->Curve.Points.Last().Pos;
							const FVector DestinationFar = FVector::DistSquared2D(
								DestinationCandidate->Curve.Points[0].Pos,
								Approach.DestinationPosition) > FVector::DistSquared2D(
								DestinationCandidate->Curve.Points.Last().Pos,
								Approach.DestinationPosition)
								? DestinationCandidate->Curve.Points[0].Pos
								: DestinationCandidate->Curve.Points.Last().Pos;
							const FVector& CornerStart = CornerBoundary->Curve.Points[0].Pos;
							const FVector& CornerEnd = CornerBoundary->Curve.Points.Last().Pos;
							ConnectionScore += FMath::Min(
								FVector::Dist2D(SourceFar, CornerStart) + FVector::Dist2D(DestinationFar, CornerEnd),
								FVector::Dist2D(SourceFar, CornerEnd) + FVector::Dist2D(DestinationFar, CornerStart));
						}
						if (ConnectionScore < BestBoundaryScore)
						{
							BestBoundaryScore = ConnectionScore;
							SrcBoundary = SourceCandidate;
							DstBoundary = DestinationCandidate;
							BoundaryApproach = Approach;
						}
					}
				}
				if (!SrcBoundary || !DstBoundary)
				{
					SetBlocked(Diagnostic, TEXT("connector roads have no usable solid-boundary pair"));
					continue;
				}
				++Diagnostic.RequirementsMet;
				Diagnostic.SourceBoundary = SrcBoundary->Curve;
				Diagnostic.DestinationBoundary = DstBoundary->Curve;
				DebugCurves.Add(SrcBoundary->Curve);
				DebugCurves.Add(DstBoundary->Curve);

				double Dist1 = 0.0;
				double Dist2 = 0.0;
				const bool bExactIntersection = SrcBoundary->Curve.SolveIntersection(DstBoundary->Curve, Dist1, Dist2);
				FVector SourceNose = FVector::ZeroVector;
				FVector DestinationNose = FVector::ZeroVector;
				double NoseGap = 0.0;
				double JunctionEdgeWidth = 0.0;

				if (bExactIntersection)
				{
					SourceNose = SrcBoundary->GetPos(Dist1);
					DestinationNose = DstBoundary->GetPos(Dist2);
				}
				else
				{
					const FPolylineClosestApproach& Closest = BoundaryApproach;
					if (!Closest.bValid)
					{
						SetBlocked(Diagnostic, TEXT("could not calculate a closest approach between split boundaries"));
						continue;
					}

					Dist1 = Closest.SourceDist;
					Dist2 = Closest.DestinationDist;
					SourceNose = Closest.SourcePosition;
					DestinationNose = Closest.DestinationPosition;
					NoseGap = FMath::Sqrt(Closest.DistanceSquared2D);

					// The closest approach is valid only when the two boundaries actually
					// narrow relative to the opposite junction edge.  This is a geometric
					// requirement, not an arbitrary world-distance allowance.
					if (HasUsableCurve(CornerBoundary))
					{
						JunctionEdgeWidth = FVector::Dist2D(
							CornerBoundary->Curve.Points[0].Pos,
							CornerBoundary->Curve.Points.Last().Pos);
					}
					else
					{
						const FVector SourceFar = FVector::DistSquared2D(
							SrcBoundary->Curve.Points[0].Pos,
							Closest.SourcePosition) > FVector::DistSquared2D(
							SrcBoundary->Curve.Points.Last().Pos,
							Closest.SourcePosition)
							? SrcBoundary->Curve.Points[0].Pos
							: SrcBoundary->Curve.Points.Last().Pos;
						const FVector DestinationFar = FVector::DistSquared2D(
							DstBoundary->Curve.Points[0].Pos,
							Closest.DestinationPosition) > FVector::DistSquared2D(
							DstBoundary->Curve.Points.Last().Pos,
							Closest.DestinationPosition)
							? DstBoundary->Curve.Points[0].Pos
							: DstBoundary->Curve.Points.Last().Pos;
						JunctionEdgeWidth = FVector::Dist2D(SourceFar, DestinationFar);
					}
					if (JunctionEdgeWidth <= UE_DOUBLE_SMALL_NUMBER || NoseGap >= JunctionEdgeWidth)
					{
						SetBlocked(
							Diagnostic,
							FString::Printf(
								TEXT("closest boundary approach %.0f cm does not narrow below junction edge %.0f cm"),
								NoseGap,
								JunctionEdgeWidth));
						continue;
					}
				}

				Diagnostic.SourceNose = SourceNose;
				Diagnostic.DestinationNose = DestinationNose;
				Diagnostic.bHasNosePair = true;
				Diagnostic.bHasIntersection = true;
				Diagnostic.Intersection = (SourceNose + DestinationNose) * 0.5;
				Diagnostic.LabelLocation = Diagnostic.Intersection;
				++Diagnostic.RequirementsMet;

				ARoadActor* MarkingRoad = SrcConn != INDEX_NONE ? DstBoundary->GetRoad() : SrcBoundary->GetRoad();
				if (!MarkingRoad)
				{
					SetBlocked(Diagnostic, TEXT("marking owner road is missing"));
					continue;
				}
				auto PrepareBoundary = [&](URoadBoundary* Boundary, double& NoseDist)->double
				{
					const double CurveStart = Boundary->Curve.Points[0].Dist;
					const double CurveEnd = Boundary->Curve.Points.Last().Dist;
					const double Inset = FMath::Min(1.0, (CurveEnd - CurveStart) * 0.1);
					NoseDist = FMath::Clamp(NoseDist, CurveStart + Inset, CurveEnd - Inset);
					const bool bNoseAtEnd = NoseDist > (CurveStart + CurveEnd) * 0.5;
					if (Boundary->Segments.Num() < 2)
						Boundary->AddSegment(NoseDist);
					else
						Boundary->Segments[1].Dist = NoseDist;
					// Segmenting at the gore nose is geometric. Do not replace an authored
					// junction marking with DefaultDashStyle/DefaultSolidStyle here.
					return bNoseAtEnd ? CurveStart : CurveEnd;
				};

				const double SrcBaseDist = PrepareBoundary(SrcBoundary, Dist1);
				const double DstBaseDist = PrepareBoundary(DstBoundary, Dist2);
				FPolyline SrcCurve = SrcBoundary->Curve.SubCurve(Dist1, SrcBaseDist);
				FPolyline DstCurve = DstBoundary->Curve.SubCurve(DstBaseDist, Dist2);
				FPolyline EffectiveCornerCurve = CornerBoundary->Curve;
				if (!HasUsableCurve(CornerBoundary))
				{
					EffectiveCornerCurve.Points.Empty();
					EffectiveCornerCurve.AddPoint(SrcBoundary->GetPos(SrcBaseDist), 0);
					EffectiveCornerCurve.AddPoint(DstBoundary->GetPos(DstBaseDist), 0);
					Diagnostic.CornerBoundary = EffectiveCornerCurve;
				}

				if (SrcCurve.Points.Num() < 2 || DstCurve.Points.Num() < 2)
				{
					SetBlocked(Diagnostic, TEXT("split boundary subcurve is too short to form a gore"));
					continue;
				}

				// Construct one connected world-space wedge.  Its single nose is the
				// closest-approach midpoint, and the opposite/wide end is the junction
				// boundary selected by the original RoadBuilder topology.
				TArray<FVector> Polygon;
				Polygon.Add(Diagnostic.Intersection);
				AppendConnectedPolyline(Polygon, SrcCurve, true, false);
				AppendConnectedPolyline(Polygon, EffectiveCornerCurve, true, false);
				AppendConnectedPolyline(Polygon, DstCurve, true, true);
				if (Polygon.Num() > 1 && Polygon.Last().Equals(Polygon[0], 1.0))
					Polygon.Pop();

				Diagnostic.PolygonPointCount = Polygon.Num();
				if (Polygon.Num() < 3 || PolygonArea2D(Polygon) < 100.0)
				{
					SetBlocked(
						Diagnostic,
						FString::Printf(TEXT("generated wedge is degenerate (%d points)"), Polygon.Num()));
					continue;
				}

				UMarkingCurve* Marking = MarkingRoad->GetGeneratedMarkingCurve(FillStyle);
				if (!Marking)
				{
					Marking = MarkingRoad->AddMarkingCurve(true);
					Marking->FillStyle = FillStyle;
				}
				Marking->SetGeneratedWorldPoints(Polygon);
				Diagnostic.Marking = Marking;
				const FVector JunctionEdgeMidpoint =
					(EffectiveCornerCurve.Points[0].Pos + EffectiveCornerCurve.Points.Last().Pos) * 0.5;
				FVector Dir = (JunctionEdgeMidpoint - Diagnostic.Intersection).GetSafeNormal2D();
				if (Dir.IsNearlyZero())
				{
					const FVector SrcDir = (SrcCurve.Points.Last().Pos - SrcCurve.Points[0].Pos).GetSafeNormal2D();
					const FVector DstDir = (DstCurve.Points[0].Pos - DstCurve.Points.Last().Pos).GetSafeNormal2D();
					Dir = (SrcDir + DstDir).GetSafeNormal2D();
				}
				Marking->Orientation = FMath::RadiansToDegrees(FMath::Atan2(-Dir.X, Dir.Y));

				++Diagnostic.RequirementsMet;
				Diagnostic.State = bExactIntersection
					? EGoreDiagnosticState::ReadyExactIntersection
					: EGoreDiagnosticState::ReadyNoseGapFallback;
				Diagnostic.Status = bExactIntersection
					? FString::Printf(TEXT("READY %d/%d | exact boundary crossing | %d wedge points"), Diagnostic.RequirementsMet, FGoreDiagnostic::RequirementCount, Polygon.Num())
					: FString::Printf(
						TEXT("READY %d/%d | closest-approach nose %.0f cm < junction edge %.0f cm | %d wedge points"),
						Diagnostic.RequirementsMet,
						FGoreDiagnostic::RequirementCount,
						NoseGap,
						JunctionEdgeWidth,
						Polygon.Num());
			}
		}
	}
}

bool AJunctionActor::Contains(ARoadActor* Road, double Dist)
{
	return GetSlot(Road, Dist).IsValid();
}

void AJunctionActor::FixHeight(FPolyline& Polyline)
{
	FVector Delta(0, 0, 10000);
	UStaticMeshComponent* MC = Cast<UStaticMeshComponent>(RootComponent);
	for (FPolyPoint& Point : Polyline.Points)
	{
		FHitResult Hit;
		if (MC->LineTraceComponent(Hit, Point.Pos + Delta, Point.Pos - Delta, FCollisionQueryParams()))
			Point.Pos = Hit.Location;
	}
}

void AJunctionActor::FixHeight(TArray<FVector>& Points)
{
	FVector Delta(0, 0, 10000);
	UStaticMeshComponent* MC = Cast<UStaticMeshComponent>(RootComponent);
	for (FVector& Point : Points)
	{
		FHitResult Hit;
		if (MC->LineTraceComponent(Hit, Point + Delta, Point - Delta, FCollisionQueryParams()))
			Point = Hit.Location;
	}
}

void AJunctionActor::UpdateCorner(FJunctionGate& SrcGate, double SrcDist, FJunctionGate& DstGate, double DstDist)
{
	FJunctionLink& Corner = SrcGate.Links[CornerIndex];
	int SrcSide = SrcGate.Sign > 0 ? 1 : 0;
	int DstSide = DstGate.Sign > 0 ? 0 : 1;
	SrcGate.CornerDists[SrcSide] = SrcDist;
	DstGate.CornerDists[DstSide] = DstDist;
	bool SrcRamp = SrcGate.IsRampOf(DstGate);
	bool DstRamp = DstGate.IsRampOf(SrcGate);
	if (SrcRamp || DstRamp)
	{
		Corner.Radius = 0;
		SrcGate.CutDists[SrcSide] = SrcDist;
		DstGate.CutDists[DstSide] = DstDist;
		return;
	}
	URoadBoundary* SrcBoundary = SrcGate.Road->GetRoadEdge(SrcSide);
	URoadBoundary* DstBoundary = DstGate.Road->GetRoadEdge(DstSide);
	double MinSize = 0;
	double MaxSize = FMath::Min(FMath::Min(SrcGate.Sign > 0 ? SrcBoundary->Length() - SrcDist : SrcDist, DstGate.Sign > 0 ? DstBoundary->Length() - DstDist : DstDist), 12800);
	double Size = (MinSize + MaxSize) / 2;
	double R = 0;
	for (int i = 0; i < 100; i++)
	{
		SrcGate.CutDists[SrcSide] = SrcDist + Size * SrcGate.Sign;
		DstGate.CutDists[DstSide] = DstDist + Size * DstGate.Sign;
		FVector SrcPos = SrcBoundary->GetPos(SrcGate.CutDists[SrcSide]);
		FVector SrcDir = SrcBoundary->GetDir(SrcGate.CutDists[SrcSide]) * (-SrcGate.Sign);
		FVector DstPos = DstBoundary->GetPos(DstGate.CutDists[DstSide]);
		FVector DstDir = DstBoundary->GetDir(DstGate.CutDists[DstSide]) * (DstGate.Sign);
		double Dist1, Dist2;
		if (DoLinesIntersect((const FVector2D&)SrcPos, (const FVector2D&)SrcDir, (const FVector2D&)DstPos, -(const FVector2D&)DstDir, Dist1, Dist2))
		{
			double Dist = FMath::Min(Dist1, Dist2);
			double Diff = WrapRadian(FMath::Atan2(DstDir.Y, DstDir.X) - FMath::Atan2(SrcDir.Y, SrcDir.X));
			double Tan = FMath::Abs(FMath::Tan(Diff / 2));
			if (FMath::IsNearlyZero(Tan))
				return;
			R = Dist / Tan;
			if (FMath::IsNearlyEqual(R, Corner.Radius))
				break;
			if (R < Corner.Radius)
			{
				MinSize = Size;
				Size = (MinSize + MaxSize) / 2;
			}
			else
			{
				MaxSize = Size;
				Size = (MinSize + MaxSize) / 2;
			}
		}
		else
		{
			MaxSize = Size;
			Size = (MinSize + MaxSize) / 2;
		}
	}
	Corner.Radius = R;
}

FJunctionGate& AJunctionActor::AddGate(ARoadActor* Road, double Dist, double Sign)
{
	FJunctionGate& Gate = Gates[Gates.AddDefaulted()];
	Gate.Road = Road;
	Gate.InitDist = Gate.Dist = Dist;
	Gate.CornerDists[0] = Gate.CornerDists[1] = Dist;
	Gate.CutDists[0] = Gate.CutDists[1] = Dist;
	Gate.Radian = 0;
	Gate.Sign = Sign;
	return Gate;
}

int AJunctionActor::GetGate(const FVector& Pos)
{
	for (int i = 0; i < Gates.Num(); i++)
	{
		FVector2D UV = Gates[i].Road->GetUV(Pos);
		if (FMath::IsNearlyEqual(Gates[i].Dist, UV.X))
			return i;
	}
	return INDEX_NONE;
}

int AJunctionActor::GetRampConnection(FJunctionGate& Gate)
{
	ARoadActor* Parent = nullptr;
	double Sign = 1;
	if (Gate.Sign > 0)
	{
		if (Gate.Road->ConnectedParents[0] && GetScene()->GetJunctionSlots(Gate.Road)[0].Junction == this)
		{
			if (Gate.Road->ConnectedParents[0]->ConnectedParents[1] != Gate.Road)
			{
				Parent = Gate.Road->ConnectedParents[0];
				Sign = Parent->GetConnectedChild(Gate.Road, 0).ConnectionSign(Parent);
			}
		}
	}
	else
	{
		if (Gate.Road->ConnectedParents[1] && GetScene()->GetJunctionSlots(Gate.Road).Last().Junction == this)
		{
			if (Gate.Road->ConnectedParents[1]->ConnectedParents[0] != Gate.Road)
			{
				Parent = Gate.Road->ConnectedParents[1];
				Sign = Parent->GetConnectedChild(Gate.Road, 1).ConnectionSign(Parent);
			}
		}
	}
	if (Parent)
	{
		for (int i = 0; i < Gates.Num(); i++)
		{
			FJunctionGate& G = Gates[i];
			if (G.Road == Parent && G.Sign * Gate.Sign * Sign < 0)
				return i;
		}
	}
	return INDEX_NONE;
}

FJunctionSlot AJunctionActor::GetSlot(ARoadActor* Road, double Dist)
{
	const double MaxEditingDelta = 1600.0;
	TArray<FJunctionSlot> Slots = GetSlots(Road);
	for (FJunctionSlot& Slot : Slots)
		if (Slot.InputDist() - MaxEditingDelta <= Dist && Slot.OutputDist() + MaxEditingDelta >= Dist)
			return Slot;
	return FJunctionSlot();
}

TArray<FJunctionSlot> AJunctionActor::GetSlots(ARoadActor* Road)
{
	TArray<FJunctionSlot> Slots;
	TArray<FJunctionGate*> Sorts;
	for (FJunctionGate& Gate : Gates)
		if (Gate.Road == Road)
			Sorts.Add(&Gate);
	Sorts.Sort([](const FJunctionGate& A, const FJunctionGate& B)
	{
		return A.Dist < B.Dist;
	});
	for (int i = 0; i < Sorts.Num();)
	{
		if (i + 1 < Sorts.Num())
		{
			if (Sorts[i]->IsInput() && Sorts[i + 1]->IsOutput())
				Slots.Add({ this, Road, Sorts[i]->InitDist, Sorts[i + 1]->InitDist });
			else
				Slots.Add({ this, Road, Sorts[i + 1]->InitDist, Sorts[i]->InitDist });
			i += 2;
			continue;
		}
		if (Sorts[i]->IsInput())
			Slots.Add({ this, Road, Sorts[i]->InitDist, MAX_dbl });
		else
			Slots.Add({ this, Road, -MAX_dbl, Sorts[i]->InitDist });
		i++;
	}
	return MoveTemp(Slots);
}

bool AJunctionActor::IsTurnAllowed(int FromGate, int ToGate) const
{
	for (const FTurnRestriction& Restriction : TurnRestrictions)
	{
		if (Restriction.FromGateIndex == FromGate && Restriction.ToGateIndex == ToGate)
		{
			return false;
		}
	}
	return true;
}

void AJunctionActor::AddTurnRestriction(int FromGate, int ToGate)
{
	if (IsTurnAllowed(FromGate, ToGate))
	{
		FTurnRestriction& Restriction = TurnRestrictions[TurnRestrictions.AddDefaulted()];
		Restriction.FromGateIndex = FromGate;
		Restriction.ToGateIndex = ToGate;
	}
}

void AJunctionActor::RemoveTurnRestriction(int FromGate, int ToGate)
{
	for (int Index = 0; Index < TurnRestrictions.Num(); ++Index)
	{
		if (TurnRestrictions[Index].FromGateIndex == FromGate && TurnRestrictions[Index].ToGateIndex == ToGate)
		{
			TurnRestrictions.RemoveAt(Index);
			return;
		}
	}
}

ARoadScene* AJunctionActor::GetScene()
{
	return Cast<ARoadScene>(GetAttachParentActor());
}

void AJunctionActor::ExportXodr(FXmlNode* XmlNode, int& RoadId, int& ObjectId)
{
	int JunctionId = RoadId++;
	FXmlNode* JunctionNode = XmlNode_CreateChild(XmlNode, TEXT("junction"));
	XmlNode_AddAttribute(JunctionNode, TEXT("id"), JunctionId);
	for (FJunctionGate& Gate : Gates)
		for (FJunctionLink& Link : Gate.Links)
			if (Link.Road)
				Link.Road->ExportXodr(XmlNode, RoadId, ObjectId, JunctionId);
}

void AJunctionActor::Destroyed()
{
	CleanupTrafficControl();
	CleanupTurnArrows();
	for (FJunctionGate& Gate : Gates)
		Gate.Clear();
	AActor::Destroyed();
}

void AJunctionActor::CleanupTrafficControl()
{
	for (ATrafficLightActor* Light : TrafficLights)
		if (Light)
			Light->Destroy();
	TrafficLights.Empty();
	for (ATrafficSignActor* Sign : TrafficSigns)
		if (Sign)
			Sign->Destroy();
	TrafficSigns.Empty();
}

void AJunctionActor::CleanupTurnArrows()
{
	for (ATurnArrowActor* Arrow : TurnArrows)
		if (Arrow)
			Arrow->Destroy();
	TurnArrows.Empty();
}

void AJunctionActor::GenerateTrafficControl()
{
	CleanupTrafficControl();
	if (TrafficControlType == ETrafficControlType::None)
		return;

	UWorld* World = GetWorld();
	if (!World)
		return;

	USettings_Global* Settings = GetMutableDefault<USettings_Global>();
	ARoadScene* Scene = GetScene();

	for (int i = 0; i < Gates.Num(); i++)
	{
		FJunctionGate& Gate = Gates[i];
		if (!Gate.IsInput())
			continue;

		const int ApproachSide = Scene
			? Scene->GetTrafficSideForDirection(-Gate.Sign)
			: (Gate.Sign > 0 ? RD_LEFT : RD_RIGHT);
		TArray<URoadLane*> ApproachLanes = Gate.Road->GetLanes(ApproachSide, { ELaneType::Driving });
		FVector Pos = FVector(Gate.Road->GetPos(Gate.Dist), Gate.Road->GetHeight(Gate.Dist));
		if (ApproachLanes.Num() > 0)
		{
			Pos = FVector::ZeroVector;
			for (URoadLane* Lane : ApproachLanes)
			{
				Pos += (Lane->LeftBoundary->GetPos(Gate.Dist) + Lane->RightBoundary->GetPos(Gate.Dist)) * 0.5;
			}
			Pos /= ApproachLanes.Num();
		}
		FVector Dir = Gate.Road->GetDir(Gate.Dist) * Gate.Sign;
		FRotator Rot = Dir.Rotation();
		FVector SpawnPos = Pos;

		if (TrafficControlType == ETrafficControlType::TrafficLight)
		{
			ATrafficLightActor* Light = World->SpawnActor<ATrafficLightActor>(SpawnPos, Rot);
			if (Light)
			{
				Light->GateIndex = i;
				Light->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
				if (UStaticMesh* Mesh = Settings->TrafficLightMesh.LoadSynchronous())
					Light->PoleMesh->SetStaticMesh(Mesh);
				// Offset phases so opposing traffic alternates
				int InputCount = 0;
				for (int j = 0; j < i; j++)
					if (Gates[j].IsInput())
						InputCount++;
				float TotalCycle = Light->GreenDuration + Light->YellowDuration + Light->RedDuration;
				Light->PhaseOffset = (InputCount % 2 == 0) ? 0.0f : Light->GreenDuration + Light->YellowDuration;
				TrafficLights.Add(Light);
			}
		}
		else if (TrafficControlType == ETrafficControlType::StopSign || TrafficControlType == ETrafficControlType::YieldSign)
		{
			ATrafficSignActor* Sign = World->SpawnActor<ATrafficSignActor>(SpawnPos, Rot);
			if (Sign)
			{
				Sign->SignType = TrafficControlType;
				Sign->GateIndex = i;
				Sign->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
				UStaticMesh* Mesh = (TrafficControlType == ETrafficControlType::StopSign)
					? Settings->StopSignMesh.LoadSynchronous()
					: Settings->YieldSignMesh.LoadSynchronous();
				if (Mesh)
					Sign->SignMesh->SetStaticMesh(Mesh);
				TrafficSigns.Add(Sign);
			}
		}
	}
}

void AJunctionActor::GenerateTurnArrows()
{
	CleanupTurnArrows();

	USettings_Global* Settings = GetMutableDefault<USettings_Global>();
	if (!Settings->AutoGenerateTurnArrows)
		return;

	UStaticMesh* ArrowMesh = Settings->TurnArrowMesh.LoadSynchronous();
	UWorld* World = GetWorld();
	if (!World)
		return;
	ARoadScene* Scene = GetScene();

	for (int i = 0; i < Gates.Num(); i++)
	{
		FJunctionGate& Gate = Gates[i];
		if (!Gate.IsInput())
			continue;

		const int ApproachSide = Scene
			? Scene->GetTrafficSideForDirection(-Gate.Sign)
			: (Gate.Sign > 0 ? RD_LEFT : RD_RIGHT);
		TArray<URoadLane*> ApproachLanes = Gate.Road->GetLanes(ApproachSide, { ELaneType::Driving });
		if (ApproachLanes.Num() == 0)
			continue;

		// For each input gate, determine which output gates it can reach
		TArray<int> Destinations;
		for (int j = 0; j < Gates.Num(); j++)
		{
			if (Gates[j].IsOutput() && IsTurnAllowed(i, j))
				Destinations.Add(j);
		}

		if (Destinations.Num() == 0)
			continue;

		// Determine arrow type based on angular relationship
		FVector InputDir = Gate.Road->GetDir(Gate.Dist) * (-Gate.Sign);
		double InputAngle = FMath::Atan2(InputDir.Y, InputDir.X);

		for (int d = 0; d < Destinations.Num(); d++)
		{
			FJunctionGate& OutGate = Gates[Destinations[d]];
			FVector OutDir = OutGate.Road->GetDir(OutGate.Dist) * OutGate.Sign;
			double OutAngle = FMath::Atan2(OutDir.Y, OutDir.X);
			double AngleDiff = FMath::FindDeltaAngleRadians(InputAngle, OutAngle);

			ETurnArrowType ArrowType = ETurnArrowType::Through;
			if (AngleDiff > DOUBLE_PI / 6.0)
				ArrowType = ETurnArrowType::Left;
			else if (AngleDiff < -DOUBLE_PI / 6.0)
				ArrowType = ETurnArrowType::Right;

			// Place arrow on the road surface approaching the junction
			double ArrowDist = Gate.Dist + Gate.Sign * 500.0; // 5m before the stop line
			URoadLane* ApproachLane = ApproachLanes[FMath::Min(d, ApproachLanes.Num() - 1)];
			FVector ArrowPos = (ApproachLane->LeftBoundary->GetPos(ArrowDist) + ApproachLane->RightBoundary->GetPos(ArrowDist)) * 0.5;
			FRotator ArrowRot = (Gate.Road->GetDir(ArrowDist) * (-Gate.Sign)).Rotation();

			ATurnArrowActor* Arrow = World->SpawnActor<ATurnArrowActor>(ArrowPos, ArrowRot);
			if (Arrow)
			{
				Arrow->ArrowType = ArrowType;
				Arrow->GateIndex = i;
				Arrow->LaneIndex = d;
				Arrow->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
				if (ArrowMesh)
					Arrow->ArrowMesh->SetStaticMesh(ArrowMesh);
				TurnArrows.Add(Arrow);
			}
		}
	}
}

ARoadScene::ARoadScene(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	Octree = TOctree2<FRoadOctreeElement, FRoadOctreeSemantics>(FVector::ZeroVector, HALF_WORLD_MAX);
}

ERoadTrafficHandedness ARoadScene::GetResolvedTrafficHandedness() const
{
	const USettings_Global* Settings = GetDefault<USettings_Global>();
	const ERoadTrafficHandedness ProjectHandedness = Settings
		? Settings->DefaultTrafficHandedness
		: ERoadTrafficHandedness::RightHandTraffic;
	return ResolveTrafficHandedness(bOverrideTrafficHandedness, TrafficHandedness, ProjectHandedness);
}

ERoadTrafficHandedness ARoadScene::ResolveTrafficHandedness(
	bool bUseSceneOverride,
	ERoadTrafficHandedness SceneHandedness,
	ERoadTrafficHandedness ProjectHandedness)
{
	return bUseSceneOverride ? SceneHandedness : ProjectHandedness;
}

int32 ARoadScene::ResolveTrafficSide(ERoadTrafficHandedness Handedness, double DirectionSign)
{
	const bool bForward = DirectionSign >= 0.0;
	if (Handedness == ERoadTrafficHandedness::LeftHandTraffic)
	{
		return bForward ? RD_LEFT : RD_RIGHT;
	}
	return bForward ? RD_RIGHT : RD_LEFT;
}

double ARoadScene::ResolveTrafficDirectionSign(ERoadTrafficHandedness Handedness, int32 Side)
{
	return Side == ResolveTrafficSide(Handedness, 1.0) ? 1.0 : -1.0;
}

int32 ARoadScene::GetForwardTrafficSide() const
{
	return ResolveTrafficSide(GetResolvedTrafficHandedness(), 1.0);
}

int32 ARoadScene::GetReverseTrafficSide() const
{
	return ResolveTrafficSide(GetResolvedTrafficHandedness(), -1.0);
}

int32 ARoadScene::GetTrafficSideForDirection(double DirectionSign) const
{
	return ResolveTrafficSide(GetResolvedTrafficHandedness(), DirectionSign);
}

double ARoadScene::GetTrafficDirectionSignForSide(int32 Side) const
{
	return ResolveTrafficDirectionSign(GetResolvedTrafficHandedness(), Side);
}

FString ARoadScene::GetOpenDriveTrafficRule() const
{
	return ToOpenDriveTrafficRule(GetResolvedTrafficHandedness());
}

FString ARoadScene::ToOpenDriveTrafficRule(ERoadTrafficHandedness Handedness)
{
	return Handedness == ERoadTrafficHandedness::LeftHandTraffic ? TEXT("LHT") : TEXT("RHT");
}

bool ARoadScene::IsTrafficHandednessApplied() const
{
	return bTrafficHandednessInitialized && LastBuiltTrafficHandedness == GetResolvedTrafficHandedness();
}

void ARoadScene::ApplyTrafficHandedness()
{
	Modify();
	Rebuild();
}

void ARoadScene::ResetTrafficDerivedData()
{
	CleanupMassGraph();
	for (AJunctionActor* Junction : Junctions)
	{
		if (!IsValid(Junction))
		{
			continue;
		}

		Junction->Modify();
		Junction->CleanupTrafficControl();
		Junction->CleanupTurnArrows();
		for (FJunctionGate& Gate : Junction->Gates)
		{
			Gate.Clear();
		}
	}
}

ARoadActor* ARoadScene::AddRoad()
{
	ARoadActor* Road = GetWorld()->SpawnActor<ARoadActor>();
	Road->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
	Roads.Add(Road);
	return Road;
}

ARoadActor* ARoadScene::RuntimeCreateRoad(const TArray<FVector>& WorldPoints, URoadStyle* Style, bool bRebuildScene)
{
	if (WorldPoints.Num() < 2)
	{
		return nullptr;
	}

	ARoadActor* Road = AddRoad(Style, WorldPoints[0].Z);
	if (!Road || !Road->RuntimeSetRoadPoints(WorldPoints, false))
	{
		if (Road)
		{
			DestroyRoad(Road);
		}
		return nullptr;
	}

	if (bRebuildScene)
	{
		Rebuild();
	}

	return Road;
}

bool ARoadScene::RuntimeDestroyRoad(ARoadActor* Road, bool bRebuildScene)
{
	if (!IsValid(Road) || !Roads.Contains(Road))
	{
		return false;
	}

	DestroyRoad(Road);
	if (bRebuildScene)
	{
		Rebuild();
	}
	return true;
}

void ARoadScene::RuntimeRebuild()
{
	RemoveInvalidReferences();
	for (ARoadActor* Road : Roads)
	{
		if (IsValid(Road))
		{
			Road->RuntimeRefresh(false);
		}
	}
	Rebuild();
}

ARoadActor* ARoadScene::AddRoad(URoadStyle* Style, double Height)
{
	ARoadActor* Road = AddRoad();
	Road->InitWithStyle(Style, Height);
	return Road;
}

ARoadActor* ARoadScene::DuplicateRoad(ARoadActor* Source)
{
	ULevel* Level = GetWorld()->GetCurrentLevel();
	ARoadActor* Road = CastChecked<ARoadActor>(StaticDuplicateObject(Source, Level));
	Road->RegisterAllComponents();
	Road->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
	Roads.Add(Road);
#if WITH_EDITOR
	Level->AddLoadedActor(Road);
	GEditor->BroadcastLevelActorAdded(Road);
#endif
	return Road;
}

ARoadActor* ARoadScene::PickRoad(const FVector& Pos, ARoadActor* IgnoredRoad)
{
	RemoveInvalidReferences();
	double BaseOffset = 0;
	FVector2D BestUV(0, MAX_dbl);
	ARoadActor* Result = nullptr;
	Octree.FindElementsWithBoundsTest(FBoxCenterAndExtent(Pos, FVector(DefaultJunctionExtent, DefaultJunctionExtent, DefaultJunctionExtent)), [&](const FRoadOctreeElement& Element)
	{
		ARoadActor* Road = Element.Boundary ? Element.Boundary->GetRoad() : nullptr;
		if (IsValid(Road) && Road != IgnoredRoad)
		{
			FVector2D UV = Element.Boundary->Curve.GetUV((const FVector2D&)Pos, Element.Index);
			if (UV.X >= 0 && UV.X <= Road->Length() && FMath::Abs(BestUV.Y) > FMath::Abs(UV.Y))
			{
				BestUV = UV;
				Result = Road;
				BaseOffset = Element.Boundary->GetOffset(UV.X);
			}
		}
	});
	if (Result && Result->GetRoadEdge(0)->GetOffset(BestUV.X) >= (BaseOffset + BestUV.Y) && Result->GetRoadEdge(1)->GetOffset(BestUV.X) <= (BaseOffset + BestUV.Y))
		return Result;
	return nullptr;
}

AGroundActor* ARoadScene::AddGround(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots, const TArray<FGroundPoint>& Points)
{
	for (AGroundActor* Ground : Grounds)
	{
		if (Ground->Contains(RoadSlots, Points))
		{
			Ground->Renew();
			return Ground;
		}
	}
	AGroundActor* Ground = GetWorld()->SpawnActor<AGroundActor>();
	Ground->Points = Points;
	Ground->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
	Grounds.Add(Ground);
	return Ground;
}

AJunctionActor* ARoadScene::AddJunction(ARoadActor* R0, double D0, ARoadActor* R1, double D1)
{
	for (AJunctionActor* Junction : Junctions)
	{
		bool C0 = Junction->Contains(R0, D0);
		bool C1 = Junction->Contains(R1, D1);
		if (C0 && C1)
		{
			Junction->AddRoad(R0, D0);
			Junction->AddRoad(R1, D1);
			return Junction;
		}
	}
	AJunctionActor* Junction = GetWorld()->SpawnActor<AJunctionActor>();
	Junction->AddRoad(R0, D0);
	Junction->AddRoad(R1, D1);
	Junction->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
	Junctions.Add(Junction);
	return Junction;
}

#if 0
UMarkingCurve* ARoadScene::GetMarkingCurve(TArray<FCurveCoordinate>& Coordinates)
{
	for (URoadMarking* Marking : Markings)
	{
		if (UMarkingCurve* MarkingCurve = Cast<UMarkingCurve>(Marking))
		{
			if (MarkingCurve->Match(Coordinates))
				return MarkingCurve;
		}
	}
	return nullptr;
}

TArray<URoadMarking*> ARoadScene::GetMarkings(ARoadActor* Road)
{
	TArray<URoadMarking*> Results;
	for (URoadMarking* Marking : Markings)
	{
		if (Marking->GetRoad() == Road)
			Results.Add(Marking);
	}
	return MoveTemp(Results);
}
#endif
TMap<ARoadActor*, TArray<FJunctionSlot>> ARoadScene::GetAllJunctionSlots()
{
	RemoveInvalidReferences();
	TMap<ARoadActor*, TArray<FJunctionSlot>> RoadSlots;
	for (ARoadActor* Road : Roads)
	{
		if (IsValid(Road))
		{
			RoadSlots.Add(Road, GetJunctionSlots(Road));
		}
	}
	return MoveTemp(RoadSlots);
}

TArray<FJunctionSlot> ARoadScene::GetJunctionSlots(ARoadActor* Road)
{
	TArray<FJunctionSlot> Results;
	if (!IsValid(Road))
	{
		return MoveTemp(Results);
	}

	for (AJunctionActor* Junction : Junctions)
	{
		if (IsValid(Junction))
		{
			Results.Append(Junction->GetSlots(Road));
		}
	}
	Results.Sort();
	return MoveTemp(Results);
}
/*
FVector2D ARoadScene::GetRoadUV(ARoadActor* SelectedRoad, const FVector& Pos)
{
	double MinDist = MAX_FLT;
	double BestU = 0;
	Octree.FindElementsWithBoundsTest(FBoxCenterAndExtent(Pos, FVector(DefaultJunctionExtent, DefaultJunctionExtent, 10000)), [&](const FRoadOctreeElement& Element)
	{
		ARoadActor* Road = Element.Boundary->GetRoad();
		if (SelectedRoad == Road && Element.Boundary == Road->BaseCurve)
			Element.Boundary->Curve.GetUV((const FVector2D&)Pos, Element.Index, MinDist, BestU);
	});
	return FVector2D(BestU, MinDist);
}
*/
void ARoadScene::Rebuild()
{
	RemoveInvalidReferences();
	for (ARoadActor* Road : Roads)
	{
		if (IsValid(Road) && !Road->CrossRoadSceneConnections.IsEmpty())
		{
			Road->UpdateCurve();
		}
	}
	const ERoadTrafficHandedness ResolvedTrafficHandedness = GetResolvedTrafficHandedness();
	if (!bTrafficHandednessInitialized || LastBuiltTrafficHandedness != ResolvedTrafficHandedness)
	{
		ResetTrafficDerivedData();
	}
	Octree = TOctree2<FRoadOctreeElement, FRoadOctreeSemantics>(FVector::ZeroVector, HALF_WORLD_MAX);
	for (ARoadActor* Road : Roads)
	{
		OctreeAddRoad(Road);
	}

	for (AJunctionActor* Junction : Junctions)
	{
		if (!IsValid(Junction))
		{
			continue;
		}
		Junction->Modify();
		for (FJunctionGate& Gate : Junction->Gates)
			Gate.MarkExpired();
	}
	for (ARoadActor* Road : Roads)
	{
		if (!IsValid(Road) || !Road->BaseCurve)
		{
			continue;
		}
		if (GetMutableDefault<USettings_Global>()->BuildJunctions)
		{
			for (int i = 0; i < Road->BaseCurve->Curve.Points.Num() - 1; i++)
			{
				FRoadOctreeElement Segment(Road->BaseCurve, i);
				FPolyline& C = Road->BaseCurve->Curve;
				FVector StartPos = C.Points[i].Pos;
				double StartDist = C.Points[i].Dist;
				FVector EndPos = C.Points[i + 1].Pos;
				double EndDist = C.Points[i + 1].Dist;
				Octree.FindElementsWithBoundsTest(Segment.GetBounds(), [&](const FRoadOctreeElement& Element)
				{
					if (!Element.Boundary || !IsValid(Element.Boundary->GetRoad()))
					{
						return;
					}
					if (Element.IsBoundary() || Element.Adjacent(Road->BaseCurve, i))
						return;
					double Seg1, Seg2;
					FPolyline& Curve = Element.Boundary->Curve;
					if (DoLineSegmentsIntersect((const FVector2D&)StartPos, (const FVector2D&)EndPos, (const FVector2D&)Curve.Points[Element.Index].Pos, (const FVector2D&)Curve.Points[Element.Index + 1].Pos, Seg1, Seg2))
					{
						double Dist1 = FMath::Lerp(StartDist, EndDist, Seg1);
						double Dist2 = FMath::Lerp(Curve.Points[Element.Index].Dist, Curve.Points[Element.Index + 1].Dist, Seg2);
						AddJunction(Road, Dist1, Element.Boundary->GetRoad(), Dist2);
					}
				});
			}
		}
		for (int i = 0; i < 2; i++)
		{
			if (ARoadActor* Parent = Road->ConnectedParents[i])
			{
				FConnectInfo& Info = Parent->GetConnectedChild(Road, i);
				AddJunction(Parent, Info.UV.X, Road, i ? Road->Length() : 0);
			}
		}
	}
	for (int i = 0; i < Junctions.Num();)
	{
		AJunctionActor* Junction = Junctions[i];
		if (!IsValid(Junction))
		{
			Junctions.RemoveAt(i);
			continue;
		}
		TSet<ARoadActor*> Starts, Ends;
		for (FJunctionGate& Gate : Junction->Gates)
		{
			if (!IsValid(Gate.Road))
			{
				Gate.MarkExpired();
				continue;
			}
			if (FMath::IsNearlyZero(Gate.Dist))
				Starts.Add(Gate.Road);
			if (FMath::IsNearlyEqual(Gate.Dist, Gate.Road->Length()))
				Ends.Add(Gate.Road);
		}
		TSet<ARoadActor*> RemoveRoads = Starts.Intersect(Ends);
		for (int j = 0; j < Junction->Gates.Num();)
		{
			if (RemoveRoads.Contains(Junction->Gates[j].Road))
			{
				Junction->Gates[j].Clear();
				Junction->Gates.RemoveAt(j);
			}
			else if (Junction->Gates[j].IsExpired())
			{
				/*
				for (int k = 0; k < Junction->Gates.Num(); k++)
				{
					if (k != j && Junction->Gates[k].Links.Num())
					{
						Junction->Gates[k].Links[j].Destroy();
						Junction->Gates[k].Links.RemoveAt(j);
					}
				}*/
				Junction->Gates[j].Clear();
				Junction->Gates.RemoveAt(j);
			}
			else
				j++;
		}
		if (!Junction->Gates.Num())
		{
			Junction->Destroy();
			Junctions.RemoveAt(i);
		}
		else
			i++;
	}
	ForEachAttachedActors([&](AActor* Actor)->bool
	{
		if (!IsValid(Actor))
		{
			return true;
		}
		if (Actor->IsA<AJunctionActor>())
		{
			if (!Junctions.Contains(Actor))
				Actor->Destroy();
		}
		if (Actor->IsA<ARoadActor>())
		{
			if (!Roads.Contains(Actor))
				Actor->Destroy();
		}
		return true;
	});
	TMap<ARoadActor*, TArray<FJunctionSlot>> RoadSlots;
	auto ReplaceJunctionWith = [&](AJunctionActor* Junction, AJunctionActor* With = nullptr, ARoadActor* CurrentRoad = nullptr)
	{
		for (auto& KV : RoadSlots)
		{
			if (KV.Key == CurrentRoad)
				continue;
			TArray<FJunctionSlot>& Slots = KV.Value;
			for (int i = 0; i < Slots.Num();)
			{
				if (Slots[i].Junction == Junction)
				{
					if (With)
					{
						Slots[i].Junction = With;
						i++;
					}
					else
						Slots.RemoveAt(i);
				}
				else
					i++;
			}
		}
	};
	while (true)
	{
		RemoveInvalidReferences();
		RoadSlots = GetAllJunctionSlots();
		for (AJunctionActor* Junction : Junctions)
		{
			if (IsValid(Junction))
			{
				Junction->Update(Octree);
			}
		}
		bool ReSolve = false;
		for (auto& Pair : RoadSlots)
		{
			TArray<FJunctionSlot>& Slots = Pair.Value;
			for (int i = 1; i < Slots.Num();)
			{
				if (Slots[i - 1].Junction == Slots[i].Junction)
					Slots.RemoveAt(i);
				else
				{
					double InputDist = Slots[i].InputDist();
					double OutputDist = Slots[i - 1].OutputDist();
					if (OutputDist >= InputDist)
					{
						ReplaceJunctionWith(Slots[i].Junction, Slots[i - 1].Junction, Pair.Key);
						Slots[i - 1].Combine(Slots[i]);
						Slots.RemoveAt(i);
						ReSolve = true;
					}
					else
						i++;
				}
			}
		}
		if (!ReSolve)
			break;
	}
	for (int i = 0; i < Junctions.Num();)
	{
		AJunctionActor* Junction = Junctions[i];
		if (!IsValid(Junction))
		{
			Junctions.RemoveAt(i);
		}
		else if (Junction->Gates.Num() < 3)
		{
			ReplaceJunctionWith(Junction);
			Junctions[i]->Destroy();
			Junctions.RemoveAt(i);
		}
		else
			i++;
	}
	RemoveInvalidReferences();
	for (AJunctionActor* Junction : Junctions)
	{
		if (IsValid(Junction))
		{
			Junction->Build();
		}
	}
	USettings_Global* GlobalSettings = GetMutableDefault<USettings_Global>();
	for (AJunctionActor* Junction : Junctions)
	{
		if (IsValid(Junction))
		{
			Junction->GenerateTrafficControl();
			if (GlobalSettings->AutoGenerateTurnArrows)
			{
				Junction->GenerateTurnArrows();
			}
		}
	}
	for (ARoadActor* Road : Roads)
	{
		if (IsValid(Road))
		{
			Road->BuildMesh(RoadSlots[Road]);
		}
	}
	GenerateGrounds(RoadSlots);
	for (AGroundActor* Ground : Grounds)
	{
		if (IsValid(Ground))
		{
			Ground->BuildMesh(RoadSlots);
		}
	}
	if (GetMutableDefault<USettings_Global>()->BuildMassGraph)
	{
		GenerateMassGraph(RoadSlots);
	}
	bTrafficHandednessInitialized = true;
	LastBuiltTrafficHandedness = ResolvedTrafficHandedness;
}

void ARoadScene::RemoveInvalidReferences()
{
	Roads.RemoveAllSwap([](ARoadActor* Road)
	{
		return !IsValid(Road);
	});
	Junctions.RemoveAllSwap([](AJunctionActor* Junction)
	{
		return !IsValid(Junction);
	});
	Grounds.RemoveAllSwap([](AGroundActor* Ground)
	{
		return !IsValid(Ground);
	});
}

void ARoadScene::GenerateGrounds(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots)
{
	TSet<FGroundPoint> VisitedGroundPoints;
	TMap<ARoadActor*, int> PrevSlots;
	//Check slot count match
	for (AGroundActor* Ground : Grounds)
	{
		for (FGroundPoint& Point : Ground->Points)
		{
			if (Point.Road)
			{
				int Slot = Point.Index / 2;
				if (int* SlotPtr = PrevSlots.Find(Point.Road))
					*SlotPtr = FMath::Max(*SlotPtr, Slot);
				else
					PrevSlots.Add(Point.Road, Slot);
			}
		}
	}
	for (int i = 0; i < Grounds.Num();)
	{
		bool Delete = false;
		AGroundActor* Ground = Grounds[i];
		for (FGroundPoint& Point : Ground->Points)
		{
			if (Point.Road && (!RoadSlots.Contains(Point.Road) || PrevSlots[Point.Road] > RoadSlots[Point.Road].Num()))
			{
				Delete = true;
				break;
			}
		}
		if (Delete)
		{
			Ground->Destroy();
			Grounds.RemoveAt(i);
		}
		else
		{
			Ground->Modify();
			Ground->MarkExpired();
			i++;
		}
	}
	for (ARoadActor* Road : Roads)
	{
		if (!Road->bHasGround || FMath::IsNearlyZero(Road->Length()))
			continue;
		TArray<FJunctionSlot>& Slots = RoadSlots[Road];
		for (int Side = 0; Side < 2; Side++)
		{
			for (int i = 0; i <= Slots.Num(); i++)
			{
				if (Slots.Num())
				{
					if (i == 0 && !Slots[i].HasInput() || i == Slots.Num() && !Slots[i - 1].HasOutput())
						continue;
				}
				for (int j = 0; j < 2; j++)
				{
					FGroundPoint Point = { Road, Side, i*2+j };
					if (VisitedGroundPoints.Contains(Point))
						continue;
					TArray<FGroundPoint> Points = { Point };
					VisitedGroundPoints.Add(Point);
					while (true)
					{
						FGroundPoint Prev = Points[0].PrevPoint(RoadSlots);
						if (!Prev.Road || VisitedGroundPoints.Contains(Prev))
							break;
						Points.Insert(Prev, 0);
						VisitedGroundPoints.Add(Prev);
					}
					while (true)
					{
						FGroundPoint Next = Points.Last().NextPoint(RoadSlots);
						if (!Next.Road || VisitedGroundPoints.Contains(Next))
							break;
						Points.Add(Next);
						VisitedGroundPoints.Add(Next);
					}
					AGroundActor* Ground = AddGround(RoadSlots, Points);
					if (Points.Last().NextPoint(RoadSlots) == Points[0])
						Ground->bClosedLoop = true;
				}
			}
		}
	}
	for (int i = 0; i < Grounds.Num();)
	{
		AGroundActor* Ground = Grounds[i];
		if (Ground->IsExpired())
		{
			Ground->Destroy();
			Grounds.RemoveAt(i);
		}
		else
			i++;
	}
}

void ARoadScene::CleanupMassGraph()
{
	for (AActor* Actor : MassGraphActors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	MassGraphActors.Empty();
}

static AActor* CreateZoneShapeForLane(
	UWorld* World,
	AActor* Parent,
	ARoadActor* Road,
	URoadLane* Lane,
	double StartDist,
	double EndDist)
{
	if (!World || !Road || !Lane || !Lane->LeftBoundary || !Lane->RightBoundary ||
		FMath::IsNearlyEqual(StartDist, EndDist))
	{
		return nullptr;
	}

	AActor* ShapeActor = World->SpawnActor<AActor>();
	if (!ShapeActor)
	{
		return nullptr;
	}

	USceneComponent* Root = NewObject<USceneComponent>(ShapeActor, TEXT("Root"));
	ShapeActor->SetRootComponent(Root);
	Root->RegisterComponent();
	ShapeActor->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);

	UZoneShapeComponent* ShapeComponent = NewObject<UZoneShapeComponent>(ShapeActor, TEXT("ZoneShape"));
	ShapeComponent->SetupAttachment(Root);
	ShapeComponent->RegisterComponent();

	FPolyline& LeftCurve = Lane->LeftBoundary->Curve;
	const bool bReverse = StartDist > EndDist;
	const double ActualStart = bReverse ? EndDist : StartDist;
	const double ActualEnd = bReverse ? StartDist : EndDist;

	TArray<FVector> CenterlinePoints;
	int StartIndex = LeftCurve.GetPoint(ActualStart);
	const int EndIndex = LeftCurve.GetPoint(ActualEnd);
	if (StartIndex >= LeftCurve.Points.Num() - 1)
	{
		StartIndex = LeftCurve.Points.Num() - 2;
	}

	CenterlinePoints.Add(
		(Lane->LeftBoundary->GetPos(ActualStart) + Lane->RightBoundary->GetPos(ActualStart)) * 0.5);
	for (int Index = StartIndex + 1; Index <= EndIndex && Index < LeftCurve.Points.Num(); ++Index)
	{
		const double Distance = LeftCurve.Points[Index].Dist;
		if (Distance > ActualStart && Distance < ActualEnd)
		{
			CenterlinePoints.Add(
				(Lane->LeftBoundary->GetPos(Distance) + Lane->RightBoundary->GetPos(Distance)) * 0.5);
		}
	}
	CenterlinePoints.Add(
		(Lane->LeftBoundary->GetPos(ActualEnd) + Lane->RightBoundary->GetPos(ActualEnd)) * 0.5);

	if (bReverse)
	{
		Algo::Reverse(CenterlinePoints);
	}
	if (CenterlinePoints.Num() < 2)
	{
		ShapeActor->Destroy();
		return nullptr;
	}

	const double LaneWidth = Lane->GetWidth((ActualStart + ActualEnd) * 0.5);
	ShapeComponent->SetShapeType(FZoneShapeType::Spline);
	FZoneLaneProfile Profile;
	Profile.Name = TEXT("RoadBuilderLane");
	FZoneLaneDesc LaneDescription;
	LaneDescription.Width = LaneWidth;
	LaneDescription.Direction = EZoneLaneDirection::Forward;
	Profile.Lanes.Add(LaneDescription);
	ShapeComponent->SetCommonLaneProfile(FZoneLaneProfileRef(Profile));

	TArray<FZoneShapePoint>& ShapePoints = ShapeComponent->GetMutablePoints();
	ShapePoints.Reset();
	ShapePoints.Reserve(CenterlinePoints.Num());
	for (int Index = 0; Index < CenterlinePoints.Num(); ++Index)
	{
		FZoneShapePoint Point(CenterlinePoints[Index]);
		Point.Type = (Index == 0 || Index == CenterlinePoints.Num() - 1)
			? FZoneShapePointType::Sharp
			: FZoneShapePointType::AutoBezier;
		Point.InnerTurnRadius = 0.0f;
		ShapePoints.Add(Point);
	}

	return ShapeActor;
}

void ARoadScene::GenerateMassGraph(TMap<ARoadActor*, TArray<FJunctionSlot>>& RoadSlots)
{
	CleanupMassGraph();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (ARoadActor* Road : Roads)
	{
		if (!IsValid(Road) || FMath::IsNearlyZero(Road->Length()))
		{
			continue;
		}
		TArray<FJunctionSlot>& Slots = RoadSlots.FindOrAdd(Road);
		for (URoadLane* Lane : Road->Lanes)
		{
			if (!Lane)
			{
				continue;
			}
			bool bHasDrivingSegment = false;
			for (const FLaneSegment& Segment : Lane->Segments)
			{
				if (Segment.LaneType == ELaneType::Driving)
				{
					bHasDrivingSegment = true;
					break;
				}
			}
			if (!bHasDrivingSegment)
			{
				continue;
			}

			const int Side = Lane->GetSide();
			double PreviousDistance = 0.0;
			for (const FJunctionSlot& Slot : Slots)
			{
				const double SlotInput = Slot.InputDist();
				if (SlotInput > PreviousDistance)
				{
					double Start = PreviousDistance;
					double End = SlotInput;
					if (GetTrafficDirectionSignForSide(Side) < 0.0)
					{
						Swap(Start, End);
					}
					if (AActor* Shape = CreateZoneShapeForLane(World, this, Road, Lane, Start, End))
					{
						MassGraphActors.Add(Shape);
					}
				}
				PreviousDistance = Slot.OutputDist();
			}
			if (PreviousDistance < Road->Length())
			{
				double Start = PreviousDistance;
				double End = Road->Length();
				if (GetTrafficDirectionSignForSide(Side) < 0.0)
				{
					Swap(Start, End);
				}
				if (AActor* Shape = CreateZoneShapeForLane(World, this, Road, Lane, Start, End))
				{
					MassGraphActors.Add(Shape);
				}
			}
		}
	}

	for (AJunctionActor* Junction : Junctions)
	{
		if (!IsValid(Junction))
		{
			continue;
		}
		for (int GateIndex = 0; GateIndex < Junction->Gates.Num(); ++GateIndex)
		{
			FJunctionGate& Gate = Junction->Gates[GateIndex];
			for (int LinkIndex = 0; LinkIndex < Gate.Links.Num(); ++LinkIndex)
			{
				ARoadActor* LinkRoad = Gate.Links[LinkIndex].Road;
				if (!IsValid(LinkRoad))
				{
					continue;
				}
				const int TargetGateIndex = (GateIndex + LinkIndex) % Junction->Gates.Num();
				if (LinkIndex != 0 && LinkIndex != 1 && !Junction->IsTurnAllowed(GateIndex, TargetGateIndex))
				{
					continue;
				}
				for (URoadLane* Lane : LinkRoad->Lanes)
				{
					if (!Lane)
					{
						continue;
					}
					bool bHasDrivingSegment = false;
					for (const FLaneSegment& Segment : Lane->Segments)
					{
						if (Segment.LaneType == ELaneType::Driving)
						{
							bHasDrivingSegment = true;
							break;
						}
					}
					if (bHasDrivingSegment)
					{
						double Start = 0.0;
						double End = LinkRoad->Length();
						if (GetTrafficDirectionSignForSide(Lane->GetSide()) < 0.0)
						{
							Swap(Start, End);
						}
						if (AActor* Shape = CreateZoneShapeForLane(World, this, LinkRoad, Lane, Start, End))
						{
							MassGraphActors.Add(Shape);
						}
					}
				}
			}
		}
	}

	UE_LOG(LogRoadBuilder, Log, TEXT("Generated %d MassGraph zone shapes"), MassGraphActors.Num());
}

void ARoadScene::OctreeAddBoundary(URoadBoundary* Boundary)
{
	if (!Boundary)
	{
		return;
	}

	for (int i = 0; i < Boundary->Curve.Points.Num() - 1; i++)
		Octree.AddElement(FRoadOctreeElement(Boundary, i));
}

void ARoadScene::OctreeRemoveBoundary(URoadBoundary* Boundary)
{
	if (!Boundary)
	{
		return;
	}

	for (int i = 0; i < Boundary->OctreeIds.Num(); i++)
	{
		Octree.RemoveElement(Boundary->OctreeIds[i]);
	}
	Boundary->OctreeIds.Empty();
}

void ARoadScene::OctreeAddRoad(ARoadActor* Road)
{
	if (!IsValid(Road) || !Road->BaseCurve)
	{
		return;
	}
	TSet<URoadBoundary*> Boundaries = { Road->BaseCurve, Road->GetRoadEdge(0), Road->GetRoadEdge(1) };
	for (URoadBoundary* Boundary : Boundaries)
		OctreeAddBoundary(Boundary);
}

void ARoadScene::OctreeRemoveRoad(ARoadActor* Road)
{
	if (!IsValid(Road) || !Road->BaseCurve)
	{
		return;
	}
	TSet<URoadBoundary*> Boundaries = { Road->BaseCurve, Road->GetRoadEdge(0), Road->GetRoadEdge(1) };
	for (URoadBoundary* Boundary : Boundaries)
		OctreeRemoveBoundary(Boundary);
}

void ARoadScene::DestroyRoad(ARoadActor* Road)
{
	if (!IsValid(Road))
	{
		RemoveInvalidReferences();
		return;
	}
	Roads.Remove(Road);
	OctreeRemoveRoad(Road);
	Road->DeleteAllMarkings();
	Road->DisconnectAll();
	Road->Destroy();
	RemoveInvalidReferences();
}

void ARoadScene::PostLoad()
{
	AActor::PostLoad();
	RemoveInvalidReferences();
	for (ARoadActor* Road : Roads)
		OctreeAddRoad(Road);
}
#if WITH_EDITOR
#include "DesktopPlatformModule.h"
void ARoadScene::ExportXodr()
{
	TArray<FString> Files;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		if (DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Export OpenDRIVE"),
			TEXT(""),
			TEXT(""),
			TEXT("OpenDRIVE File (*.xodr)|*.xodr"),
			EFileDialogFlags::None,
			Files))
		{
			FXmlFile XmlFile(TEXT("<OpenDRIVE/>"), EConstructMethod::ConstructFromBuffer);
			FXmlNode* RootNode = XmlFile.GetRootNode();
			int RoadId = 0;
			int ObjectId = 200;
			for (ARoadActor* Road : Roads)
				Road->ExportXodr(RootNode, RoadId, ObjectId, -1);
			for (AJunctionActor* Junction : Junctions)
				Junction->ExportXodr(RootNode, RoadId, ObjectId);
			XmlFile.Save(*Files[0]);
		}
	}
}
#endif
