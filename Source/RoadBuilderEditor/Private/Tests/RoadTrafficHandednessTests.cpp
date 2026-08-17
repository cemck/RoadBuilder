// Copyright 2024. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RoadBuilderSettings.h"
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

#endif
