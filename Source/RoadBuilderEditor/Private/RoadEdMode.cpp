// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#include "RoadEdMode.h"
#include "EditorViewportClient.h"
#include "EditorModes.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "SceneView.h"
#include "Kismet/GameplayStatics.h"
#include "Editor/TransBuffer.h"
#include "DynamicMeshBuilder.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "ScopedTransaction.h"
#include "RoadEdModeToolkit.h"
#include "RoadPreset.h"
#include "Toolkits/ToolkitManager.h"
#include "Containers/Ticker.h"

IMPLEMENT_HIT_PROXY(HRoadProxy, HHitProxy);
IMPLEMENT_HIT_PROXY(HGroundProxy, HHitProxy);
IMPLEMENT_HIT_PROXY(HJunctionProxy, HHitProxy);
IMPLEMENT_HIT_PROXY(HRoadCurveProxy, HHitProxy);
IMPLEMENT_HIT_PROXY(HRoadMarkingProxy, HHitProxy);

#define LOCTEXT_NAMESPACE "RoadBuilder"

namespace
{
	void RebuildRoadMarkingMesh(URoadMarking* Marking)
	{
		ARoadActor* Road = IsValid(Marking) ? Marking->GetRoad() : nullptr;
		if (!IsValid(Road))
			return;

		TArray<FJunctionSlot> Slots;
		if (!Road->IsLink())
		{
			if (ARoadScene* Scene = Road->GetScene(); IsValid(Scene))
				Slots = Scene->GetJunctionSlots(Road);
		}
		Road->BuildMesh(Slots);
	}
}

bool FRoadTool::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (Event == IE_Pressed)
	{
		if (Key == EKeys::Escape)
		{
			SelectParent();
			return true;
		}
	}
	return false;
}

bool FRoadTool::EndModify()
{
	if (LazyRebuild)
	{
		// Clear the request before rebuilding. Undo/redo can end an active
		// viewport interaction and must not leave a stale rebuild armed.
		LazyRebuild = false;
		if (!GIsTransacting)
		{
			if (ARoadScene* RoadScene = GetScene(); IsValid(RoadScene))
			{
				TSet<ARoadScene*> DependentScenes;
				if (UWorld* World = RoadScene->GetWorld())
				{
					for (TActorIterator<ARoadActor> RoadIt(World); RoadIt; ++RoadIt)
					{
						ARoadActor* CandidateRoad = *RoadIt;
						ARoadScene* CandidateScene = IsValid(CandidateRoad) ? CandidateRoad->GetScene() : nullptr;
						if (IsValid(CandidateScene) && CandidateScene != RoadScene &&
							CandidateRoad->HasCrossRoadSceneParentInScene(RoadScene))
						{
							CandidateRoad->Modify();
							DependentScenes.Add(CandidateScene);
						}
					}
				}
				RoadScene->Rebuild();
				for (ARoadScene* DependentScene : DependentScenes)
				{
					if (IsValid(DependentScene))
					{
						DependentScene->Modify();
						DependentScene->Rebuild();
					}
				}
			}
		}
	}
	return true;
}

void FRoadTool::Reset()
{
	FEditorViewportClient* Client = GLevelEditorModeTools().GetFocusedViewportClient();
	if (Client)
	{
		Client->Invalidate();
	}

	if (SRoadEdit* EditWidget = GetEditWidget())
	{
		EditWidget->SetEditNone();
	}
}

ARoadScene* FRoadTool::GetScene() const
{
	if (FEdModeRoad* RoadMode = FEdModeRoad::Get())
	{
		return RoadMode->Scene;
	}
	return nullptr;
}

ARoadActor*& FRoadTool::GetSelectedRoad() const
{
	static ARoadActor* NullSelectedRoad = nullptr;
	if (FEdModeRoad* RoadMode = FEdModeRoad::Get())
	{
		return RoadMode->SelectedRoad;
	}
	NullSelectedRoad = nullptr;
	return NullSelectedRoad;
}

AGroundActor*& FRoadTool::GetSelectedGround() const
{
	static AGroundActor* NullSelectedGround = nullptr;
	if (FEdModeRoad* RoadMode = FEdModeRoad::Get())
	{
		return RoadMode->SelectedGround;
	}
	NullSelectedGround = nullptr;
	return NullSelectedGround;
}

AJunctionActor*& FRoadTool::GetSelectedJunction() const
{
	static AJunctionActor* NullSelectedJunction = nullptr;
	if (FEdModeRoad* RoadMode = FEdModeRoad::Get())
	{
		return RoadMode->SelectedJunction;
	}
	NullSelectedJunction = nullptr;
	return NullSelectedJunction;
}

SRoadEdit* FRoadTool::GetEditWidget() const
{
	FEdModeRoad* RoadMode = FEdModeRoad::Get();
	if (!RoadMode)
	{
		return nullptr;
	}

	TSharedPtr<FModeToolkit> ModeToolkit = RoadMode->GetToolkit();
	if (!ModeToolkit.IsValid())
	{
		return nullptr;
	}

	TSharedPtr<SWidget> Widget = ModeToolkit->GetInlineContent();
	return (SRoadEdit*)Widget.Get();
}

FRay FRoadTool::GetRay(FEditorViewportClient* ViewportClient) const
{
	if (!ViewportClient || !ViewportClient->Viewport || !ViewportClient->GetScene())
	{
		return FRay();
	}

	FViewport* Viewport = ViewportClient->Viewport;
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, ViewportClient->GetScene(), ViewportClient->EngineShowFlags).SetRealtimeUpdate(ViewportClient->IsRealtime()));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return FRay();
	}

	FViewportCursorLocation MouseViewportRay(View, ViewportClient, Viewport->GetMouseX(), Viewport->GetMouseY());
	return FRay(MouseViewportRay.GetOrigin(), MouseViewportRay.GetDirection());
}

FVector FRoadTool::LineTrace(const FRay& Ray, AActor* IgnoredActor) const
{
	FHitResult Hit;
	FCollisionQueryParams Params;
	if (IgnoredActor)
		Params.AddIgnoredActor(IgnoredActor);
	ARoadScene* RoadScene = GetScene();
	UWorld* World = RoadScene ? RoadScene->GetWorld() : nullptr;
	if (World && World->LineTraceSingleByChannel(Hit, Ray.Origin, Ray.Origin + Ray.Direction * 1000000.f, ECollisionChannel::ECC_Visibility, Params))
		return Hit.Location;
	return FVector(WORLD_MAX, WORLD_MAX, WORLD_MAX);
}

FVector FRoadTool::LineTrace(FEditorViewportClient* ViewportClient, AActor* IgnoredActor) const
{
	return LineTrace(GetRay(ViewportClient), IgnoredActor);
}

void FRoadTool::SelectParent()
{
	ARoadActor*& SelectedRoad = GetSelectedRoad();
	AJunctionActor*& SelectedJunction = GetSelectedJunction();
	if (SelectedRoad)
	{
		if (AJunctionActor* Junction = Cast<AJunctionActor>(SelectedRoad->GetAttachParentActor()))
			SelectedJunction = Junction;
		SelectedRoad = nullptr;
	}
	else if (SelectedJunction)
		SelectedJunction = nullptr;
	Reset();
}

bool FRoadTool::HandleClickRoad(HHitProxy* HitProxy, const FViewportClick& Click, int* PointIndex)
{
	if (Click.GetKey() == EKeys::LeftMouseButton)
	{
		ARoadActor*& SelectedRoad = GetSelectedRoad();
		if (HRoadProxy* Proxy = HitProxyCast<HRoadProxy>(HitProxy))
		{
			if (!IsValid(Proxy->Road))
			{
				SelectedRoad = nullptr;
				if (PointIndex)
				{
					*PointIndex = INDEX_NONE;
				}
				return false;
			}
			SelectedRoad = Proxy->Road;
			// World Partition levels can contain many independent RoadScenes.
			// Keep the editor tool scoped to the scene that owns the clicked road.
			if (FEdModeRoad* RoadMode = FEdModeRoad::Get())
			{
				if (ARoadScene* OwningScene = Proxy->Road->GetScene())
				{
					RoadMode->Scene = OwningScene;
				}
			}
			if (PointIndex)
				*PointIndex = Proxy->Index;
			return true;
		}
		/*
		if (!HitProxy || HitProxy->IsA(HActor::StaticGetType()))
		{
			Reset();
			return true;
		}*/
	}
	return false;
}

ARoadActor* FRoadTool::PickRoadAcrossLoadedScenes(const FVector& Pos, ARoadActor* IgnoredRoad) const
{
	ARoadScene* ActiveScene = GetScene();
	if (!IsValid(ActiveScene))
	{
		return nullptr;
	}

	const USettings_Global* Settings = GetDefault<USettings_Global>();
	if (!Settings || !Settings->AllowCrossRoadSceneConnections)
	{
		return ActiveScene->PickRoad(Pos, IgnoredRoad);
	}

	UWorld* World = ActiveScene->GetWorld();
	ARoadActor* BestRoad = nullptr;
	double BestDistanceSquared = MAX_dbl;
	for (TActorIterator<ARoadScene> SceneIt(World); SceneIt; ++SceneIt)
	{
		ARoadScene* CandidateScene = *SceneIt;
		if (!IsValid(CandidateScene))
		{
			continue;
		}

		ARoadActor* CandidateRoad = CandidateScene->PickRoad(Pos, IgnoredRoad);
		if (!IsValid(CandidateRoad) || !CandidateRoad->BaseCurve)
		{
			continue;
		}

		const FVector2D UV = CandidateRoad->GetUV(Pos);
		if (!IsUVValid(UV))
		{
			continue;
		}
		const FVector CandidatePosition(CandidateRoad->GetPos(UV.X), CandidateRoad->GetHeight(UV.X));
		const double DistanceSquared = FVector::DistSquared(Pos, CandidatePosition);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestRoad = CandidateRoad;
		}
	}
	return BestRoad;
}

bool FRoadTool::IsValidRoadPointSelection(const ARoadActor* Road, int32 PointIndex)
{
	return IsValid(Road) && Road->RoadPoints.IsValidIndex(PointIndex);
}

bool FRoadTool::IsValidHeightPointSelection(const ARoadActor* Road, int32 PointIndex)
{
	return IsValid(Road) && Road->HeightPoints.IsValidIndex(PointIndex);
}

bool FRoadTool::HandleClickJunction(HHitProxy* HitProxy, const FViewportClick& Click, int* GateIndex, int* LinkIndex)
{
	if (Click.GetKey() == EKeys::LeftMouseButton)
	{
		AJunctionActor*& SelectedJunction = GetSelectedJunction();
		if (HJunctionProxy* Proxy = HitProxyCast<HJunctionProxy>(HitProxy))
		{
			SelectedJunction = Proxy->Junction;
			if (FEdModeRoad* RoadMode = FEdModeRoad::Get())
			{
				if (ARoadScene* OwningScene = Proxy->Junction->GetScene())
				{
					RoadMode->Scene = OwningScene;
				}
			}
			if (GateIndex)
				*GateIndex = Proxy->Index;
			if (LinkIndex)
				*LinkIndex = Proxy->SubId;
			return true;
		}
		/*
		if (!HitProxy || HitProxy->IsA(HActor::StaticGetType()))
		{
			Reset();
			return true;
		}*/
	}
	return false;
}

