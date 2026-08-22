// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#include "LaneMarkStyle.h"
#include "RoadScene.h"

void ULaneMarkStyle::BuildMesh(UObject* Caller, FRoadMesh& Builder, const FPolyline& Curve)
{
	TArray<double> DashOffsets, SolidOffsets;
	AJunctionActor* Junction = Cast<ARoadActor>(Caller)->GetJunction();
	switch (MarkType)
	{
	case ELaneMarkType::Dash:
		DashOffsets.Add(0);
		break;
	case ELaneMarkType::Solid:
		SolidOffsets.Add(0);
		break;
	case ELaneMarkType::DashDash:
		DashOffsets.Add(-Separation);
		DashOffsets.Add(+Separation);
		break;
	case ELaneMarkType::DashSolid:
		DashOffsets.Add(-Separation);
		SolidOffsets.Add(+Separation);
		break;
	case ELaneMarkType::SolidDash:
		SolidOffsets.Add(-Separation);
		DashOffsets.Add(+Separation);
		break;
	case ELaneMarkType::SolidSolid:
		SolidOffsets.Add(-Separation);
		SolidOffsets.Add(+Separation);
		break;
	}
	if (DashOffsets.Num())
	{
		double Start = Curve.Points[0].Dist;
		double End = Curve.Points.Last().Dist;
		double Length = End - Start;
		int NumSegments = FMath::RoundToInt(Length / (DashLength + DashSpacing));
		double Step = Length / NumSegments;
		double ActualSpacing = Step - DashLength;
		for (int i = 0; i < NumSegments; i++)
		{
			double Base = Start + Step * i;
			FPolyline SubCurve = Curve.SubCurve(Base + ActualSpacing / 2, Base + ActualSpacing / 2 + DashLength);
			for (double Offset : DashOffsets)
			{
				FPolyline LeftCurve = SubCurve.Offset(FVector2D(Offset - Width / 2, 0), true);
				FPolyline RightCurve = SubCurve.Offset(FVector2D(Offset + Width / 2, 0), true);
				if (Junction)
				{
					Junction->FixHeight(LeftCurve);
					Junction->FixHeight(RightCurve);
				}
				Builder.AddStrip(Material, LeftCurve, RightCurve);
			}
		}
	}
	for (double Offset : SolidOffsets)
	{
		FPolyline LeftCurve = Curve.Offset(FVector2D(Offset - Width / 2, 0), true);
		FPolyline RightCurve = Curve.Offset(FVector2D(Offset + Width / 2, 0), true);
		if (Junction)
		{
			Junction->FixHeight(LeftCurve);
			Junction->FixHeight(RightCurve);
		}
		check(!LeftCurve.ContainsNaN());
		check(!RightCurve.ContainsNaN());
		Builder.AddStrip(Material, LeftCurve, RightCurve);
	}
}

void UCrosswalkStyle::BuildMesh(UObject* Caller, FRoadMesh& Builder, const FPolyline& Curve)
{
	FPolyline Points = Curve.Resample(DashLength + DashGap);
	AJunctionActor* Junction = Cast<ARoadActor>(Caller)->GetJunction();
	for (int i = 0; i < Points.Points.Num(); i++)
	{
		FVector VDir = Points.GetStraightDir(i);
		FVector HDir(-VDir.Y, VDir.X, VDir.Z);
		FVector& Pos = Points.Points[i].Pos;
		FPolyline LeftCurve({ FPolyPoint(Pos + VDir * DashLength / 2 - HDir * Width / 2, 0), FPolyPoint(Pos + VDir * DashLength / 2 + HDir * Width / 2, Width) });
		FPolyline RightCurve({ FPolyPoint(Pos - VDir * DashLength / 2 - HDir * Width / 2, 0), FPolyPoint(Pos - VDir * DashLength / 2 + HDir * Width / 2, Width) });
		if (Junction)
		{
			Junction->FixHeight(LeftCurve);
			Junction->FixHeight(RightCurve);
		}
		Builder.AddStrip(Material, LeftCurve, RightCurve);
	}
}

