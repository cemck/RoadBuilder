// Copyright 2024. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "RoadActor.h"
#include "RoadBuilderSettings.h"
#include "RoadEdMode.h"
#include "RoadMarking.h"
#include "RoadScene.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderTrafficDirectionMappingTest,
	"RoadBuilder.Traffic.DirectionMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderTrafficDirectionMappingTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("RHT forward uses the physical right side"),
		ARoadScene::ResolveTrafficSide(ERoadTrafficHandedness::RightHandTraffic, 1.0),
		RD_RIGHT);
	TestEqual(
		TEXT("RHT reverse uses the physical left side"),
		ARoadScene::ResolveTrafficSide(ERoadTrafficHandedness::RightHandTraffic, -1.0),
		RD_LEFT);
	TestEqual(
		TEXT("LHT forward uses the physical left side"),
		ARoadScene::ResolveTrafficSide(ERoadTrafficHandedness::LeftHandTraffic, 1.0),
		RD_LEFT);
	TestEqual(
		TEXT("LHT reverse uses the physical right side"),
		ARoadScene::ResolveTrafficSide(ERoadTrafficHandedness::LeftHandTraffic, -1.0),
		RD_RIGHT);

	TestEqual(
		TEXT("RHT physical right travels with increasing spline distance"),
		ARoadScene::ResolveTrafficDirectionSign(ERoadTrafficHandedness::RightHandTraffic, RD_RIGHT),
		1.0);
	TestEqual(
		TEXT("RHT physical left travels against increasing spline distance"),
		ARoadScene::ResolveTrafficDirectionSign(ERoadTrafficHandedness::RightHandTraffic, RD_LEFT),
		-1.0);
	TestEqual(
		TEXT("LHT physical left travels with increasing spline distance"),
		ARoadScene::ResolveTrafficDirectionSign(ERoadTrafficHandedness::LeftHandTraffic, RD_LEFT),
		1.0);
	TestEqual(
		TEXT("LHT physical right travels against increasing spline distance"),
		ARoadScene::ResolveTrafficDirectionSign(ERoadTrafficHandedness::LeftHandTraffic, RD_RIGHT),
		-1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderTrafficSettingsTest,
	"RoadBuilder.Traffic.SettingsAndExport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderTrafficSettingsTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("A scene without an override inherits the project setting"),
		ARoadScene::ResolveTrafficHandedness(
			false,
			ERoadTrafficHandedness::RightHandTraffic,
			ERoadTrafficHandedness::LeftHandTraffic),
		ERoadTrafficHandedness::LeftHandTraffic);
	TestEqual(
		TEXT("A scene override wins over the project setting"),
		ARoadScene::ResolveTrafficHandedness(
			true,
			ERoadTrafficHandedness::RightHandTraffic,
			ERoadTrafficHandedness::LeftHandTraffic),
		ERoadTrafficHandedness::RightHandTraffic);
	TestEqual(
		TEXT("OpenDRIVE LHT rule"),
		ARoadScene::ToOpenDriveTrafficRule(ERoadTrafficHandedness::LeftHandTraffic),
		FString(TEXT("LHT")));
	TestEqual(
		TEXT("OpenDRIVE RHT rule"),
		ARoadScene::ToOpenDriveTrafficRule(ERoadTrafficHandedness::RightHandTraffic),
		FString(TEXT("RHT")));

	const USettings_Global* Settings = GetDefault<USettings_Global>();
	TestNotNull(TEXT("RoadBuilder global settings are available"), Settings);
	if (Settings)
	{
		TestEqual(
			TEXT("REV_CULT project default is Japanese left-hand traffic"),
			Settings->DefaultTrafficHandedness,
			ERoadTrafficHandedness::LeftHandTraffic);
		TestTrue(
			TEXT("REV_CULT enables gore diagnostics in Road Mode by default"),
			Settings->DisplayGoreDiagnostics != 0);
	}

	// A details-panel edit must only make the stored generation state stale.
	// Rebuild() is reached only by an explicit Apply/Rebuild action.
	ARoadScene* SceneDefaults = GetMutableDefault<ARoadScene>();
	TestNotNull(TEXT("RoadScene defaults are available"), SceneDefaults);
	if (SceneDefaults)
	{
		const bool bSavedOverride = SceneDefaults->bOverrideTrafficHandedness;
		const ERoadTrafficHandedness SavedSetting = SceneDefaults->TrafficHandedness;
		const bool bSavedInitialized = SceneDefaults->bTrafficHandednessInitialized;
		const ERoadTrafficHandedness SavedLastBuilt = SceneDefaults->LastBuiltTrafficHandedness;

		SceneDefaults->bOverrideTrafficHandedness = true;
		SceneDefaults->TrafficHandedness = ERoadTrafficHandedness::RightHandTraffic;
		SceneDefaults->bTrafficHandednessInitialized = true;
		SceneDefaults->LastBuiltTrafficHandedness = ERoadTrafficHandedness::RightHandTraffic;
		TestTrue(TEXT("Matching stored state reports applied"), SceneDefaults->IsTrafficHandednessApplied());

		SceneDefaults->TrafficHandedness = ERoadTrafficHandedness::LeftHandTraffic;
		TestFalse(TEXT("Changing the dropdown does not update the last-built state"), SceneDefaults->IsTrafficHandednessApplied());
		TestEqual(
			TEXT("Changing the dropdown does not regenerate or advance the last-built rule"),
			SceneDefaults->LastBuiltTrafficHandedness,
			ERoadTrafficHandedness::RightHandTraffic);

		SceneDefaults->bOverrideTrafficHandedness = bSavedOverride;
		SceneDefaults->TrafficHandedness = SavedSetting;
		SceneDefaults->bTrafficHandednessInitialized = bSavedInitialized;
		SceneDefaults->LastBuiltTrafficHandedness = SavedLastBuilt;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderUndoRedoCurveSafetyTest,
	"RoadBuilder.Editor.UndoRedoCurveSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderUndoRedoCurveSafetyTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("An editor world is available"), World);
	if (!World)
	{
		return false;
	}

	ARoadActor* Road = World->SpawnActor<ARoadActor>();
	TestNotNull(TEXT("A transient road can be created"), Road);
	if (!Road)
	{
		return false;
	}
	Road->SetFlags(RF_Transient);
	Road->Init(0.0);
	Road->RoadSegments.Reset();
	Road->AddRoadSegment(0.0, 1000.0, FVector2D::ZeroVector, 0.0, 0.0, 0.0);
	Road->HeightSegments.Reset();
	Road->AddHeightSegment(0.0, 0.0, 0.0);
	Road->AddHeightSegment(0.0, 100.0, 0.0);

	// Unreal can temporarily restore duplicated derived height keys while it
	// applies an undo transaction. This was the exact source of the NaN passed
	// into URoadCurve::FillPolyline in the reported editor crash.
	Road->UpdateCurveBySegments();
	TestEqual(TEXT("Duplicate restored height segments are coalesced"), Road->HeightSegments.Num(), 1);
	TestTrue(TEXT("Height lookup remains finite after coalescing"), FMath::IsFinite(Road->GetHeight(500.0)));
	TestTrue(
		TEXT("The base curve can rebuild without a NaN assertion"),
		Road->BaseCurve && Road->BaseCurve->Curve.Points.Num() >= 2);

	Road->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderUndoRedoDeferredRebuildTest,
	"RoadBuilder.Editor.UndoRedoDeferredRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderUndoRedoDeferredRebuildTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("An editor world is available"), World);
	if (!World)
	{
		return false;
	}

	ARoadScene* Scene = World->SpawnActor<ARoadScene>();
	Scene->SetFlags(RF_Transient);
	ARoadActor* Road = Scene->AddRoad(nullptr, 0.0);
	Road->SetFlags(RF_Transient);
	TestTrue(
		TEXT("A valid road can be prepared for deferred regeneration"),
		Road->RuntimeSetRoadPoints({ FVector(0.0, 0.0, 0.0), FVector(5000.0, 0.0, 0.0) }, false));
	Scene->bTrafficHandednessInitialized = false;

	{
		FEdModeRoad Mode;
		Mode.Scene = Scene;
		Mode.OnPostUndoRedo();
		TestTrue(TEXT("PostUndoRedo queues one rebuild"), Mode.bUndoRedoRebuildQueued);
		TestFalse(
			TEXT("PostUndoRedo does not regenerate synchronously inside the transaction notification"),
			Scene->bTrafficHandednessInitialized);

		// Cancel the registered ticker so the test can execute the same callback
		// deterministically while the stack-owned mode object remains alive.
		Mode.CancelUndoRedoRebuild();
		TestFalse(TEXT("A queued rebuild can be safely cancelled on mode exit"), Mode.bUndoRedoRebuildQueued);
		TestFalse(TEXT("The deferred rebuild callback completes in one tick"), Mode.RunUndoRedoRebuild(0.0f));
		TestTrue(TEXT("The deferred callback rebuilds the final restored scene"), Scene->bTrafficHandednessInitialized);
		Mode.Scene = nullptr;
	}

	TArray<AActor*> AttachedActors;
	Scene->GetAttachedActors(AttachedActors, true, true);
	for (int32 ActorIndex = AttachedActors.Num() - 1; ActorIndex >= 0; --ActorIndex)
	{
		if (IsValid(AttachedActors[ActorIndex]))
		{
			AttachedActors[ActorIndex]->Destroy();
		}
	}
	Scene->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderGeneratedWorldGoreWedgeTest,
	"RoadBuilder.Traffic.GeneratedWorldGoreWedge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderGeneratedWorldGoreWedgeTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("An editor world is available"), World);
	if (!World)
		return false;

	URoadStyle* MainStyle = LoadObject<URoadStyle>(
		nullptr,
		TEXT("/RoadBuilder/RoadStyles/Highway-Main-6-Lanes.Highway-Main-6-Lanes"));
	UPolygonMarkStyle* GoreStyle = GetMutableDefault<USettings_Global>()->DefaultGoreMarking.LoadSynchronous();
	TestNotNull(TEXT("The highway main style is available"), MainStyle);
	TestNotNull(TEXT("The default gore style is available"), GoreStyle);
	if (!MainStyle || !GoreStyle)
		return false;

	ARoadScene* Scene = World->SpawnActor<ARoadScene>();
	Scene->SetFlags(RF_Transient);
	ARoadActor* Road = Scene->AddRoad(MainStyle, 0.0);
	Road->RuntimeSetRoadPoints({ FVector(0.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0) }, false);
	UMarkingCurve* AuthoredCurve = Road->AddMarkingCurve(true);
	AuthoredCurve->FillStyle = GoreStyle;
	TArray<FVector2D> AuthoredPoints = {
		FVector2D(2000.0, -300.0),
		FVector2D(3500.0, -300.0),
		FVector2D(3500.0, 300.0)
	};
	AuthoredCurve->SetPoints(AuthoredPoints);
	UMarkingCurve* Gore = Road->AddMarkingCurve(true);
	Gore->FillStyle = GoreStyle;
	Gore->Orientation = -90.0;
	Gore->SetGeneratedWorldPoints(
		{ FVector(1000.0, 0.0, 0.0), FVector(7000.0, -600.0, 0.0), FVector(7000.0, 600.0, 0.0) });
	Road->BuildMesh(TArray<FJunctionSlot>());

	TestEqual(TEXT("An authored curve using the gore style retains its editable points"), AuthoredCurve->Points.Num(), 3);
	TestFalse(TEXT("An authored curve is not classified as junction-generated"), AuthoredCurve->bUseGeneratedWorldPoints);
	TestEqual(TEXT("Generated gore lookup never hijacks an authored curve with the same style"), Road->GetGeneratedMarkingCurve(GoreStyle), Gore);
	TestEqual(TEXT("The generated gore keeps one shared nose and a wide junction edge"), Gore->GeneratedWorldPoints.Num(), 3);
	TestTrue(TEXT("A world-space gore wedge emits chevron mesh triangles"), Gore->LastBuildTriangleCount > 0);

	TArray<AActor*> AttachedActors;
	Scene->GetAttachedActors(AttachedActors, true, true);
	for (int32 ActorIndex = AttachedActors.Num() - 1; ActorIndex >= 0; --ActorIndex)
	{
		if (IsValid(AttachedActors[ActorIndex]))
			AttachedActors[ActorIndex]->Destroy();
	}
	Scene->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderRampSnappingAndForkTopologyTest,
	"RoadBuilder.Traffic.RampSnappingAndForkTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderRampSnappingAndForkTopologyTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("An editor world is available"), World);
	if (!World)
	{
		return false;
	}

	URoadStyle* MainStyle = LoadObject<URoadStyle>(
		nullptr,
		TEXT("/RoadBuilder/RoadStyles/Highway-Main-6-Lanes.Highway-Main-6-Lanes"));
	URoadStyle* RampStyle = LoadObject<URoadStyle>(
		nullptr,
		TEXT("/RoadBuilder/RoadStyles/Highway-Ramp-2-Lanes.Highway-Ramp-2-Lanes"));
	TestNotNull(TEXT("The highway main style is available"), MainStyle);
	TestNotNull(TEXT("The highway ramp style is available"), RampStyle);
	if (!MainStyle || !RampStyle)
	{
		return false;
	}

	auto DestroyTransientScene = [](ARoadScene* Scene)
	{
		TArray<AActor*> AttachedActors;
		Scene->GetAttachedActors(AttachedActors, true, true);
		for (int ActorIndex = AttachedActors.Num() - 1; ActorIndex >= 0; --ActorIndex)
		{
			if (IsValid(AttachedActors[ActorIndex]))
			{
				AttachedActors[ActorIndex]->Destroy();
			}
		}
		Scene->Destroy();
	};

	for (const ERoadTrafficHandedness Handedness :
		{ ERoadTrafficHandedness::RightHandTraffic, ERoadTrafficHandedness::LeftHandTraffic })
	{
		const TCHAR* HandednessName = Handedness == ERoadTrafficHandedness::LeftHandTraffic ? TEXT("LHT") : TEXT("RHT");
		for (int RampConnectionPoint = 0; RampConnectionPoint < 2; ++RampConnectionPoint)
		{
			ARoadScene* Scene = World->SpawnActor<ARoadScene>();
			Scene->SetFlags(RF_Transient);
			Scene->bOverrideTrafficHandedness = true;
			Scene->TrafficHandedness = Handedness;
			ARoadActor* MainRoad = Scene->AddRoad(MainStyle, 0.0);
			ARoadActor* RampRoad = Scene->AddRoad(RampStyle, 0.0);
			MainRoad->RuntimeSetRoadPoints(
				{ FVector(0.0, 0.0, 0.0), FVector(20000.0, 0.0, 0.0) }, false);

			const double ConnectionDistance = 8000.0;
			URoadBoundary* ConnectionBoundary = MainRoad->GetRoadEdge(Scene->GetForwardTrafficSide());
			const double BoundaryOffset = ConnectionBoundary->GetOffset(ConnectionDistance);
			const double OutwardSign = BoundaryOffset < 0.0 ? -1.0 : 1.0;
			const FVector ConnectionPosition = ConnectionBoundary->GetPos(ConnectionDistance)
				- MainRoad->GetRight(ConnectionDistance) * OutwardSign * 50.0;
			const double ChildTrafficSign = Scene->GetTrafficDirectionSignForSide(RD_RIGHT);
			const double AlongRoad = RampConnectionPoint == 0
				? ChildTrafficSign * 7000.0
				: -ChildTrafficSign * 7000.0;
			const FVector RampOuterPosition = ConnectionPosition + FVector(AlongRoad, OutwardSign * 3500.0, 0.0);
			RampRoad->RuntimeSetRoadPoints(
				RampConnectionPoint == 0
					? TArray<FVector>({ ConnectionPosition, RampOuterPosition })
					: TArray<FVector>({ RampOuterPosition, ConnectionPosition }),
				false);
			int SnappedPointIndex = RampConnectionPoint;
			RampRoad->ConnectTo(SnappedPointIndex, MainRoad);
			TestTrue(
				FString::Printf(TEXT("A %s ramp connected at point %d passes endpoint snapping"), HandednessName, RampConnectionPoint),
				RampRoad->ConnectedParents[RampConnectionPoint] == MainRoad);
			DestroyTransientScene(Scene);
		}

		ARoadScene* Scene = World->SpawnActor<ARoadScene>();
		Scene->SetFlags(RF_Transient);
		Scene->bOverrideTrafficHandedness = true;
		Scene->TrafficHandedness = Handedness;
		ARoadActor* ParentRoad = Scene->AddRoad(MainStyle, 0.0);
		ARoadActor* ThroughRoad = Scene->AddRoad(MainStyle, 0.0);
		ARoadActor* RampRoad = Scene->AddRoad(RampStyle, 0.0);
		ParentRoad->RuntimeSetRoadPoints(
			{ FVector(0.0, 0.0, 0.0), FVector(4609.23, 34.651, 0.0), FVector(9218.46, 69.302, 0.0) }, false);
		const double ParentLength = ParentRoad->Length();
		const FVector MainConnection(ParentRoad->GetPos(ParentLength), ParentRoad->GetHeight(ParentLength));
		ThroughRoad->RuntimeSetRoadPoints(
			{ MainConnection, MainConnection + FVector(5368.683, 40.36, 0.0) }, false);
		ParentRoad->ConnectTo(ThroughRoad, FVector2D(0.0, 0.0), 1);
		ThroughRoad->ConnectTo(ParentRoad, FVector2D(ParentRoad->Length(), 0.0), 0);

		const int RampConnectionPoint = Handedness == ERoadTrafficHandedness::RightHandTraffic ? 1 : 0;
		URoadBoundary* ConnectionBoundary = ParentRoad->GetRoadEdge(Scene->GetForwardTrafficSide());
		const FVector RampConnection = ConnectionBoundary->GetPos(ParentRoad->Length());
		TArray<FVector> RampPoints;
		if (Handedness == ERoadTrafficHandedness::RightHandTraffic)
		{
			RampPoints =
			{
				RampConnection + FVector(16980.4, 127.654, 0.0),
				RampConnection + FVector(15373.195, 115.571, 0.0),
				RampConnection + FVector(13781.994, -96.396, 0.0),
				RampConnection + FVector(3201.413, -175.939, 0.0),
				RampConnection + FVector(1603.921, 12.057, 0.0),
				RampConnection
			};
		}
		else
		{
			RampPoints =
			{
				RampConnection,
				RampConnection + FVector(1608.941, 12.096, 0.0),
				RampConnection + FVector(3198.406, 224.05, 0.0),
				RampConnection + FVector(8488.696, 263.822, 0.0),
				RampConnection + FVector(13778.986, 303.593, 0.0),
				RampConnection + FVector(15377.795, 115.606, 0.0),
				RampConnection + FVector(16980.4, 127.654, 0.0)
			};
		}
		RampRoad->RuntimeSetRoadPoints(RampPoints, false);
		RampRoad->ConnectTo(
			ParentRoad,
			FVector2D(ParentRoad->Length(), ConnectionBoundary->GetOffset(ParentRoad->Length())),
			RampConnectionPoint);
		RampRoad->UpdateCurve();
		Scene->Rebuild();
		TestEqual(
			FString::Printf(TEXT("The Showcase-style %s fork produces one junction"), HandednessName),
			Scene->Junctions.Num(),
			1);
		int RampConnectionCount = 0;
		int GoreDiagnosticCount = 0;
		for (AJunctionActor* Junction : Scene->Junctions)
		{
			if (!IsValid(Junction))
				continue;
			TestEqual(
				FString::Printf(TEXT("The Showcase-style %s fork has three gates"), HandednessName),
				Junction->Gates.Num(),
				3);
			for (FJunctionGate& Gate : Junction->Gates)
			{
				if (Junction->GetRampConnection(Gate) != INDEX_NONE)
					++RampConnectionCount;
			}
			for (const FGoreDiagnostic& Diagnostic : Junction->GoreDiagnostics)
			{
				++GoreDiagnosticCount;
				AddInfo(FString::Printf(
					TEXT("%s gore G%d-G%d: %s"),
					HandednessName,
					Diagnostic.GateIndex,
					Diagnostic.NextGateIndex,
					*Diagnostic.Status));
				TestFalse(
					FString::Printf(TEXT("The %s gore candidate has a visible diagnostic message"), HandednessName),
					Diagnostic.Status.IsEmpty());
				TestTrue(
					FString::Printf(TEXT("The %s gore requirement count stays in range"), HandednessName),
					Diagnostic.RequirementsMet >= 0 && Diagnostic.RequirementsMet <= FGoreDiagnostic::RequirementCount);
				if (Diagnostic.IsReady())
				{
					TestEqual(
						FString::Printf(TEXT("A ready %s gore candidate meets every requirement"), HandednessName),
						Diagnostic.RequirementsMet,
						FGoreDiagnostic::RequirementCount);
					TestTrue(
						FString::Printf(TEXT("A ready %s gore candidate has a polygon"), HandednessName),
						Diagnostic.PolygonPointCount >= 3);
					TestTrue(
						FString::Printf(TEXT("A ready %s gore candidate emitted marking triangles"), HandednessName),
						Diagnostic.Marking.IsValid() && Diagnostic.Marking->LastBuildTriangleCount > 0);
				}
			}
		}
		TestEqual(
			FString::Printf(TEXT("The Showcase-style %s fork identifies one ramp connection"), HandednessName),
			RampConnectionCount,
			1);
		TestTrue(
			FString::Printf(TEXT("The Showcase-style %s fork exposes at least one gore diagnostic"), HandednessName),
			GoreDiagnosticCount > 0);

		// Junction-link marking edits are authored overrides. Applying/rebuilding
		// the scene must not replace them with the global default dash/solid styles.
		ULaneMarkStyle* JunctionMarkingOverride = NewObject<ULaneMarkStyle>(Scene);
		JunctionMarkingOverride->MarkType = ELaneMarkType::Solid;
		TArray<TWeakObjectPtr<ARoadActor>> EditedLinkRoads;
		for (AJunctionActor* Junction : Scene->Junctions)
		{
			if (!IsValid(Junction))
				continue;
			for (FJunctionGate& Gate : Junction->Gates)
			{
				for (FJunctionLink& Link : Gate.Links)
				{
					if (!IsValid(Link.Road))
						continue;
					EditedLinkRoads.AddUnique(Link.Road);
					for (URoadBoundary* Boundary : Link.Road->Boundaries)
					{
						if (!IsValid(Boundary))
							continue;
						for (FBoundarySegment& Segment : Boundary->Segments)
							Segment.LaneMarking = JunctionMarkingOverride;
					}
				}
			}
		}
		TestTrue(
			FString::Printf(TEXT("The %s fork exposes junction link roads for marking override verification"), HandednessName),
			!EditedLinkRoads.IsEmpty());
		Scene->Rebuild();
		int32 PreservedLinkCount = 0;
		for (const TWeakObjectPtr<ARoadActor>& EditedLinkRoad : EditedLinkRoads)
		{
			if (!EditedLinkRoad.IsValid())
				continue;
			++PreservedLinkCount;
			for (URoadBoundary* Boundary : EditedLinkRoad->Boundaries)
			{
				if (!IsValid(Boundary))
					continue;
				for (const FBoundarySegment& Segment : Boundary->Segments)
				{
					TestEqual(
						FString::Printf(TEXT("A %s junction marking override survives Apply/Rebuild"), HandednessName),
						Segment.LaneMarking,
						JunctionMarkingOverride);
				}
			}
		}
		TestTrue(
			FString::Printf(TEXT("The %s junction links survive a marking-only Apply/Rebuild"), HandednessName),
			PreservedLinkCount > 0);
		// This endpoint-snapping fixture can legitimately collapse a connector to
		// zero length.  It verifies selection/topology and diagnostics; actual
		// world-space wedge rendering is covered by GeneratedWorldGoreWedge.
		DestroyTransientScene(Scene);
	}
	return true;
}

#endif