void FRoadTool::DrawCurve(FPrimitiveDrawInterface* PDI, const FPolyline& Curve, FColor Color, float Thickness, float DepthBias)
{
	for (int i = 0; i < Curve.Points.Num() - 1; i++)
	{
		const FVector& Start = Curve.Points[i].Pos;
		const FVector& End = Curve.Points[i + 1].Pos;
		PDI->DrawLine(Start, End, Color, SDPG_Foreground, Thickness, DepthBias, true);
	}
}

void FRoadTool::DrawPoint(FPrimitiveDrawInterface* PDI, URoadCurve* Curve, double Dist, FColor Color)
{
	if (!PDI || !Curve)
	{
		return;
	}

	FVector Point = Curve->GetPos(Dist);
	PDI->DrawPoint(Point, Color, Size_Point, SDPG_Foreground);
}

void FRoadTool::DrawDivider(FPrimitiveDrawInterface* PDI, URoadLane* Lane, double Dist, FColor Color)
{
	if (!PDI || !Lane || !Lane->RightBoundary || !Lane->LeftBoundary)
	{
		return;
	}

	FVector Start = Lane->RightBoundary->GetPos(Dist);
	FVector End = Lane->LeftBoundary->GetPos(Dist);
	PDI->DrawLine(Start, End, Color, SDPG_Foreground);
}

void FRoadTool::DrawRoads(FPrimitiveDrawInterface* PDI, bool DrawLinks)
{
	if (!PDI)
	{
		return;
	}

	ARoadActor* SelectedRoad = GetSelectedRoad();
	ARoadScene* ActiveScene = GetScene();
	UWorld* World = IsValid(ActiveScene) ? ActiveScene->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	// Draw every loaded World Partition sector. Unloaded sectors remain streamed
	// out; loading their cells makes their editable curves appear immediately.
	for (TActorIterator<ARoadScene> SceneIt(World); SceneIt; ++SceneIt)
	{
		ARoadScene* Scene = *SceneIt;
		if (!IsValid(Scene))
		{
			continue;
		}

		Scene->RemoveInvalidReferences();
		for (ARoadActor* Road : Scene->Roads)
		{
			if (!IsValid(Road))
			{
				continue;
			}

			PDI->SetHitProxy(new HRoadProxy(Road));
			FColor Color = Road == SelectedRoad ? Color_Select : Color_Road;
			if (Road->RoadPoints.Num() == 1)
				PDI->DrawPoint(Road->GetPos(0), Color, Size_Point, SDPG_Foreground);
			else if (Road->BaseCurve)
				DrawCurve(PDI, Road->BaseCurve->Curve, Color, Thickness_Road, Road == SelectedRoad ? DepthBias_Select : 0);
		}
		if (DrawLinks)
		{
			for (AJunctionActor* Junction : Scene->Junctions)
			{
				if (!IsValid(Junction))
				{
					continue;
				}

				for (FJunctionGate& Gate : Junction->Gates)
				{
					for (int i = 0; i < Gate.Links.Num(); i++)
					{
						if (ARoadActor* Road = Gate.Links[i].Road)
						{
							if (!IsValid(Road) || !Road->BaseCurve)
							{
								continue;
							}

							if (URoadCurve* Curve = (i == 1) ? (URoadCurve*)Road->BaseCurve : (URoadCurve*)Road->BaseCurve->RightLane)
							{
								PDI->SetHitProxy(new HRoadProxy(Road));
								DrawCurve(PDI, Curve->Curve, (Road == SelectedRoad) ? Color_Select : Color_Road, Thickness_Road, Road == SelectedRoad ? DepthBias_Select : 0);
							}
						}
					}
			}
			}
		}
	}
}

void FRoadTool::DrawJunction(FPrimitiveDrawInterface* PDI, AJunctionActor* Junction, FColor Color)
{
	if (!PDI || !IsValid(Junction))
	{
		return;
	}

	TArray<FJunctionGate>& Gates = Junction->Gates;
	if (Gates.Num() < 2)
	{
		return;
	}

	PDI->SetHitProxy(new HJunctionProxy(Junction));
	for (int i = 0; i < Gates.Num(); i++)
	{
		FJunctionGate& Gate = Gates[i];
		FJunctionGate& Next = Gates[(i + 1) % Gates.Num()];
		if (!IsValid(Gate.Road) || !IsValid(Next.Road))
		{
			continue;
		}

		int SrcSide = Gate.Sign > 0 ? 0 : 1;
		int DstSide = Next.Sign > 0 ? 1 : 0;
		URoadBoundary* SrcBoundary = Gate.Road->GetRoadEdge(SrcSide);
		URoadBoundary* PrevBoundary = Gate.Road->GetRoadEdge(!SrcSide);
		URoadBoundary* DstBoundary = Next.Road->GetRoadEdge(DstSide);
		if (!SrcBoundary || !PrevBoundary || !DstBoundary)
		{
			continue;
		}

		PDI->DrawLine(PrevBoundary->GetPos(Gate.Dist), SrcBoundary->GetPos(Gate.Dist), Color, SDPG_Foreground, Thickness_Road, 0, true);
		if (Gate.Links.IsValidIndex(1) && IsValid(Gate.Links[1].Road) && Gate.Links[1].Road->BaseCurve)
			DrawCurve(PDI, Gate.Links[1].Road->BaseCurve->Curve, Color, Thickness_Road);
	}
	USettings_Global* Settings = GetMutableDefault<USettings_Global>();
	if (Settings->DisplayGateRadianPoints)
	{
#if 0
		FVector Center(0, 0, 0);
		for (FJunctionGate& Gate : Junction->Gates)
		{
			FVector Pos = Gate.Road->BaseCurve->GetPos(Gate.Dist);
			Center += Pos / Gates.Num();
		}
		PDI->DrawPoint(Center, FColor::Red, Size_Point, SDPG_Foreground);
		for (FJunctionGate& Gate : Junction->Gates)
		{
			FVector Pos = Gate.Road->BaseCurve->GetPos(Gate.Dist + Gate.Sign * DefaultJunctionExtent);
			PDI->DrawPoint(Pos, FColor::Blue, Size_Point, SDPG_Foreground);
		}
#elif 0
		for (FVector2D& Crossing : Junction->DebugCrossings)
		{
			//	FVector Pos = Gate.Road->BaseCurve->GetPos(Gate.Dist + Gate.Sign * DefaultJunctionExtent);
			PDI->DrawPoint(FVector(Crossing, 0), FColor::Blue, Size_Point, SDPG_Foreground);
		}
#elif 0
		for (int i = 0; i < Junction->DebugCurves.Num(); i++)
		{
			DrawCurve(PDI, Junction->DebugCurves[i], i % 2 ? FColor::Green : FColor::Red, Thickness_Road);
		}
#else
		PDI->SetHitProxy(nullptr);
		for (int i = 0; i < Junction->DebugPoints.Num(); i++)
		{
			FVector& Start = Junction->DebugPoints[i];
			FVector& End = Junction->DebugPoints[(i + 1) % Junction->DebugPoints.Num()];
			FVector Dir = (End - Start).GetSafeNormal();
			FVector N(-Dir.Y, Dir.X, Dir.Z);
			FVector Center = (Start + End) / 2;
			FVector Left = Center - N * 50 - Dir * 100;
			FVector Right = Center + N * 50 - Dir * 100;
			PDI->DrawLine(Start, End, FColor::Blue, SDPG_Foreground, Thickness_Line, 0, true);
			PDI->DrawLine(Left, Center, FColor::Blue, SDPG_Foreground, Thickness_Line, 0, true);
			PDI->DrawLine(Right, Center, FColor::Blue, SDPG_Foreground, Thickness_Line, 0, true);
		}
#endif
	}
	if (Settings->DisplayGoreDiagnostics)
	{
		PDI->SetHitProxy(nullptr);
		for (const FGoreDiagnostic& Diagnostic : Junction->GoreDiagnostics)
		{
			const FColor StatusColor = Diagnostic.IsReady() ? FColor::Green : FColor::Red;

			if (Diagnostic.SourceBoundary.Points.Num() >= 2)
				DrawCurve(PDI, Diagnostic.SourceBoundary, FColor::Cyan, Thickness_Road + 1.0f, DepthBias_Select);
			if (Diagnostic.DestinationBoundary.Points.Num() >= 2)
				DrawCurve(PDI, Diagnostic.DestinationBoundary, FColor::Magenta, Thickness_Road + 1.0f, DepthBias_Select);
			if (Diagnostic.CornerBoundary.Points.Num() >= 2)
				DrawCurve(PDI, Diagnostic.CornerBoundary, FColor(64, 128, 255), Thickness_Road, DepthBias_Select);

			if (Diagnostic.bHasNosePair)
			{
				PDI->DrawPoint(Diagnostic.SourceNose, FColor::Cyan, Size_Point + 6.0f, SDPG_Foreground);
				PDI->DrawPoint(Diagnostic.DestinationNose, FColor::Magenta, Size_Point + 6.0f, SDPG_Foreground);
				PDI->DrawLine(
					Diagnostic.SourceNose,
					Diagnostic.DestinationNose,
					StatusColor,
					SDPG_Foreground,
					Thickness_Road + 1.0f,
					DepthBias_Select,
					true);
			}
			if (Diagnostic.bHasIntersection)
				PDI->DrawPoint(Diagnostic.Intersection, StatusColor, Size_Point + 12.0f, SDPG_Foreground);

			const FVector Marker = Diagnostic.LabelLocation + FVector(0, 0, 50);
			PDI->DrawPoint(Marker, StatusColor, Size_Point + 10.0f, SDPG_Foreground);
			PDI->DrawLine(Marker - FVector(75, 0, 0), Marker + FVector(75, 0, 0), StatusColor, SDPG_Foreground, Thickness_Road, DepthBias_Select, true);
			PDI->DrawLine(Marker - FVector(0, 75, 0), Marker + FVector(0, 75, 0), StatusColor, SDPG_Foreground, Thickness_Road, DepthBias_Select, true);
		}
	}
}

void FRoadTool::DrawJunctions(FPrimitiveDrawInterface* PDI)
{
	ARoadScene* ActiveScene = GetScene();
	UWorld* World = IsValid(ActiveScene) ? ActiveScene->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	AJunctionActor* SelectedJunction = GetSelectedJunction();
	for (TActorIterator<ARoadScene> SceneIt(World); SceneIt; ++SceneIt)
	{
		ARoadScene* Scene = *SceneIt;
		if (!IsValid(Scene))
		{
			continue;
		}
		Scene->RemoveInvalidReferences();
		for (AJunctionActor* Junction : Scene->Junctions)
		{
			if (IsValid(Junction))
			{
				DrawJunction(PDI, Junction, (Junction == SelectedJunction) ? Color_Select : Color_Road);
			}
		}
	}
}

bool FRoadTool_RoadPlan::ShouldDrawWidget() const
{
	return IsValidRoadPointSelection(GetSelectedRoad(), PointIndex);
}