void UPolygonMarkStyle::BuildMesh(UObject* Caller, FRoadMesh& Builder, const FPolyline& Curve)
{
	if (Curve.Points.Num() < 3)
		return;

	struct FLine
	{
		FVector GetPoint(double S) const
		{
			return FMath::Lerp(Start, End, S);
		}
		FVector GetDir()
		{
			return (End - Start).GetSafeNormal();
		}
		FVector GetRight()
		{
			FVector Dir = GetDir();
			return FVector(-Dir.Y, Dir.X, Dir.Z);
		}
		FLine LeftLine(double Offset)
		{
			FVector Right = GetRight();
			return { Start - Right * Offset, End - Right * Offset };
		}
		FLine RightLine(double Offset)
		{
			FVector Right = GetRight();
			return { Start + Right * Offset, End + Right * Offset };
		}
		FVector Start;
		FVector End;

	};
	UMarkingCurve* Marking = Cast<UMarkingCurve>(Caller);
	if (!Marking)
		return;
	ARoadActor* Road = Marking->GetRoad();
	double Margin = 10.0;
	const double SafeLineSpace = FMath::Max(1.0, LineSpace);
	TArray<FLine> Lines;
	const FVector& Origin = Curve.Points[0].Pos;
	FTransform Trans(FRotator(0, Marking->Orientation, 0), Origin);
	FBox Box(EForceInit::ForceInit);
	for (const FPolyPoint& Point : Curve.Points)
		Box += Trans.InverseTransformPosition(Point.Pos);
	AJunctionActor* Junction = Road->GetJunction();
	auto CreateLine = [&Trans](const FVector& Start, const FVector& End)->FLine
	{
		return { Trans.TransformPosition(Start), Trans.TransformPosition(End) };
	};
	switch (MarkType)
	{
	case EPolygonMarkType::Solid:
	{
		TArray<FVector> Points = Curve.GetPositions();
		if (Junction)
			Junction->FixHeight(Points);
		Builder.AddPolygon(Material, nullptr, Points);
		break;
	}
	case EPolygonMarkType::Striped:
	{
		FVector Start = Box.Min - FVector(Margin, Margin, 0);
		FVector Size = Box.GetSize() + FVector(Margin, Margin, 0) * 2;
		int NumRows = FMath::Max(1, FMath::RoundToInt(Size.Y / SafeLineSpace));
		double StepY = Size.Y / NumRows;
		Start.Y += StepY * 0.5;
		for (int i = 0; i < NumRows; i++)
		{
			Lines.Add(CreateLine(Start, Start + FVector(Size.X, 0, 0)));
			Start.Y += StepY;
		}
		break;
	}
	case EPolygonMarkType::Crosshatch:
	{
		FVector Start = Box.Min - FVector(Margin, Margin, 0);
		FVector Size = Box.GetSize() + FVector(Margin, Margin, 0) * 2;
		int NumCols = FMath::Max(1, FMath::RoundToInt(Size.X / SafeLineSpace));
		int NumRows = FMath::Max(1, FMath::RoundToInt(Size.Y / SafeLineSpace));
		double StepX = Size.X / NumCols;
		double StepY = Size.Y / NumRows;
		Start += FVector(StepX * 0.5, StepY * 0.5, 0);
		for (int i = 0; i < NumRows; i++)
		{
			Lines.Add(CreateLine(Start, Start + FVector(Size.X, 0, 0)));
			Start.Y += StepY;
		}
		Start = Box.Min - FVector(Margin, Margin, 0);
		Start += FVector(StepX * 0.5, StepY * 0.5, 0);
		for (int i = 0; i < NumCols; i++)
		{
			Lines.Add(CreateLine(Start, Start + FVector(0, Size.Y, 0)));
			Start.X += StepX;
		}
		break;
	}
	case EPolygonMarkType::Chevron:
	{
	//	FVector Center =Box.GetCenter();
		FVector Size = Box.GetSize();
		double Tan = FMath::Tan(FMath::DegreesToRadians(ChevrenAngle));
		double OffsetY = Size.X * Tan * 0.5;
		FVector Start = FVector(0, Box.Min.Y - OffsetY, 0);
		Size += FVector(0, OffsetY, 0) + FVector(Margin, Margin, 0) * 2;
		double Sin = FMath::Sin(FMath::DegreesToRadians(ChevrenAngle));
		int NumRows = FMath::Max(1, FMath::RoundToInt(Size.Y / SafeLineSpace));
		double StepY = Size.Y / NumRows;
		Start.Y += StepY * 0.5;
		for (int i = 0; i < NumRows; i++)
		{
			Lines.Add(CreateLine(Start, Start + FVector(-Size.X / Sin, Size.X, 0)));
			Lines.Add(CreateLine(Start, Start + FVector(Size.X / Sin, Size.X, 0)));
			Start.Y += StepY;
		}
		break;
	}
	}
	for (FLine& Line : Lines)
	{
		auto IsInsidePolygon = [&Curve](const FVector& Position)
		{
			bool bInside = false;
			const FVector2D TestPoint(Position.X, Position.Y);
			for (int32 EdgeIndex = 0; EdgeIndex < Curve.Points.Num() - 1; ++EdgeIndex)
			{
				const FVector2D StartPoint(Curve.Points[EdgeIndex].Pos.X, Curve.Points[EdgeIndex].Pos.Y);
				const FVector2D EndPoint(Curve.Points[EdgeIndex + 1].Pos.X, Curve.Points[EdgeIndex + 1].Pos.Y);
				const bool bCrossesScanline = (StartPoint.Y > TestPoint.Y) != (EndPoint.Y > TestPoint.Y);
				if (bCrossesScanline)
				{
					const double CrossingX = StartPoint.X +
						(TestPoint.Y - StartPoint.Y) * (EndPoint.X - StartPoint.X) / (EndPoint.Y - StartPoint.Y);
					if (TestPoint.X < CrossingX)
						bInside = !bInside;
				}
			}
			return bInside;
		};
		auto SolvePoints = [&Curve, &IsInsidePolygon](const FLine& ClippingLine)->TArray<FVector>
		{
			TArray<double> Hits = { 0.0, 1.0 };
			for (int32 EdgeIndex = 0; EdgeIndex < Curve.Points.Num() - 1; ++EdgeIndex)
			{
				const FVector2D LineStart(ClippingLine.Start.X, ClippingLine.Start.Y);
				const FVector2D LineEnd(ClippingLine.End.X, ClippingLine.End.Y);
				const FVector2D PolygonStart(Curve.Points[EdgeIndex].Pos.X, Curve.Points[EdgeIndex].Pos.Y);
				const FVector2D PolygonEnd(Curve.Points[EdgeIndex + 1].Pos.X, Curve.Points[EdgeIndex + 1].Pos.Y);
				double LineHit = 0.0;
				double PolygonHit = 0.0;
				if (DoLineSegmentsIntersect(LineStart, LineEnd, PolygonStart, PolygonEnd, LineHit, PolygonHit))
					Hits.Add(FMath::Clamp(LineHit, 0.0, 1.0));
			}
			Hits.Sort();
			for (int32 HitIndex = Hits.Num() - 1; HitIndex > 0; --HitIndex)
			{
				if (FMath::IsNearlyEqual(Hits[HitIndex], Hits[HitIndex - 1], UE_DOUBLE_KINDA_SMALL_NUMBER))
					Hits.RemoveAt(HitIndex);
			}
			TArray<FVector> Points;
			for (int32 HitIndex = 0; HitIndex + 1 < Hits.Num(); ++HitIndex)
			{
				const double StartHit = Hits[HitIndex];
				const double EndHit = Hits[HitIndex + 1];
				if (EndHit - StartHit <= UE_DOUBLE_KINDA_SMALL_NUMBER)
					continue;
				if (IsInsidePolygon(ClippingLine.GetPoint((StartHit + EndHit) * 0.5)))
				{
					Points.Add(ClippingLine.GetPoint(StartHit));
					Points.Add(ClippingLine.GetPoint(EndHit));
				}
			}
			return MoveTemp(Points);
		};
		TArray<FVector> LeftPoints = SolvePoints(Line.LeftLine(5));
		TArray<FVector> RightPoints = SolvePoints(Line.RightLine(5));
		if (Junction)
		{
			Junction->FixHeight(LeftPoints);
			Junction->FixHeight(RightPoints);
		}
		const int32 PairedPointCount = FMath::Min(LeftPoints.Num(), RightPoints.Num()) & ~1;
		for (int i = 0; i < PairedPointCount; i += 2)
			Builder.AddTriangles(Material, { FIndex3i(0,1,2), FIndex3i(2,1,3) }, { RightPoints[i], RightPoints[i + 1], LeftPoints[i], LeftPoints[i + 1] }, FVector::UpVector);
	}
}