FVector FRoadTool_RoadPlan::GetWidgetLocation() const
{
	ARoadActor* Road = GetSelectedRoad();
	if (IsValidRoadPointSelection(Road, PointIndex))
		return Road->GetPos(PointIndex);
	return FRoadTool::GetWidgetLocation();
}

bool FRoadTool_RoadPlan::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	ARoadActor*& Road = GetSelectedRoad();
	if (HandleClickRoad(HitProxy, Click, &PointIndex))
	{
		if (!IsValidRoadPointSelection(Road, PointIndex))
		{
			PointIndex = INDEX_NONE;
		}
		if (SRoadEdit* EditWidget = GetEditWidget())
		{
			EditWidget->SetEditRoadPoint(Road, PointIndex);
		}
		return true;
	}
	if (Click.GetKey() == EKeys::LeftMouseButton)
	{
		Reset();
		return true;
	}
	if (Click.GetKey() == EKeys::RightMouseButton)
	{
		const FScopedTransaction Transaction(LOCTEXT("RoadPlan", "RoadPlan"));
		USettings_RoadPlan* Data = GetMutableDefault<USettings_RoadPlan>();
		ARoadScene* Scene = GetScene();
		FRay Ray = GetRay(InViewportClient);
		FVector Pos = FMath::RayPlaneIntersection(Ray.Origin, Ray.Direction, FPlane(FVector(0, 0, Data->BaseHeight), FVector::UpVector));
		if (HRoadProxy* Proxy = HitProxyCast<HRoadProxy>(HitProxy))
		{
			if (!IsValid(Proxy->Road))
			{
				return false;
			}
			Road = Proxy->Road;
			if (FEdModeRoad* RoadMode = FEdModeRoad::Get())
			{
				if (ARoadScene* OwningScene = Road->GetScene())
				{
					RoadMode->Scene = OwningScene;
					Scene = OwningScene;
				}
			}
			FVector2D UV = Road->GetUV(Pos);
			Road->Modify();
			PointIndex = Road->AddPoint(UV.X);
		}
		else
		{
			if (!Road)
			{
				Scene->Modify();
				URoadStyle* ResolvedStyle = nullptr;
				if (URoadPreset* PresetObj = Data->Preset.LoadSynchronous())
					ResolvedStyle = PresetObj->GenerateRoadStyle();
				else
					ResolvedStyle = Data->Style.LoadSynchronous();
				Road = Scene->AddRoad(ResolvedStyle, Data->BaseHeight);
			}
			Road->Modify();
			Road->InsertPoint((FVector2D&)Pos, PointIndex);
		}
		Road->UpdateCurve();
		Scene->Rebuild();
		if (SRoadEdit* EditWidget = GetEditWidget())
		{
			EditWidget->SetEditRoadPoint(Road, PointIndex);
		}
		return true;
	}
	return false;
}

bool FRoadTool_RoadPlan::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (FRoadTool::InputKey(ViewportClient, Viewport, Key, Event))
		return true;
	if (Event == IE_Pressed)
	{
		if (Key == EKeys::Delete)
		{
			if (ARoadActor* SelectedRoad = GetSelectedRoad())
			{
				ARoadScene* Scene = GetScene();
				if (IsValidRoadPointSelection(SelectedRoad, PointIndex))
				{
					SelectedRoad->RoadPoints.RemoveAt(PointIndex);
					PointIndex = INDEX_NONE;
					SelectedRoad->UpdateCurve();
				}
				else
				{
					Scene->DestroyRoad(SelectedRoad);
					Reset();
				}
				Scene->Rebuild();
			}
			return true;
		}
	}
	return false;
}

bool FRoadTool_RoadPlan::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
	{
		if (ARoadActor* SelectedRoad = GetSelectedRoad(); IsValidRoadPointSelection(SelectedRoad, PointIndex))
		{
			SelectedRoad->Modify();
			SelectedRoad->RoadPoints[PointIndex].Pos += (FVector2D&)InDrag;
			if ((PointIndex == 0 || PointIndex == SelectedRoad->RoadPoints.Num() - 1))
			{
				SelectedRoad->DisconnectAll(PointIndex);
				int HeightIndex = PointIndex ? SelectedRoad->HeightPoints.Num() - 1 : 0;
				if (!SelectedRoad->HeightPoints.IsValidIndex(HeightIndex))
				{
					PointIndex = INDEX_NONE;
					return false;
				}
			//	FVector Pos(SelectedRoad->RoadPoints[PointIndex].Pos, SelectedRoad->HeightPoints[HeightIndex].Height);
			//	FVector Location = LineTrace(FRay(InViewportClient->GetViewLocation(), (Pos - InViewportClient->GetViewLocation()).GetSafeNormal()));
				ARoadActor* HoveredRoad = PickRoadAcrossLoadedScenes(
					FVector(SelectedRoad->RoadPoints[PointIndex].Pos, SelectedRoad->HeightPoints[HeightIndex].Height),
					SelectedRoad);
				if (HoveredRoad && HoveredRoad != SelectedRoad)
				{
				//	SelectedRoad->HeightPoints[HeightIndex].Height = Location.Z;
					SelectedRoad->ConnectTo(PointIndex, HoveredRoad);
				}
			}
			SelectedRoad->UpdateCurve();
			LazyRebuild = true;
		}
		return true;
	}
	return false;
}

void FRoadTool_RoadPlan::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	DrawRoads(PDI, false);
	if (ARoadActor* SelectedRoad = GetSelectedRoad(); IsValid(SelectedRoad))
	{
		for (int i = 0; i < SelectedRoad->RoadPoints.Num(); i++)
		{
			PDI->SetHitProxy(new HRoadProxy(SelectedRoad, i));
			PDI->DrawPoint(SelectedRoad->GetPos(i), i == PointIndex ? Color_Select : Color_Road, Size_Point, SDPG_Foreground);
		}
	}
}

bool FRoadTool_RoadHeight::ShouldDrawWidget() const
{
	return IsValidHeightPointSelection(GetSelectedRoad(), PointIndex);
}

FVector FRoadTool_RoadHeight::GetWidgetLocation() const
{
	ARoadActor* Road = GetSelectedRoad();
	if (IsValidHeightPointSelection(Road, PointIndex) && Road->BaseCurve)
		return Road->BaseCurve->GetPos(Road->HeightPoints[PointIndex].Dist);
	return FRoadTool::GetWidgetLocation();
}

bool FRoadTool_RoadHeight::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	ARoadActor* Road = GetSelectedRoad();
	if (IsValidHeightPointSelection(Road, PointIndex) && Road->BaseCurve)
	{
		InMatrix = FRotationMatrix(Road->BaseCurve->GetDir(Road->HeightPoints[PointIndex].Dist).Rotation());
		return true;
	}
	return false;
}

bool FRoadTool_RoadHeight::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	ARoadActor*& SelectedRoad = GetSelectedRoad();
	if (HandleClickRoad(HitProxy, Click, &PointIndex))
	{
		if (!IsValidHeightPointSelection(SelectedRoad, PointIndex))
		{
			PointIndex = INDEX_NONE;
		}
		if (SRoadEdit* EditWidget = GetEditWidget())
		{
			EditWidget->SetEditHeightPoint(SelectedRoad, PointIndex);
		}
		return true;
	}
	if (SelectedRoad)
	{
		if (Click.GetKey() == EKeys::RightMouseButton)
		{
			FVector2D UV = SelectedRoad->GetUV(LineTrace(InViewportClient));
			if (HRoadProxy* Proxy = HitProxyCast<HRoadProxy>(HitProxy))
			{
				const FScopedTransaction Transaction(LOCTEXT("RoadHeight", "RoadHeight"));
				SelectedRoad->Modify();
				PointIndex = SelectedRoad->AddHeight(UV.X);
				if (SRoadEdit* EditWidget = GetEditWidget())
				{
					EditWidget->SetEditHeightPoint(SelectedRoad, PointIndex);
				}
				SelectedRoad->UpdateCurve();
				if (ARoadScene* Scene = SelectedRoad->GetScene(); IsValid(Scene))
				{
					Scene->RebuildHeightOnly(SelectedRoad);
				}
				return true;
			}
		}
	}
	return false;
}

bool FRoadTool_RoadHeight::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (FRoadTool::InputKey(ViewportClient, Viewport, Key, Event))
		return true;
	if (Event == IE_Pressed)
	{
		if (Key == EKeys::Delete)
		{
			ARoadActor* SelectedRoad = GetSelectedRoad();
			if (IsValidHeightPointSelection(SelectedRoad, PointIndex))
			{
				const FScopedTransaction Transaction(LOCTEXT("DeleteRoadHeight", "Delete Road Height Point"));
				SelectedRoad->Modify();
				SelectedRoad->HeightPoints.RemoveAt(PointIndex);
				PointIndex = INDEX_NONE;
				SelectedRoad->UpdateCurve();
				if (ARoadScene* Scene = SelectedRoad->GetScene(); IsValid(Scene))
				{
					Scene->RebuildHeightOnly(SelectedRoad);
				}
			}
			return true;
		}
	}
	return false;
}

bool FRoadTool_RoadHeight::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
	{
		if (ARoadActor* SelectedRoad = GetSelectedRoad(); IsValidHeightPointSelection(SelectedRoad, PointIndex))
		{
			FMatrix Mat = GLevelEditorModeTools().GetCustomDrawingCoordinateSystem();
			FVector LocalDrag = Mat.InverseTransformVector(InDrag);
			SelectedRoad->HeightPoints[PointIndex].Dist += LocalDrag.X;
			SelectedRoad->UpdateCurve();
			LazyRebuild = true;
		}
		return true;
	}
	return false;
}

bool FRoadTool_RoadHeight::EndModify()
{
	if (LazyRebuild)
	{
		LazyRebuild = false;
		if (!GIsTransacting)
		{
			if (ARoadActor* Road = GetSelectedRoad(); IsValid(Road))
			{
				if (ARoadScene* Scene = Road->GetScene(); IsValid(Scene))
				{
					Scene->RebuildHeightOnly(Road);
				}
			}
		}
	}
	return true;
}

void FRoadTool_RoadHeight::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	DrawRoads(PDI, false);
	if (ARoadActor* SelectedRoad = GetSelectedRoad(); IsValid(SelectedRoad) && SelectedRoad->BaseCurve)
	{
		for (int i = 0; i < SelectedRoad->HeightPoints.Num(); i++)
		{
			PDI->SetHitProxy(new HRoadProxy(SelectedRoad, i));
			PDI->DrawPoint(SelectedRoad->BaseCurve->GetPos(SelectedRoad->HeightPoints[i].Dist), i == PointIndex ? Color_Select : Color_Road, Size_Point, SDPG_Foreground);
		}
	}
}

bool FRoadTool_RoadChop::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	ARoadActor*& SelectedRoad = GetSelectedRoad();
	if (HandleClickRoad(HitProxy, Click))
		return true;
	if (SelectedRoad)
	{
		if (Click.GetKey() == EKeys::RightMouseButton)
		{
			const FVector HitLocation = LineTrace(InViewportClient);
			HRoadProxy* Proxy = HitProxyCast<HRoadProxy>(HitProxy);
			ARoadActor* HitRoad = Proxy ? Proxy->Road : nullptr;
			if (!IsValid(HitRoad) && HitLocation.X != WORLD_MAX)
			{
				// The road surface is easier to hit than the thin editor centerline.
				// Limit this fallback to the selected road so a surface click cannot
				// accidentally join another actor.
				const FVector2D SelectedUV = SelectedRoad->GetUV(HitLocation);
				URoadBoundary* PositiveEdge = SelectedRoad->GetRoadEdge(0);
				URoadBoundary* NegativeEdge = SelectedRoad->GetRoadEdge(1);
				if (IsUVValid(SelectedUV) && SelectedUV.X >= 0.0 && SelectedUV.X <= SelectedRoad->Length() &&
					PositiveEdge && NegativeEdge &&
					PositiveEdge->GetOffset(SelectedUV.X) >= SelectedUV.Y &&
					NegativeEdge->GetOffset(SelectedUV.X) <= SelectedUV.Y)
				{
					HitRoad = SelectedRoad;
				}
			}

			if (IsValid(HitRoad))
			{
				FScopedTransaction Transaction(LOCTEXT("RoadChop", "RoadChop"));
				if (SelectedRoad == HitRoad)
				{
					const FVector2D UV = SelectedRoad->GetUV(HitLocation);
					if (SelectedRoad->Chop(UV.X))
					{
						GetScene()->Rebuild();
					}
					else
					{
						Transaction.Cancel();
					}
				}
				else
				{
					SelectedRoad->Join(HitRoad);
					GetScene()->Rebuild();
				}
				return true;
			}
		}
	}
	return false;
}

void FRoadTool_RoadChop::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	DrawRoads(PDI, false);
}

bool FRoadTool_RoadSplit::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	ARoadActor*& SelectedRoad = GetSelectedRoad();
	if (HandleClickRoad(HitProxy, Click))
		return true;
	if (SelectedRoad)
	{
		if (Click.GetKey() == EKeys::RightMouseButton)
		{
			if (HRoadCurveProxy* Proxy = HitProxyCast<HRoadCurveProxy>(HitProxy))
			{
				const FScopedTransaction Transaction(LOCTEXT("RoadSplit", "RoadSplit"));
				SelectedRoad->Split(Cast<URoadBoundary>(Proxy->Curve));
				GetScene()->Rebuild();
				return true;
			}
		}
	}
	return false;
}

void FRoadTool_RoadSplit::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	if (ARoadActor* SelectedRoad = GetSelectedRoad())
	{
		if (SelectedRoad->Length() > 0)
		{
			for (URoadBoundary* Boundary : SelectedRoad->Boundaries)
			{
				PDI->SetHitProxy(new HRoadCurveProxy(Boundary));
				DrawCurve(PDI, Boundary->Curve, Color_Line, Thickness_Line);
			}
		}
	}
	else
		DrawRoads(PDI, false);
}

bool FRoadTool_JunctionLink::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	AJunctionActor*& Junction = GetSelectedJunction();
	if (HandleClickJunction(HitProxy, Click, &GateIndex, &LinkIndex))
	{
		GetEditWidget()->SetEditLink(Junction, GateIndex, LinkIndex);
		return true;
	}
	return false;
}

void FRoadTool_JunctionLink::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	if (AJunctionActor* Junction = GetSelectedJunction())
	{
		for (int i = 0; i < Junction->Gates.Num(); i++)
		{
			FJunctionGate& Gate = Junction->Gates[i];
			for (int j = 0; j < Gate.Links.Num(); j++)
			{
				if (ARoadActor* Road = Gate.Links[j].Road)
				{
					if (URoadCurve* Curve = (j == 1) ? (URoadCurve*)Road->BaseCurve : (URoadCurve*)Road->BaseCurve->RightLane)
					{
						PDI->SetHitProxy(new HJunctionProxy(Junction, i, j));
						if (i == GateIndex && j == LinkIndex)
							DrawCurve(PDI, Curve->Curve, Color_Select, Thickness_Road, DepthBias_Select);
						else
							DrawCurve(PDI, Curve->Curve, Color_Road, Thickness_Road);
					}
				}
			}
		}
	}
	else
		DrawJunctions(PDI);
}

FVector FRoadTool_LaneEdit::GetWidgetLocation() const
{
	if (CurrentLane)
		return CurrentLane->GetPos(CurrentLane->SegmentStart(SegmentIndex));
	return FRoadTool::GetWidgetLocation();
}

bool FRoadTool_LaneEdit::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (CurrentLane)
	{
		InMatrix = FRotationMatrix(CurrentLane->GetDir(CurrentLane->SegmentStart(SegmentIndex)).Rotation());
		return true;
	}
	return false;
}

bool FRoadTool_LaneEdit::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (HandleClickRoad(HitProxy, Click))
		return true;
	if (ARoadActor* SelectedRoad = GetSelectedRoad())
	{
		FVector2D UV = SelectedRoad->GetUV(LineTrace(InViewportClient));
		URoadLane* Lane = SelectedRoad->GetLane(UV);
		if (Click.GetKey() == EKeys::LeftMouseButton)
		{
			CurrentLane = Lane;
			SegmentIndex = CurrentLane ? CurrentLane->GetSegment(UV.X) : INDEX_NONE;
			GetEditWidget()->SetEditLaneSegment(CurrentLane, SegmentIndex);
			return true;
		}
		if (Click.GetKey() == EKeys::RightMouseButton && CurrentLane)
		{
			const FScopedTransaction Transaction(LOCTEXT("LaneEdit", "LaneEdit"));
			if (HRoadCurveProxy* Proxy = HitProxyCast<HRoadCurveProxy>(HitProxy))
			{
				SelectedRoad->CopyLane(CurrentLane, Proxy->Curve == CurrentLane->LeftBoundary);
				SelectedRoad->UpdateLanes();
				GetScene()->Rebuild();
			}
			else if (CurrentLane == Lane)
			{
				CurrentLane->Modify();
				SegmentIndex = CurrentLane->AddSegment(UV.X);
				GetEditWidget()->SetEditLaneSegment(CurrentLane, SegmentIndex);
			}
			else
			{
				CurrentLane = Lane;
				SegmentIndex = CurrentLane ? CurrentLane->GetSegment(UV.X) : INDEX_NONE;
				GetEditWidget()->SetEditLaneSegment(CurrentLane, SegmentIndex);
			}
			return true;
		}
	}
	return false;
}

bool FRoadTool_LaneEdit::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (FRoadTool::InputKey(ViewportClient, Viewport, Key, Event))
		return true;
	if (Event == IE_Pressed && CurrentLane && SegmentIndex != INDEX_NONE)
	{
		ARoadActor* SelectedRoad = GetSelectedRoad();
		if (Key == EKeys::Home)
		{
			if (CurrentLane->LeftBoundary->LeftLane)
			{
				CurrentLane = CurrentLane->LeftBoundary->LeftLane;
				SegmentIndex = FMath::Min(SegmentIndex, CurrentLane->Segments.Num() - 1);
				GetEditWidget()->SetEditLaneSegment(CurrentLane, SegmentIndex);
			}
			return true;
		}
		if (Key == EKeys::End)
		{
			if (CurrentLane->RightBoundary->RightLane)
			{
				CurrentLane = CurrentLane->RightBoundary->RightLane;
				SegmentIndex = FMath::Min(SegmentIndex, CurrentLane->Segments.Num() - 1);
				GetEditWidget()->SetEditLaneSegment(CurrentLane, SegmentIndex);
			}
			return true;
		}
		if (Key == EKeys::Tab)
		{
			SegmentIndex = (SegmentIndex + 1) % CurrentLane->Segments.Num();
			GetEditWidget()->SetEditLaneSegment(CurrentLane, SegmentIndex);
			return true;
		}
		if (Key == EKeys::Delete)
		{	
			CurrentLane->DeleteSegment(SegmentIndex);
			SelectedRoad->UpdateLanes();
			SelectedRoad->GetScene()->Rebuild();
			Reset();
			return true;
		}
	}
	return false;
}

bool FRoadTool_LaneEdit::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
	{
		if (CurrentLane)
		{
			FMatrix Mat = GLevelEditorModeTools().GetCustomDrawingCoordinateSystem();
			FVector LocalDrag = Mat.InverseTransformVector(InDrag);
			CurrentLane->Modify();
			CurrentLane->SegmentStart(SegmentIndex) += LocalDrag.X;
			CurrentLane->SnapSegment(SegmentIndex);
			CurrentLane->GetRoad()->UpdateLanes();
			LazyRebuild = true;
		}
		return true;
	}
	return false;
}

void FRoadTool_LaneEdit::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	if (ARoadActor* SelectedRoad = GetSelectedRoad())
	{
		if (SelectedRoad->Length() > 0)
		{
			PDI->SetHitProxy(nullptr);
			for (URoadBoundary* Boundary : SelectedRoad->Boundaries)
			{
				if (!CurrentLane || Boundary != CurrentLane->LeftBoundary && Boundary != CurrentLane->RightBoundary)
					DrawCurve(PDI, Boundary->Curve, Color_Line, Thickness_Line);
			}
			for (URoadLane* Lane : SelectedRoad->Lanes)
			{
				for (int i = 0; i < Lane->Segments.Num(); i++)
				{
					double Start = Lane->SegmentStart(i);
					double End = Lane->SegmentEnd(i);
					FColor BoundaryColor = (Lane == CurrentLane && i == SegmentIndex) ? Color_Select : Color_Line;
					FColor Color = (Lane == CurrentLane && (i == SegmentIndex || i - 1 == SegmentIndex)) ? Color_Select : Color_Line;
					if (Lane == CurrentLane)
					{
						PDI->SetHitProxy(new HRoadCurveProxy(Lane->RightBoundary));
						DrawCurve(PDI, Lane->RightBoundary->CreatePolyline(Start, End), BoundaryColor, Thickness_Line, DepthBias_Select);
						PDI->SetHitProxy(new HRoadCurveProxy(Lane->LeftBoundary));
						DrawCurve(PDI, Lane->LeftBoundary->CreatePolyline(Start, End), BoundaryColor, Thickness_Line, DepthBias_Select);
						PDI->SetHitProxy(nullptr);
					}
					DrawDivider(PDI, Lane, Start, Color);
					if (i + 1 == Lane->Segments.Num())
						DrawDivider(PDI, Lane, End, BoundaryColor);
				}
			}
		}
	}
	else
		DrawRoads(PDI, false);
}

bool FRoadTool_LaneCarve::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (HandleClickRoad(HitProxy, Click))
		return true;
	if (ARoadActor* SelectedRoad = GetSelectedRoad())
	{
		if (Click.GetKey() == EKeys::RightMouseButton)
		{
			if (HRoadCurveProxy* Proxy = HitProxyCast<HRoadCurveProxy>(HitProxy))
			{
				FVector2D EndUV = SelectedRoad->GetUV(LineTrace(InViewportClient));
				EndUV.Y = Proxy->Curve->GetOffset(EndUV.X);
				if (StartUV.X < 0)
					StartUV = EndUV;
				else
				{
					double StartOffset = StartUV.X;
					double EndOffset = EndUV.X;
					TArray<URoadBoundary*> StartBs = SelectedRoad->GetBoundaries(StartUV);
					TArray<URoadBoundary*> EndBs = SelectedRoad->GetBoundaries(EndUV);
					URoadLane* Lane = nullptr;
					int Side = INDEX_NONE;
					if (EndOffset < StartOffset)
					{
						Swap(StartBs, EndBs);
						Swap(StartOffset, EndOffset);
					}
					if (StartBs[0]->LeftLane == EndBs.Last()->RightLane)
					{
						Lane = StartBs[0]->LeftLane;
						Side = 0;
					}
					else if (StartBs.Last()->RightLane == EndBs[0]->LeftLane)
					{
						Lane = EndBs[0]->LeftLane;
						Side = 1;
					}
					if (Side != -1)
					{
						int LaneSide = Lane->GetSide();
						double LaneWidth = Lane->GetWidth((StartOffset + EndOffset) / 2);
						const FScopedTransaction Transaction(LOCTEXT("LaneCarve", "LaneCarve"));
						SelectedRoad->Modify();
						URoadLane* ForkLane = nullptr;
						if (URoadLane* SideLane = Lane->GetBoundary(Side)->GetLane(Side))
							if (FMath::IsNearlyZero(SideLane->GetWidth(StartOffset)) && FMath::IsNearlyZero(SideLane->GetWidth(EndOffset)))
								ForkLane = SideLane;
						if (!ForkLane)
							ForkLane = SelectedRoad->CopyLane(Lane, Side);
						{
							Lane->Modify();
						//	int Seg = Lane->AddSegment(EndOffset);
						//	Lane->Segments[Seg].LaneType = ELaneType::None;
							URoadBoundary* B = Lane->GetBoundary(LaneSide);
							B->Modify();
							int PrevIdx = B->AddLocalOffset(StartOffset);
							int NextIdx = B->AddLocalOffset(EndOffset);
						//	B->LocalOffsets[PrevIdx - 1].Offset = LaneWidth;
						//	B->LocalOffsets[PrevIdx].Offset = LaneWidth;
							B->LocalOffsets[NextIdx].Offset = 0;
							B->LocalOffsets[NextIdx + 1].Offset = 0;
							B->AddSegment(StartOffset);
							B->AddSegment(EndOffset);
						}
						{
							ForkLane->Modify();
						//	int Seg = ForkLane->AddSegment(StartOffset);
						//	ForkLane->Segments[Seg - 1].LaneType = ELaneType::None;
							URoadBoundary* B = ForkLane->GetBoundary(LaneSide);
							B->Modify();
							int PrevIdx = B->AddLocalOffset(StartOffset);
							int NextIdx = B->AddLocalOffset(EndOffset);
							B->LocalOffsets[PrevIdx - 1].Offset = 0;
							B->LocalOffsets[PrevIdx].Offset = 0;
							B->LocalOffsets[NextIdx].Offset = LaneWidth;
							B->LocalOffsets[NextIdx + 1].Offset = LaneWidth;
							B->AddSegment(StartOffset);
							B->AddSegment(EndOffset);
						}
						SelectedRoad->UpdateLanes();
						GetScene()->Rebuild();
					}
					Reset();
				}
				return true;
			}
		}
	}
	return false;
}

void FRoadTool_LaneCarve::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	if (ARoadActor* SelectedRoad = GetSelectedRoad())
	{
		if (SelectedRoad->Length() > 0)
		{
			for (URoadBoundary* Boundary : SelectedRoad->Boundaries)
			{
				PDI->SetHitProxy(new HRoadCurveProxy(Boundary));
				DrawCurve(PDI, Boundary->Curve, Color_Line, Thickness_Line);
			}
			if (StartUV.X >= 0)
			{
				FVector StartPos = SelectedRoad->GetPos(StartUV);
				PDI->DrawPoint(StartPos, Color_Line, Size_Point, SDPG_Foreground);
				FVector Pos = LineTrace(static_cast<FEditorViewportClient*>(Viewport->GetClient()));
				if (Pos.X < WORLD_MAX)
				{
					FVector2D EndUV = SelectedRoad->GetUV(Pos);
					FVector EndPos = SelectedRoad->GetPos(EndUV);
					PDI->DrawLine(StartPos, EndPos, Color_Line, SDPG_Foreground, Thickness_Line, 0, true);
				}
			}
		}
	}
	else
		DrawRoads(PDI, false);
}

FVector FRoadTool_LaneWidth::GetWidgetLocation() const
{
	if (CurrentBoundary && OffsetIndex != INDEX_NONE)
		return CurrentBoundary->GetPos(CurrentBoundary->LocalOffsets[OffsetIndex].Dist);
	return FRoadTool::GetWidgetLocation();
}

bool FRoadTool_LaneWidth::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (CurrentBoundary && OffsetIndex != INDEX_NONE)
	{
		InMatrix = FRotationMatrix(CurrentBoundary->GetDir(CurrentBoundary->LocalOffsets[OffsetIndex].Dist).Rotation());
		return true;
	}
	return false;
}

bool FRoadTool_LaneWidth::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (HandleClickRoad(HitProxy, Click))
		return true;
	if (ARoadActor* SelectedRoad = GetSelectedRoad())
	{
		FVector2D UV = SelectedRoad->GetUV(LineTrace(InViewportClient));
		if (Click.GetKey() == EKeys::LeftMouseButton)
		{
			if (HRoadCurveProxy* Proxy = HitProxyCast<HRoadCurveProxy>(HitProxy))
			{
				CurrentBoundary = Cast<URoadBoundary>(Proxy->Curve);
				OffsetIndex = Proxy->Index;
				GetEditWidget()->SetEditBoundaryOffset(CurrentBoundary, OffsetIndex);
			}
			else
				Reset();
			return true;
		}
		if (Click.GetKey() == EKeys::RightMouseButton)
		{
			if (HRoadCurveProxy* Proxy = HitProxyCast<HRoadCurveProxy>(HitProxy))
			{
				const FScopedTransaction Transaction(LOCTEXT("LaneWidth", "LaneWidth"));
				CurrentBoundary = Cast<URoadBoundary>(Proxy->Curve);
				CurrentBoundary->Modify();
				OffsetIndex = CurrentBoundary->AddLocalOffset(UV.X);
				GetEditWidget()->SetEditBoundaryOffset(CurrentBoundary, OffsetIndex);
				return true;
			}
		}
	}
	return false;
}

bool FRoadTool_LaneWidth::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (FRoadTool::InputKey(ViewportClient, Viewport, Key, Event))
		return true;
	if (Event == IE_Pressed && CurrentBoundary && OffsetIndex != INDEX_NONE)
	{
		ARoadActor* SelectedRoad = GetSelectedRoad();
		if (Key == EKeys::Delete)
		{
			CurrentBoundary->DeleteOffset(OffsetIndex);
			OffsetIndex = INDEX_NONE;
			SelectedRoad->UpdateLanes();
			SelectedRoad->GetScene()->Rebuild();		
			return true;
		}
	}
	return false;
}

bool FRoadTool_LaneWidth::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
	{
		if (CurrentBoundary)
		{
			FMatrix Mat = GLevelEditorModeTools().GetCustomDrawingCoordinateSystem();
			FVector LocalDrag = Mat.InverseTransformVector(InDrag);
			CurrentBoundary->Modify();
			CurrentBoundary->LocalOffsets[OffsetIndex].Dist += LocalDrag.X;
			CurrentBoundary->LocalOffsets[OffsetIndex].Offset += LocalDrag.Y;
			CurrentBoundary->SnapOffset(OffsetIndex);
			CurrentBoundary->GetRoad()->UpdateLanes();
			LazyRebuild = true;
		}
		return true;
	}
	return false;
}

void FRoadTool_LaneWidth::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	if (ARoadActor* SelectedRoad = GetSelectedRoad())
	{
		if (SelectedRoad->Length() > 0)
		{
			for (URoadBoundary* Boundary : SelectedRoad->Boundaries)
			{
				if (Boundary != CurrentBoundary)
				{
					PDI->SetHitProxy(new HRoadCurveProxy(Boundary));
					DrawCurve(PDI, Boundary->Curve, Color_Line, Thickness_Line);
				}
			}
			if (CurrentBoundary)
			{
				PDI->SetHitProxy(new HRoadCurveProxy(CurrentBoundary));
				DrawCurve(PDI, CurrentBoundary->Curve, Color_Select, Thickness_Line, DepthBias_Select);
				for (int i = 0; i < CurrentBoundary->LocalOffsets.Num(); i++)
				{
					double Dist = CurrentBoundary->LocalOffsets[i].Dist;
					FColor Color = i == OffsetIndex ? Color_Select : Color_Line;
					PDI->SetHitProxy(new HRoadCurveProxy(CurrentBoundary, i));
					PDI->DrawPoint(CurrentBoundary->GetPos(Dist), Color, Size_Point, SDPG_Foreground);
					if (CurrentBoundary != SelectedRoad->BaseCurve)
					{
						URoadLane* Lane = CurrentBoundary->GetSide() ? CurrentBoundary->RightLane : CurrentBoundary->LeftLane;
						DrawDivider(PDI, Lane, Dist, Color);
					}
				}
			}
		}
	}
	else
		DrawRoads(PDI, false);
}

FVector FRoadTool_MarkingLane::GetWidgetLocation() const
{
	if (CurrentBoundary)
		return CurrentBoundary->GetPos(CurrentBoundary->SegmentStart(SegmentIndex));
	return FRoadTool::GetWidgetLocation();
}

bool FRoadTool_MarkingLane::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (CurrentBoundary)
	{
		InMatrix = FRotationMatrix(CurrentBoundary->GetDir(CurrentBoundary->SegmentStart(SegmentIndex)).Rotation());
		return true;
	}
	return false;
}

bool FRoadTool_MarkingLane::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (HandleClickRoad(HitProxy, Click))
		return true;
	if (ARoadActor* SelectedRoad = GetSelectedRoad())
	{
		FVector2D UV = SelectedRoad->GetUV(LineTrace(InViewportClient));
		if (Click.GetKey() == EKeys::LeftMouseButton)
		{
			if (HRoadCurveProxy* Proxy = HitProxyCast<HRoadCurveProxy>(HitProxy))
			{
				CurrentBoundary = Cast<URoadBoundary>(Proxy->Curve);
				SegmentIndex = Proxy->Index;
				GetEditWidget()->SetEditBoundarySegment(CurrentBoundary, SegmentIndex);
			}
			else
				Reset();
			return true;
		}
		if (Click.GetKey() == EKeys::RightMouseButton)
		{
			if (HRoadCurveProxy* Proxy = HitProxyCast<HRoadCurveProxy>(HitProxy))
			{
				const FScopedTransaction Transaction(LOCTEXT("MarkingLane", "MarkingLane"));
				CurrentBoundary = Cast<URoadBoundary>(Proxy->Curve);
				CurrentBoundary->Modify();
				SegmentIndex = CurrentBoundary->AddSegment(UV.X);
				GetEditWidget()->SetEditBoundarySegment(CurrentBoundary, SegmentIndex);
				return true;
			}
		}
	}
	return false;
}

bool FRoadTool_MarkingLane::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (FRoadTool::InputKey(ViewportClient, Viewport, Key, Event))
		return true;
	if (Event == IE_Pressed && CurrentBoundary && SegmentIndex != INDEX_NONE)
	{
		ARoadActor* SelectedRoad = GetSelectedRoad();
		if (Key == EKeys::Home)
		{
			if (CurrentBoundary->LeftLane->LeftBoundary)
			{
				CurrentBoundary = CurrentBoundary->LeftLane->LeftBoundary;
				SegmentIndex = FMath::Min(SegmentIndex, CurrentBoundary->Segments.Num() - 1);
				GetEditWidget()->SetEditBoundarySegment(CurrentBoundary, SegmentIndex);
			}
			return true;
		}
		if (Key == EKeys::End)
		{
			if (CurrentBoundary->RightLane->RightBoundary)
			{
				CurrentBoundary = CurrentBoundary->RightLane->RightBoundary;
				SegmentIndex = FMath::Min(SegmentIndex, CurrentBoundary->Segments.Num() - 1);
				GetEditWidget()->SetEditBoundarySegment(CurrentBoundary, SegmentIndex);
			}
			return true;
		}
		if (Key == EKeys::Tab)
		{
			SegmentIndex = (SegmentIndex + 1) % CurrentBoundary->Segments.Num();
			GetEditWidget()->SetEditBoundarySegment(CurrentBoundary, SegmentIndex);
			return true;
		}
		if (Key == EKeys::Delete)
		{
			CurrentBoundary->DeleteSegment(SegmentIndex);
			SelectedRoad->UpdateLanes();
			SelectedRoad->GetScene()->Rebuild();
			Reset();
			return true;
		}
	}
	return false;
}

bool FRoadTool_MarkingLane::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
	{
		if (CurrentBoundary)
		{
			FMatrix Mat = GLevelEditorModeTools().GetCustomDrawingCoordinateSystem();
			FVector LocalDrag = Mat.InverseTransformVector(InDrag);
			CurrentBoundary->Modify();
			CurrentBoundary->SegmentStart(SegmentIndex) += LocalDrag.X;
			CurrentBoundary->SnapSegment(SegmentIndex);
			LazyRebuild = true;
		}
		return true;
	}
	return false;
}

void FRoadTool_MarkingLane::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	if (ARoadActor* SelectedRoad = GetSelectedRoad())
	{
		if (SelectedRoad->Length() > 0)
		{
			for (URoadBoundary* Boundary : SelectedRoad->Boundaries)
			{
				for (int i = 0; i < Boundary->Segments.Num(); i++)
				{
					if (Boundary != SelectedRoad->BaseCurve && Boundary->IsZeroOffset(i))
						continue;
					double Start = Boundary->SegmentStart(i);
					double End = Boundary->SegmentEnd(i);
					PDI->SetHitProxy(new HRoadCurveProxy(Boundary, i));
					FColor Color = (Boundary == CurrentBoundary && i == SegmentIndex) ? Color_Select : Color_Line;
					float DepthBias = (Boundary == CurrentBoundary && i == SegmentIndex) ? DepthBias_Select : 0;
					DrawCurve(PDI, Boundary->CreatePolyline(Start, End), Color, Thickness_Line, DepthBias);
					DrawPoint(PDI, Boundary, Start, Color);
					if (i + 1 == Boundary->Segments.Num())
						DrawPoint(PDI, Boundary, End, Color);
				}
			}
		}
	}
	else
		DrawRoads(PDI, true);
}

FVector FRoadTool_MarkingPoint::GetWidgetLocation() const
{
	if (CurrentMarking)
		return CurrentMarking->GetRoad()->GetPos(CurrentMarking->Point);
	return FRoadTool::GetWidgetLocation();
}

bool FRoadTool_MarkingPoint::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (CurrentMarking)
	{
		InMatrix = FRotationMatrix(CurrentMarking->GetRoad()->GetDir(CurrentMarking->Point.X).Rotation());
		return true;
	}
	return false;
}

bool FRoadTool_MarkingPoint::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (HandleClickRoad(HitProxy, Click))
		return true;
	ARoadActor* SelectedRoad = GetSelectedRoad();
	if (Click.GetKey() == EKeys::LeftMouseButton)
	{
		if (HRoadMarkingProxy* Proxy = HitProxyCast<HRoadMarkingProxy>(HitProxy))
		{
			CurrentMarking = Cast<UMarkingPoint>(Proxy->Marking);
			GetEditWidget()->SetEditMarkingPoint(CurrentMarking);
		}
		else
			Reset();
		return true;
	}
	if (Click.GetKey() == EKeys::RightMouseButton)
	{
		if (SelectedRoad)
		{
			const FScopedTransaction Transaction(LOCTEXT("MarkingPoint", "MarkingPoint"));
			SelectedRoad->Modify();
			CurrentMarking = SelectedRoad->AddMarkingPoint(SelectedRoad->GetUV(LineTrace(InViewportClient)));
			GetEditWidget()->SetEditMarkingPoint(CurrentMarking);
		}
		return true;
	}
	return false;
}

bool FRoadTool_MarkingPoint::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (FRoadTool::InputKey(ViewportClient, Viewport, Key, Event))
		return true;
	if (Event == IE_Pressed && CurrentMarking)
	{
		if (Key == EKeys::Delete)
		{
			ARoadActor* SelectedRoad = GetSelectedRoad();
			SelectedRoad->DeleteMarking(CurrentMarking);
			CurrentMarking = nullptr;
			SelectedRoad->GetScene()->Rebuild();
			return true;
		}
	}
	return false;
}

bool FRoadTool_MarkingPoint::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
	{
		if (CurrentMarking)
		{
			FMatrix Mat = GLevelEditorModeTools().GetCustomDrawingCoordinateSystem();
			FVector LocalDrag = Mat.InverseTransformVector(InDrag);
			CurrentMarking->Modify();
			CurrentMarking->Point += (FVector2D&)LocalDrag;
			LazyRebuild = true;
		}
		return true;
	}
	return false;
}

void FRoadTool_MarkingPoint::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	auto Draw = [&](ARoadActor* Road)
	{
		for (URoadMarking* Marking : Road->Markings)
		{
			if (UMarkingPoint* MarkingPoint = Cast<UMarkingPoint>(Marking))
			{
				PDI->SetHitProxy(new HRoadMarkingProxy(MarkingPoint));
				PDI->DrawPoint(MarkingPoint->GetRoad()->GetPos(MarkingPoint->Point), Marking == CurrentMarking ? Color_Select : Color_Line, Size_Point, SDPG_Foreground);
			}
		}
	};
	if (ARoadActor* SelectedRoad = GetSelectedRoad())
		Draw(SelectedRoad);
	else
		DrawRoads(PDI, true);
}

FVector FRoadTool_MarkingCurve::GetWidgetLocation() const
{
	if (IsValid(CurrentMarking) && !CurrentMarking->bUseGeneratedWorldPoints && !CurrentMarking->Points.IsEmpty())
	{
		ARoadActor* Road = CurrentMarking->GetRoad();
		if (IsValid(Road))
		{
			const FVector2D UV = CurrentMarking->Points.IsValidIndex(PointIndex)
				? CurrentMarking->Points[PointIndex].GetUV(SubIndex)
				: CurrentMarking->Center();
			return Road->GetPos(UV);
		}
	}
	return FRoadTool::GetWidgetLocation();
}

bool FRoadTool_MarkingCurve::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (IsValid(CurrentMarking) && !CurrentMarking->bUseGeneratedWorldPoints && !CurrentMarking->Points.IsEmpty())
	{
		ARoadActor* Road = CurrentMarking->GetRoad();
		if (IsValid(Road))
		{
			const double Dist = CurrentMarking->Points.IsValidIndex(PointIndex)
				? CurrentMarking->Points[PointIndex].GetUV(SubIndex).X
				: CurrentMarking->Center().X;
			InMatrix = FRotationMatrix(Road->GetDir(Dist).Rotation());
			return true;
		}
	}
	return false;
}

bool FRoadTool_MarkingCurve::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (HandleClickRoad(HitProxy, Click))
		return true;
	ARoadActor* SelectedRoad = GetSelectedRoad();
	auto ClickProxy = [&](HRoadMarkingProxy* Proxy)
	{
		UMarkingCurve* ClickedMarking = Proxy ? Cast<UMarkingCurve>(Proxy->Marking) : nullptr;
		if (!IsValid(ClickedMarking) || ClickedMarking->bUseGeneratedWorldPoints)
		{
			Reset();
			return;
		}
		CurrentMarking = ClickedMarking;
		PointIndex = Proxy->Index;
		if (PointIndex != INDEX_NONE && !CurrentMarking->Points.IsValidIndex(PointIndex))
			PointIndex = INDEX_NONE;
		SubIndex = Proxy->SubId;
		if (SRoadEdit* EditWidget = GetEditWidget())
			EditWidget->SetEditMarkingCurve(CurrentMarking, PointIndex);
	};
	auto RebuildEditedMarking = [&]()
	{
		UMarkingCurve* EditedMarking = CurrentMarking;
		ARoadActor* MarkingRoad = IsValid(EditedMarking) ? EditedMarking->GetRoad() : nullptr;
		RebuildRoadMarkingMesh(EditedMarking);
		if (!IsValid(EditedMarking) || !IsValid(MarkingRoad) || !MarkingRoad->Markings.Contains(EditedMarking))
		{
			Reset();
			return;
		}
		CurrentMarking = EditedMarking;
		if (PointIndex != INDEX_NONE && !CurrentMarking->Points.IsValidIndex(PointIndex))
			PointIndex = INDEX_NONE;
		if (SRoadEdit* EditWidget = GetEditWidget())
			EditWidget->SetEditMarkingCurve(CurrentMarking, PointIndex);
	};
	if (Click.GetKey() == EKeys::LeftMouseButton)
	{
		if (HRoadMarkingProxy* Proxy = HitProxyCast<HRoadMarkingProxy>(HitProxy))
			ClickProxy(Proxy);
		else
			Reset();
		return true;
	}
	if (Click.GetKey() == EKeys::RightMouseButton)
	{
		const FScopedTransaction Transaction(LOCTEXT("MarkingCurve", "MarkingCurve"));
		HRoadMarkingProxy* Proxy = HitProxyCast<HRoadMarkingProxy>(HitProxy);
		bool bMarkingChanged = false;
		if (IsValid(CurrentMarking) && !CurrentMarking->bUseGeneratedWorldPoints && CurrentMarking->IsEndPoint(PointIndex))
		{
			if (Proxy)
			{
				if (Proxy->Marking == CurrentMarking && Proxy->Index != PointIndex && CurrentMarking->IsEndPoint(Proxy->Index))
				{
					CurrentMarking->MakeClose();
					bMarkingChanged = true;
				}
				else
					ClickProxy(Proxy);
			}
			else
			{
				ARoadActor* MarkingRoad = CurrentMarking->GetRoad();
				if (IsValid(MarkingRoad))
				{
					CurrentMarking->InsertPoint(MarkingRoad->GetUV(LineTrace(InViewportClient)), PointIndex);
					bMarkingChanged = true;
				}
			}
		}
		else if (Proxy)
			ClickProxy(Proxy);
		else if (SelectedRoad)
		{
			SelectedRoad->Modify();
			CurrentMarking = SelectedRoad->AddMarkingCurve();
			PointIndex = INDEX_NONE;
			CurrentMarking->InsertPoint(SelectedRoad->GetUV(LineTrace(InViewportClient)), PointIndex);
			bMarkingChanged = true;
		}
		if (bMarkingChanged)
			RebuildEditedMarking();
		return true;
	}
	return false;
}

bool FRoadTool_MarkingCurve::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (FRoadTool::InputKey(ViewportClient, Viewport, Key, Event))
		return true;
	if (Event == IE_Pressed && IsValid(CurrentMarking) && !CurrentMarking->bUseGeneratedWorldPoints)
	{
		if (Key == EKeys::Delete)
		{
			ARoadActor* MarkingRoad = CurrentMarking->GetRoad();
			if (!IsValid(MarkingRoad))
			{
				Reset();
				return true;
			}
			MarkingRoad->DeleteMarking(CurrentMarking);
			CurrentMarking = nullptr;
			TArray<FJunctionSlot> Slots;
			if (!MarkingRoad->IsLink())
			{
				if (ARoadScene* MarkingScene = MarkingRoad->GetScene(); IsValid(MarkingScene))
					Slots = MarkingScene->GetJunctionSlots(MarkingRoad);
			}
			MarkingRoad->BuildMesh(Slots);
			return true;
		}
	}
	return false;
}

bool FRoadTool_MarkingCurve::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
	{
		if (IsValid(CurrentMarking) && !CurrentMarking->bUseGeneratedWorldPoints && !CurrentMarking->Points.IsEmpty())
		{
			FMatrix Mat = GLevelEditorModeTools().GetCustomDrawingCoordinateSystem();
			FVector LocalDrag = Mat.InverseTransformVector(InDrag);
			CurrentMarking->Modify();
			if (CurrentMarking->Points.IsValidIndex(PointIndex))
				CurrentMarking->Points[PointIndex].ApplyDelta(SubIndex, (FVector2D&)LocalDrag);
			else
			{
				for (FMarkingCurvePoint& Point : CurrentMarking->Points)
					Point.Pos += (FVector2D&)LocalDrag;
			}
			LazyRebuild = true;
		}
		return true;
	}
	return false;
}

bool FRoadTool_MarkingCurve::EndModify()
{
	if (LazyRebuild)
	{
		LazyRebuild = false;
		if (!GIsTransacting)
			RebuildRoadMarkingMesh(CurrentMarking);
	}
	return true;
}

void FRoadTool_MarkingCurve::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	auto Draw = [&](ARoadActor* Road)
	{
		for (URoadMarking* Marking : Road->Markings)
		{
			if (UMarkingCurve* MarkingCurve = Cast<UMarkingCurve>(Marking); IsValid(MarkingCurve))
			{
				PDI->SetHitProxy(MarkingCurve->bUseGeneratedWorldPoints ? nullptr : new HRoadMarkingProxy(Marking, INDEX_NONE));
				DrawCurve(PDI, MarkingCurve->CreatePolyline(), CurrentMarking == Marking ? Color_Select : Color_Line, Thickness_Road);
				if (!MarkingCurve->bUseGeneratedWorldPoints && CurrentMarking == MarkingCurve)
				{
					for (int i = 0; i < MarkingCurve->Points.Num(); i++)
					{
						for (int j = 0; j < (i == PointIndex ? 3 : 1); j++)
						{
							FVector Pos = MarkingCurve->GetRoad()->GetPos(MarkingCurve->Points[i].GetUV(j));
							PDI->SetHitProxy(new HRoadMarkingProxy(Marking, i, j));
							PDI->DrawPoint(Pos, i == PointIndex ? Color_Select : Color_Line, Size_Point, SDPG_Foreground);
							if (j > 0)
								PDI->DrawLine(Pos, MarkingCurve->GetRoad()->GetPos(MarkingCurve->Points[i].GetUV(0)), Color_Select, SDPG_Foreground, Thickness_Line);
						}
					}
				}
			}
		}
	};
	if (ARoadActor* SelectedRoad = GetSelectedRoad())
		Draw(SelectedRoad);
	else
		DrawRoads(PDI, true);
}

FVector FRoadTool_GroundEdit::GetWidgetLocation() const
{
	AGroundActor* Ground = GetSelectedGround();
	if (PointIndex != INDEX_NONE && Ground->Points[PointIndex].Road == nullptr)
		return Ground->ManualPoints[Ground->Points[PointIndex].Index];
	return FRoadTool::GetWidgetLocation();
}

bool FRoadTool_GroundEdit::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	ARoadScene* Scene = GetScene();
	AGroundActor*& SelectedGround = GetSelectedGround();
	if (Click.GetKey() == EKeys::LeftMouseButton)
	{
		if (HGroundProxy* Proxy = HitProxyCast<HGroundProxy>(HitProxy))
		{
			SelectedGround = Proxy->Ground;
			PointIndex = Proxy->Index;
			GetEditWidget()->SetEditGround(SelectedGround, PointIndex);
		}
		return true;
	}
	if (Click.GetKey() == EKeys::RightMouseButton)
	{
		if (SelectedGround && SelectedGround->IsEndPoint(PointIndex))
		{
			const FScopedTransaction Transaction(LOCTEXT("GroundEdit", "GroundEdit"));
			if (HGroundProxy* Proxy = HitProxyCast<HGroundProxy>(HitProxy))
			{
				if (Proxy->Ground != SelectedGround)
				{
					if (Proxy->Ground->IsEndPoint(Proxy->Index))
					{
						SelectedGround->Modify();
						SelectedGround->Join(Proxy->Ground, PointIndex);
						Scene->Grounds.Remove(Proxy->Ground);
						Proxy->Ground->Destroy();
						Scene->Rebuild();
						GetEditWidget()->SetEditGround(SelectedGround, PointIndex);
					}
				}
				else if (Proxy->Index != PointIndex)
				{
					if (Proxy->Ground->IsEndPoint(Proxy->Index))
					{
						SelectedGround->Modify();
						SelectedGround->bClosedLoop = true;
						Scene->Rebuild();
						GetEditWidget()->SetEditGround(SelectedGround, PointIndex);
					}
				}
			}
			else
			{
				FGroundPoint& Point = SelectedGround->Points[PointIndex];
				TMap<ARoadActor*, TArray<FJunctionSlot>> RoadSlots = Scene->GetAllJunctionSlots();
				FVector P = Point.Road ? Point.GetPos(RoadSlots) : SelectedGround->ManualPoints[Point.Index];
				FRay Ray = GetRay(InViewportClient);
				FVector Pos = FMath::RayPlaneIntersection(Ray.Origin, Ray.Direction, FPlane(FVector(0, 0, P.Z), FVector::UpVector));
				SelectedGround->Modify();
				SelectedGround->AddManualPoint(Pos, PointIndex);
				Scene->Rebuild();
				GetEditWidget()->SetEditGround(SelectedGround, PointIndex);
			}
		}
		return true;
	}
	return false;
}

bool FRoadTool_GroundEdit::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
	{
		AGroundActor* Ground = GetSelectedGround();
		if (PointIndex != INDEX_NONE && Ground->Points[PointIndex].Road == nullptr)
		{
			Ground->Modify();
			Ground->ManualPoints[Ground->Points[PointIndex].Index] += InDrag;
			LazyRebuild = true;
		}
		return true;
	}
	return false;
}

void FRoadTool_GroundEdit::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	ARoadScene* Scene = GetScene();
	TMap<ARoadActor*, TArray<FJunctionSlot>> RoadSlots = Scene->GetAllJunctionSlots();
	AGroundActor* SelectedGround = GetSelectedGround();
	for (AGroundActor* Ground : Scene->Grounds)
	{
		TArray<FVector> Vertices = Ground->GetVertices(RoadSlots);
		FColor Color = (Ground == SelectedGround) ? Color_Select : Color_Road;
		PDI->SetHitProxy(new HGroundProxy(Ground));
		for (int i = 0; i < Vertices.Num() - !Ground->bClosedLoop; i++)
		{
			const FVector& Start = Vertices[i];
			const FVector& End = Vertices[(i + 1) % Vertices.Num()];
			PDI->DrawLine(Start, End, Color, SDPG_Foreground, Thickness_Road, 0, true);
#if 0
			FVector Dir = (End - Start).GetSafeNormal();
			FVector N(-Dir.Y, Dir.X, Dir.Z);
			FVector Center = (Start + End) / 2;
			FVector Left = Center - N * 50 - Dir * 100;
			FVector Right = Center + N * 50 - Dir * 100;
			PDI->DrawLine(Left, Center, FColor::Blue, SDPG_Foreground, Thickness_Line, 0, true);
			PDI->DrawLine(Right, Center, FColor::Blue, SDPG_Foreground, Thickness_Line, 0, true);
#endif
		}
		for (int i = 0; i < Ground->Points.Num(); i++)
		{
			FGroundPoint& Point = Ground->Points[i];
			PDI->SetHitProxy(new HGroundProxy(Ground, i));
			FVector Pos = Point.Road ? Point.GetPos(RoadSlots) : Ground->ManualPoints[Point.Index];
			PDI->DrawPoint(Pos, (Ground == SelectedGround && PointIndex == i) ? Color_Select : Color_Road, Size_Point, SDPG_Foreground);
		}
	}
}

FEdModeRoad::FEdModeRoad()
{
	Tools.Add(new FRoadTool_File);
	Tools.Add(new FRoadTool_RoadPlan);
	Tools.Add(new FRoadTool_RoadHeight);
	Tools.Add(new FRoadTool_RoadChop);
	Tools.Add(new FRoadTool_RoadSplit);
	Tools.Add(new FRoadTool_JunctionLink);
	Tools.Add(new FRoadTool_LaneEdit);
	Tools.Add(new FRoadTool_LaneCarve);
	Tools.Add(new FRoadTool_LaneWidth);
	Tools.Add(new FRoadTool_MarkingLane);
	Tools.Add(new FRoadTool_MarkingPoint);
	Tools.Add(new FRoadTool_MarkingCurve);
	Tools.Add(new FRoadTool_GroundEdit);
	Tools.Add(new FRoadTool_Settings);
}

FEdModeRoad::~FEdModeRoad()
{
	CancelUndoRedoRebuild();
	for (FModeTool* Tool : Tools)
		delete Tool;
}

int FEdModeRoad::GetToolIndex()
{
	return Tools.Find(CurrentTool);
}

void FEdModeRoad::SetToolIndex(int Index)
{
	if (!Tools.IsValidIndex(Index))
	{
		return;
	}

	FRoadTool* RoadTool = static_cast<FRoadTool*>(Tools[Index]);
	if (!RoadTool)
	{
		return;
	}

	RoadTool->Reset();
	SetCurrentTool(RoadTool);
	FEditorViewportClient* Client = GLevelEditorModeTools().GetFocusedViewportClient();
	if (Client)
	{
		Client->Invalidate();
	}
}

void FEdModeRoad::OnPostUndoRedo()
{
	// FEditorDelegates::PostUndoRedo runs only after every object and editor
	// undo client has restored its state. Reset any tool-local pointers now,
	// then coalesce the expensive scene regeneration onto the next editor tick.
	PostUndo();
	QueueUndoRedoRebuild();
}

void FEdModeRoad::QueueUndoRedoRebuild()
{
	if (bUndoRedoRebuildQueued)
	{
		return;
	}

	bUndoRedoRebuildQueued = true;
	UndoRedoRebuildHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FEdModeRoad::RunUndoRedoRebuild));
}

bool FEdModeRoad::RunUndoRedoRebuild(float DeltaTime)
{
	// A nested editor operation can start a transaction before the queued tick.
	// Keep waiting rather than regenerating from a partially transacted graph.
	if (GIsTransacting)
	{
		return true;
	}

	bUndoRedoRebuildQueued = false;
	UndoRedoRebuildHandle.Reset();
	if (!IsValid(Scene) || !IsValid(Scene->GetWorld()))
	{
		return false;
	}

	Scene->RemoveInvalidReferences();

	// RoadPoints and HeightPoints are authored state. Derived segments and
	// offset polylines can be restored in a different object order by Unreal,
	// so reconstruct them once from the final transaction state before links.
	for (ARoadActor* Road : Scene->Roads)
	{
		if (!IsValid(Road) || !Road->BaseCurve || Road->RoadPoints.Num() < 2 || Road->HeightPoints.Num() < 2)
		{
			UE_LOG(LogTemp, Warning, TEXT("RoadBuilder skipped undo/redo rebuild for %s because its authored curve is incomplete."),
				IsValid(Road) ? *Road->GetName() : TEXT("invalid road"));
			return false;
		}
		for (int32 PointIndex = 0; PointIndex < Road->RoadPoints.Num(); ++PointIndex)
		{
			const FVector2D& Position = Road->RoadPoints[PointIndex].Pos;
			if (Position.ContainsNaN() ||
				(PointIndex > 0 && Position.Equals(Road->RoadPoints[PointIndex - 1].Pos, UE_DOUBLE_SMALL_NUMBER)))
			{
				UE_LOG(LogTemp, Warning, TEXT("RoadBuilder skipped undo/redo rebuild for %s because its authored points are invalid."), *Road->GetName());
				return false;
			}
		}
	}

	TSet<ARoadActor*> UpdatedRoads;
	for (ARoadActor* Road : Scene->Roads)
	{
		if (!UpdatedRoads.Contains(Road))
		{
			UpdatedRoads.Add(Road);
			Road->UpdateCurve(UpdatedRoads);
		}
	}

	Scene->Rebuild();
	SelectedRoad = IsValid(SelectedRoad) ? SelectedRoad : nullptr;
	SelectedGround = IsValid(SelectedGround) ? SelectedGround : nullptr;
	SelectedJunction = IsValid(SelectedJunction) ? SelectedJunction : nullptr;
	return false;
}

void FEdModeRoad::CancelUndoRedoRebuild()
{
	if (UndoRedoRebuildHandle.IsValid())
	{
		FTSTicker::RemoveTicker(UndoRedoRebuildHandle);
		UndoRedoRebuildHandle.Reset();
	}
	bUndoRedoRebuildQueued = false;
}

void FEdModeRoad::Enter()
{
	FEdMode::Enter();
	FEditorDelegates::PostUndoRedo.AddRaw(this, &FEdModeRoad::OnPostUndoRedo);
	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShareable(new FRoadEdModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
		Toolkit->SetCurrentPalette(TEXT("Road"));
	}
	GLevelEditorModeTools().SetCoordSystem(COORD_Local);
	if (UWorld* World = GetWorld())
	{
		Scene = Cast<ARoadScene>(UGameplayStatics::GetActorOfClass(World, ARoadScene::StaticClass()));
		if (!Scene)
		{
			Scene = World->SpawnActor<ARoadScene>();
		}
	}
}

void FEdModeRoad::Exit()
{
	FEditorDelegates::PostUndoRedo.RemoveAll(this);
	CancelUndoRedoRebuild();
	Scene = nullptr;
	SelectedRoad = nullptr;
	SelectedGround = nullptr;
	SelectedJunction = nullptr;
	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}
	FEdMode::Exit();
}

void FEdModeRoad::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	const FScopedTransaction Transaction(LOCTEXT("NotifyPreChange", "NotifyPreChange"));
	if (FRoadTool* RoadTool = static_cast<FRoadTool*>(CurrentTool))
	{
		RoadTool->NotifyPreChange(PropertyAboutToChange);
	}
}

void FEdModeRoad::PostUndo()
{
	if (FRoadTool* RoadTool = static_cast<FRoadTool*>(CurrentTool))
	{
		RoadTool->LazyRebuild = false;
		RoadTool->Reset();
	}
}

bool FEdModeRoad::GetCursor(EMouseCursor::Type& OutCursor) const
{
	FEditorViewportClient* Client = GLevelEditorModeTools().GetFocusedViewportClient();
	if (!Client || !Client->Viewport)
	{
		return FEdMode::GetCursor(OutCursor);
	}
	HHitProxy* HitProxy = Client->Viewport->GetHitProxy(Client->GetCachedMouseX(), Client->GetCachedMouseY());
	if (HitProxy)
	{
		OutCursor = HitProxy->IsA(HActor::StaticGetType()) ? EMouseCursor::Default : HitProxy->GetMouseCursor();
		return true;
	}
	return FEdMode::GetCursor(OutCursor);
}

bool FEdModeRoad::ShouldDrawWidget() const
{
	if (FRoadTool* RoadTool = static_cast<FRoadTool*>(CurrentTool))
	{
		if (RoadTool->ShouldDrawWidget())
		{
			return true;
		}
	}
	return FEdMode::ShouldDrawWidget();
}

FVector FEdModeRoad::GetWidgetLocation() const
{
	if (FRoadTool* RoadTool = static_cast<FRoadTool*>(CurrentTool))
	{
		return RoadTool->GetWidgetLocation();
	}
	return FEdMode::GetWidgetLocation();
}

bool FEdModeRoad::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (FRoadTool* RoadTool = static_cast<FRoadTool*>(CurrentTool))
	{
		return RoadTool->GetCustomDrawingCoordinateSystem(InMatrix, InData);
	}
	return FEdMode::GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

EAxisList::Type FEdModeRoad::GetWidgetAxisToDraw(UE::Widget::EWidgetMode InWidgetMode) const
{
	if (InWidgetMode != UE::Widget::WM_Translate)
		return EAxisList::None;
	if (FRoadTool* RoadTool = static_cast<FRoadTool*>(CurrentTool))
	{
		return RoadTool->GetWidgetAxisToDraw();
	}
	return FEdMode::GetWidgetAxisToDraw(InWidgetMode);
}

bool FEdModeRoad::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (FRoadTool* RoadTool = static_cast<FRoadTool*>(CurrentTool))
	{
		if (RoadTool->HandleClick(InViewportClient, HitProxy, Click))
		{
			return true;
		}
	}
	else
	{
		return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
	}
#if 0
	if (Click.GetKey() == EKeys::RightMouseButton)
	{
		TSharedPtr<SEditorViewport> ViewportWidget = InViewportClient->GetEditorViewportWidget();
		TSharedPtr<SWidget> MenuContents = ((FRoadTool*)CurrentTool)->GenerateContextMenu();
		FSlateApplication::Get().PushMenu(ViewportWidget.ToSharedRef(), FWidgetPath(), MenuContents.ToSharedRef(), Click.GetClickPos(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		return true;
	}
#endif
	return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
}

void FEdModeRoad::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
	const USettings_Global* Settings = GetDefault<USettings_Global>();
	if (!Settings || !Settings->DisplayGoreDiagnostics || !IsValid(Scene) || !View || !Canvas || !GEngine)
	{
		return;
	}

	TArray<AJunctionActor*> DiagnosticJunctions;
	if (IsValid(SelectedJunction) && SelectedJunction->GetScene() == Scene)
	{
		DiagnosticJunctions.Add(SelectedJunction);
	}
	else
	{
		for (AJunctionActor* Junction : Scene->Junctions)
		{
			if (IsValid(Junction))
				DiagnosticJunctions.Add(Junction);
		}
	}

	int32 CandidateCount = 0;
	int32 ReadyCount = 0;
	int32 FallbackCount = 0;
	for (const AJunctionActor* Junction : DiagnosticJunctions)
	{
		CandidateCount += Junction->GoreDiagnostics.Num();
		for (const FGoreDiagnostic& Diagnostic : Junction->GoreDiagnostics)
		{
			ReadyCount += Diagnostic.IsReady() ? 1 : 0;
			FallbackCount += Diagnostic.State == EGoreDiagnosticState::ReadyNoseGapFallback ? 1 : 0;
		}
	}

	const FString Summary = FString::Printf(
		TEXT("GORE DIAGNOSTICS - last Apply/Rebuild | candidates %d | ready %d | gap-ready %d | blocked %d"),
		CandidateCount,
		ReadyCount,
		FallbackCount,
		CandidateCount - ReadyCount);
	FCanvasTextItem SummaryItem(
		FVector2D(12.0f, 12.0f),
		FText::FromString(Summary),
		GEngine->GetSmallFont(),
		FLinearColor::White);
	SummaryItem.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(SummaryItem);

	FCanvasTextItem LegendItem(
		FVector2D(12.0f, 30.0f),
		FText::FromString(TEXT("green = ready (exact crossing or narrowing split) | red = blocked | cyan/magenta = tested boundaries")),
		GEngine->GetSmallFont(),
		FLinearColor(0.75f, 0.75f, 0.75f));
	LegendItem.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(LegendItem);

	for (const AJunctionActor* Junction : DiagnosticJunctions)
	{
		for (const FGoreDiagnostic& Diagnostic : Junction->GoreDiagnostics)
		{
			FVector2D PixelLocation;
			const FVector WorldLocation = Diagnostic.LabelLocation + FVector(0, 0, 100);
			if (!View->ScreenToPixel(View->WorldToScreen(WorldLocation), PixelLocation))
				continue;

			const FLinearColor StatusColor = Diagnostic.IsReady() ? FLinearColor::Green : FLinearColor::Red;
			const FString Label = FString::Printf(
				TEXT("GORE G%d-G%d | %s"),
				Diagnostic.GateIndex,
				Diagnostic.NextGateIndex,
				*Diagnostic.Status);
			FCanvasTextItem TextItem(
				PixelLocation + FVector2D(8.0f, -8.0f),
				FText::FromString(Label),
				GEngine->GetSmallFont(),
				StatusColor);
			TextItem.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(TextItem);
		}
	}
}
#undef LOCTEXT_NAMESPACE
