# RoadBuilder Plugin — Step-by-Step Guide

## Table of Contents
1. [Create Your Materials](#1-create-your-materials)
2. [Create a Road Preset](#2-create-a-road-preset)
3. [Enter Road Mode and Draw Roads](#3-enter-road-mode-and-draw-roads)
4. [Build an Intersection](#4-build-an-intersection)
5. [Add Traffic Lights](#5-add-traffic-lights)
6. [Add Stop Signs and Yield Signs](#6-add-stop-signs-and-yield-signs)
7. [Add Turn Arrows](#7-add-turn-arrows)
8. [Set Up AI (ZoneGraph / MassTraffic)](#8-set-up-ai-zonegraph--masstraffic)
9. [Fine-Tuning: Lanes, Markings, Heights](#9-fine-tuning-lanes-markings-heights)
10. [Advanced: Manual URoadStyle Setup](#10-advanced-manual-uroadstyle-setup)
11. [Quick Reference](#11-quick-reference)

---

## 1. Create Your Materials

The plugin does NOT ship with materials. You need to create 5 basic materials before anything will render. Here's exactly what to make:

### Step 1: Create a folder
In Content Browser: right-click ? **New Folder** ? name it `RoadMaterials`

### Step 2: Create each material
Right-click inside `RoadMaterials` ? **Material** for each:

#### M_Road_Asphalt (road surface)
1. Double-click to open the Material Editor
2. Select the main node ? set **Shading Model** to `Default Lit`
3. Add a **Constant3Vector** node ? set to dark gray `(0.04, 0.04, 0.045)` ? connect to **Base Color**
4. Add a **Constant** node ? set to `0.9` ? connect to **Roughness**
5. Save

#### M_Road_Concrete (sidewalks)
1. **Constant3Vector** ? light gray `(0.35, 0.34, 0.33)` ? **Base Color**
2. **Constant** ? `0.85` ? **Roughness**

#### M_Road_Curb (curb edge)
1. **Constant3Vector** ? medium gray `(0.25, 0.24, 0.23)` ? **Base Color**
2. **Constant** ? `0.8` ? **Roughness**

#### M_Road_WhitePaint (white lane markings)
1. **Constant3Vector** ? `(1.0, 1.0, 1.0)` ? **Base Color**
2. **Constant** ? `0.3` ? **Roughness**
3. **Constant3Vector** ? `(0.05, 0.05, 0.05)` ? **Emissive Color** (subtle glow)

#### M_Road_YellowPaint (center line markings)
1. **Constant3Vector** ? `(0.8, 0.6, 0.0)` ? **Base Color**
2. **Constant** ? `0.3` ? **Roughness**
3. **Constant3Vector** ? `(0.05, 0.04, 0.0)` ? **Emissive Color**

> **Tip**: For more realism later, replace these with texture-based materials. But flat colors work perfectly for getting started.

---

## 2. Create a Road Preset

A **Road Preset** is the new simplified way to define a road. One asset controls everything — lane count, sidewalks, materials, markings.

### Step 1: Create the Preset asset
1. In Content Browser, right-click ? **RoadBuilder** ? **Road Preset**
2. Name it `MyStreet`
3. Double-click to open it

### Step 2: Configure the Preset
Fill in these fields:

| Field | Value | What it does |
|-------|-------|-------------|
| **Road Type** | `Street` | Determines defaults (Street has sidewalks, Highway doesn't) |
| **Lanes Per Side** | `2` | 2 lanes in each direction = 4-lane road |
| **Lane Width** | `350` | Standard lane width in cm |
| **Has Sidewalks** | `?` checked | Generates raised sidewalks with curbs |
| **Sidewalk Width** | `200` | Sidewalk width in cm |
| **Road Material** | `M_Road_Asphalt` | Drag your asphalt material here |
| **Sidewalk Material** | `M_Road_Concrete` | Drag your concrete material here |
| **Curb Material** | `M_Road_Curb` | Drag your curb material here |
| **Center Line Color** | `Yellow` | Center dividing line color |
| **Lane Line Color** | `White` | Lane boundary line color |
| **White Line Material** | `M_Road_WhitePaint` | Material for white lane lines |
| **Yellow Line Material** | `M_Road_YellowPaint` | Material for yellow center line |
| **Traffic Control** | `Traffic Light` | What to auto-place at intersections (or `None`) |
| **Auto Turn Arrows** | `?` checked | Auto-place turn direction arrows at intersections |
| **Has Street Lights** | `?` unchecked | Leave off for now (needs a mesh) |
| **Has Ground** | `?` unchecked | Leave off for now |

### Step 3: Save the Preset
Press `Ctrl+S` to save.

**That's it.** One asset, all your road settings. No need to create separate LaneShape, LaneMarkStyle, or RoadProps assets.

### What the Preset creates internally
When you place a road with this preset, it auto-generates:
```
Center: Yellow double-solid line (YellowSolidSolid)
Physical right side (traffic direction comes from the RoadScene setting):
  Lane 1: 350cm driving lane ? white dashed boundary
  Lane 2: 350cm driving lane ? white solid boundary
  Sidewalk: 200cm raised sidewalk with curb
Physical left side (traffic direction comes from the RoadScene setting):
  Lane 1: 350cm driving lane ? white dashed boundary
  Lane 2: 350cm driving lane ? white solid boundary
  Sidewalk: 200cm raised sidewalk with curb
```

---

## 3. Enter Road Mode and Draw Roads

### Step 1: Enter Road Mode
1. Top-left toolbar ? click the **Modes** dropdown (selection tool icon)
2. Select **Road**
3. A `RoadScene` actor auto-spawns in your level (it's the parent for everything)

### Step 2: Assign your Preset
1. Click the **Road** tab (second tab in the Road mode panel)
2. In the properties panel below, find **Preset** (under "Road (Simple)")
3. Set it to `MyStreet` (the preset you just created)
4. Set **Base Height** to `0`

### Step 3: Draw the first road
1. Click the **Plan** sub-tool (first button in the Road tab toolbar)
2. **Right-click** in the viewport ? places the first control point
3. **Right-click** at another location ? places the second point, road appears immediately
4. **Right-click** more points ? road extends through them as a smooth spline
5. You should see: asphalt surface, lane markings, sidewalks with curbs

### Step 4: Edit road points
- **Left-click** a red control point ? select it (shows properties in panel)
- **Drag** the gizmo arrows ? move the point
- **Delete** key ? remove the selected point
- **Escape** ? deselect

### Connecting roads from separate RoadScenes
1. Load both World Partition cells so both RoadScenes and their RoadActors are visible.
2. In **Road Mode > Settings**, enable **Allow Cross-RoadScene Connections**.
3. Return to **Road > Plan** and select the first or last control point of the child road.
4. Drag that endpoint onto the paved surface of the target road. The target may belong to any loaded RoadScene.
5. Keep the road directions within 45 degrees. Moving within 8 m of a compatible target endpoint enables endpoint snapping.
6. Release the gizmo, then save both affected external actors.

Cross-RoadScene connections are stored as soft seam references. This keeps World Partition sectors independent instead of creating a hard-reference chain across the full route. When both sectors are loaded, rebuilding the child sector refreshes the seam from its target. Cross-scene seams align road geometry but intentionally do not create a junction actor across sector boundaries; keep complete forks, ramps, and intersections inside one RoadScene.

### Common issues at this point
| Problem | Fix |
|---------|-----|
| Road is invisible / all black | Materials not assigned in Preset ? go back and set them |
| No lane markings | White/Yellow Line Materials not set in Preset |
| No sidewalks | `Has Sidewalks` unchecked in Preset |
| Road is flat gray checkerboard | Material paths are wrong ? re-drag the materials |

---

## 4. Build an Intersection

### Step 1: Draw a second road
1. **Left-click** on empty space ? deselects the current road
2. **Right-click** to start a new road that crosses the first one
3. Place at least 2 points so the new road crosses through the first

### Step 2: Automatic junction creation
When two roads overlap, a **Junction** is automatically created. You'll see:
- The intersection area gets filled with road surface
- Corner roads are generated for each possible turn
- Gore markings appear at merge/diverge points

### Step 3: Adjust the junction
1. Click the **Junction** tab ? **Link** tool
2. Click on one of the curved connecting roads inside the intersection
3. In the properties panel, adjust the **Radius** value:
   - Higher = gentler turn (e.g., `2000`)
   - Lower = tighter turn (e.g., `500`)

### Step 4: Restrict turns (optional)
To block specific turns (e.g., no left turn):
1. In the World Outliner, select the `JunctionActor`
2. In the Details panel, find **Turn Restrictions**
3. Click `+` to add a restriction
4. Set **FromGateIndex** = the approach direction (0, 1, 2, etc.)
5. Set **ToGateIndex** = the exit direction to block
6. Rebuild ? that turn lane disappears

> **How to find gate indices**: Gates are numbered by their angular position around the junction. Gate 0 is the first approach road sorted by angle.

---

## 5. Add Traffic Lights

Traffic lights are auto-generated when your **Preset** has `Traffic Control` set to `Traffic Light`. But you can also set them per-junction:

### Method 1: Via Preset (automatic for all junctions)
1. Open your `MyStreet` preset
2. Set **Traffic Control** ? `Traffic Light`
3. Roads created with this preset will auto-generate traffic lights at every junction

### Method 2: Per Junction (manual control)
1. In World Outliner, select the `JunctionActor`
2. In Details panel, set **Traffic Control Type** ? `Traffic Light`
3. Rebuild the road scene (the plugin rebuilds automatically when you edit roads)

### What gets generated
For each input gate (each approach direction), the plugin spawns an `ATrafficLightActor`:
- **PoleMesh** — the traffic light pole (assign a mesh in Settings ? `TrafficLightMesh`)
- **StopVolume** — a `BoxComponent` (400cm wide) that AI vehicles can detect
- **Phase offset** — opposing directions auto-alternate (green/red swap)

### Traffic Light Properties
Select a traffic light in the viewport or World Outliner to edit:

| Property | Default | What it does |
|----------|---------|-------------|
| **Current State** | `Red` | Current signal: `Red`, `Yellow`, or `Green` |
| **Green Duration** | `30s` | How long green lasts |
| **Yellow Duration** | `5s` | How long yellow lasts |
| **Red Duration** | `35s` | How long red lasts |
| **Phase Offset** | `0` | Time offset in seconds (opposing traffic is auto-offset) |
| **Cycle Enabled** | `?` | Whether the light cycles automatically at runtime |
| **Gate Index** | auto | Which junction approach this light controls |

### How AI detects the traffic light
The `StopVolume` (BoxComponent) sits in front of the stop line:
- AI vehicles check `IsLaneOpen()` ? returns `true` only when green
- AI vehicles check `IsRed()` / `IsGreen()` for state
- `GetStopLineCenter()` returns the position where vehicles should stop
- `GetStopVolumeBox()` returns the detection area

### Blueprint example: Vehicle checks traffic light
```
On Tick:
  ? Overlap check with StopVolume
  ? If overlapping:
    ? Cast to ATrafficLightActor
    ? Call IsLaneOpen()
    ? If false: stop the vehicle
    ? If true: proceed
```

### Assigning the traffic light mesh
1. Enter Road Mode ? **Settings** tab
2. Set **Traffic Light Mesh** to your traffic light static mesh
3. Rebuild ? the mesh appears at each junction approach

> **Don't have a mesh?** The traffic light still works without one — the StopVolume and state machine are fully functional. You can add a mesh later.

---

## 6. Add Stop Signs and Yield Signs

### Stop Signs
Same as traffic lights but with a different type:

1. Select a `JunctionActor` in World Outliner
2. Set **Traffic Control Type** ? `Stop Sign`
3. Each input gate gets an `ATrafficSignActor` with:
   - **SignMesh** — the stop sign mesh (set in Settings ? `StopSignMesh`)
   - **DetectionVolume** — BoxComponent for AI detection
   - `IsStopSign()` returns `true`

### Yield Signs (for T-intersections)
For 3-way intersections where one road has priority:

1. Select the `JunctionActor` for the T-intersection
2. Set **Traffic Control Type** ? `Yield Sign`
3. Each approach gets a yield sign
4. AI vehicles can check `IsYieldSign()` on the `ATrafficSignActor`

### Per-gate control
To have different signs on different approaches (e.g., yield on the side street, nothing on the main road), you'd currently need to:
1. Set the junction to `None`
2. Manually place `ATrafficSignActor` blueprints from the Place Actors panel
3. Set their `GateIndex` to match the approach

### Assigning meshes
In Road Mode ? **Settings** tab:
- **Stop Sign Mesh** ? your stop sign static mesh
- **Yield Sign Mesh** ? your yield sign static mesh

---

## 7. Add Turn Arrows

Turn arrows are road-surface markings that show drivers which direction each lane goes (left, through, right).

### Enable auto-generation
1. Enter Road Mode ? **Settings** tab
2. Check **Auto Generate Turn Arrows** ? `?`
3. Set **Turn Arrow Mesh** ? your arrow static mesh (a flat arrow decal/mesh)
4. Rebuild ? arrows appear on approach lanes at every junction

### How arrows are determined
The plugin calculates the angle between each input gate and each reachable output gate:
- **Angle > 30°** ? Left arrow
- **Angle < -30°** ? Right arrow
- **In between** ? Through (straight) arrow

### Arrow types available
Each `ATurnArrowActor` has an `ArrowType` property:

| Type | Visual | When used |
|------|--------|-----------|
| `Left` | ? | Turn is > 30° to the left |
| `Through` | ? | Roughly straight ahead |
| `Right` | ? | Turn is > 30° to the right |
| `LeftThrough` | ?? | Available for manual override |
| `ThroughRight` | ?? | Available for manual override |
| `LeftRight` | ?? | Available for manual override |
| `LeftThroughRight` | ??? | Available for manual override |
| `UTurn` | ? | Available for manual override |

### Manual arrow placement
To override auto-generated arrows:
1. Select an `ATurnArrowActor` in the World Outliner
2. Change **Arrow Type** to whatever you want
3. Move/rotate the actor to reposition

### Arrow placement position
Arrows are placed **5 meters before the stop line** (500cm along the road from the junction gate). They sit on the road surface and face the direction of travel.

> **Don't have an arrow mesh?** You can use the **Marking** tab ? **Point** tool to manually place any mesh on the road surface, or use the **Curve** tool to draw arrow shapes with bezier curves.

---

## 8. Set Up AI (ZoneGraph / MassTraffic)

The plugin can auto-generate ZoneGraph lane shapes so MassTraffic AI vehicles know where to drive.

### Enable ZoneGraph generation
1. Enter Road Mode ? **Settings** tab
2. Check **Build Mass Graph** ? `?`
3. Rebuild ? ZoneGraph shapes are generated for all driving lanes

### What gets generated
- **One ZoneShapeComponent per lane segment** between junctions
- **One ZoneShapeComponent per junction link** (each turn lane inside an intersection)
- Restricted turns (from Turn Restrictions) do NOT get zone shapes
- Only `Driving` lanes get zone shapes (sidewalks, shoulders, medians are skipped)

### How it works with traffic lights
The traffic light's `StopVolume` sits at the junction entrance. AI vehicles using MassTraffic will:
1. Follow the ZoneGraph lane shape
2. Detect the `StopVolume` overlap
3. Query `IsLaneOpen()` on the traffic light
4. Stop on red, proceed on green

### Settings for MassTraffic

| Setting | Description |
|---------|-------------|
| **Build Mass Graph** | Master toggle for ZoneGraph generation |
| **No Turn Sign Mesh** | Mesh placed at restricted turns |

### Verifying ZoneGraph
1. In the viewport, enable **Show ? Zone Graph** to visualize the generated lanes
2. Each lane should appear as a colored spline
3. Junction connections should show as curved paths through the intersection

---

## 9. Fine-Tuning: Lanes, Markings, Heights

Once your basic road network is working, use these tools to refine it:

### Change lane properties
1. **Lane** tab ? **Edit** tool
2. Click on any lane segment ? properties appear:
   - **LaneType**: `Driving`, `Sidewalk`, `Shoulder`, `Median`, `None`
   - **LaneShape**: cross-section geometry
3. Use **Carve** tool to split a lane into segments with different properties
4. Use **Width** tool to add width variation along a lane

### Change lane markings
1. **Marking** tab ? **Lane** tool
2. Click on boundary lines between lanes to change the marking style
3. Choose from: `Dash`, `Solid`, `DashDash`, `SolidSolid`, etc.

### Add custom markings
1. **Marking** tab ? **Curve** tool ? draw bezier curves on the road surface
2. **Marking** tab ? **Point** tool ? place mesh objects (manholes, road studs, etc.)

### Add elevation changes
1. **Road** tab ? **Height** tool
2. Right-click on a road to add height control points
3. Set **Height** (Z elevation) and **Range** (blending distance)
4. Creates bridges, overpasses, slopes

### Split roads
- **Road** tab ? **Chop** ? click to split a road into two at a point
- **Road** tab ? **Split** ? click a boundary to split a road laterally

---

## 10. Advanced: Manual URoadStyle Setup

If you need more control than the Preset system offers, you can still create roads the old way with separate asset types. In the Road tab, use the **Style** field (under "Road (Advanced)") instead of the Preset field.

### Asset hierarchy
```
URoadStyle          ? Full lane layout
  ??? FRoadLaneStyle (per lane)
       ??? ULaneShape      ? 3D cross-section geometry
       ??? ULaneMarkStyle  ? Line marking pattern
       ??? URoadProps      ? Repeated objects along boundary
```

### Create assets
Right-click in Content Browser ? **RoadBuilder** ? choose the asset type.

To find built-in assets:
1. Content Browser ? **Settings** (gear icon) ? check **Show Plugin Content**
2. Navigate to `RoadBuilder Content`

### Built-in assets

**Lane Shapes** (in `RoadBuilder Content/LaneShapes/`):
- `Driving` — flat road surface
- `Sidewalk` — raised with curb
- `Median` — center median
- `Driving-Elevated` — elevated with barriers

**Mark Styles** (in `RoadBuilder Content/MarkStyles/LaneMark/`):
- `WhiteDash`, `WhiteSolid`, `WhiteSolidSolid`, etc.
- `YellowDash`, `YellowSolid`, `YellowSolidSolid`, etc.

---

## 11. Quick Reference

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| **Right-click** | Add/place a point (context-dependent) |
| **Left-click** | Select road/point/lane/marking |
| **Delete** | Delete selected point, or entire road if no point selected |
| **Escape** | Deselect / go up one level |
| **Ctrl+Z** | Undo |
| **Ctrl+Y** | Redo |
| **Drag gizmo** | Move selected point |

### Tabs

| Tab | Sub-tools | Purpose |
|-----|-----------|---------|
| **File** | Export | Export to OpenDRIVE .xodr |
| **Road** | Plan, Height, Chop, Split | Place and edit roads |
| **Junction** | Link | Edit junction connections |
| **Lane** | Edit, Carve, Width | Edit individual lanes |
| **Marking** | Lane, Point, Curve | Add road markings |
| **Ground** | Edit | Edit ground patches |
| **Settings** | — | Global settings |

### Settings Tab Reference

| Setting | Description |
|---------|-------------|
| **DefaultDrivingShape** | Default cross-section for driving lanes |
| **DefaultSidewalkShape** | Default cross-section for sidewalks |
| **DefaultDashStyle** | Default dashed line marking |
| **DefaultSolidStyle** | Default solid line marking |
| **DefaultGroundMaterial** | Material for ground patches |
| **UVScale** | Texture tiling (default: 0.001) |
| **Project Traffic Handedness** | Default RHT/LHT rule inherited by RoadScenes without an override |
| **BuildJunctions** | Auto-build junctions |
| **BuildProps** | Generate road props |
| **BuildMassGraph** | Generate ZoneGraph for AI |
| **AutoGenerateTrafficControl** | Master toggle for traffic control |
| **AutoGenerateTurnArrows** | Master toggle for turn arrows |
| **TrafficLightMesh** | Mesh for traffic light poles |
| **StopSignMesh** | Mesh for stop signs |
| **YieldSignMesh** | Mesh for yield signs |
| **TurnArrowMesh** | Mesh for turn direction arrows |
| **NoTurnSignMesh** | Mesh for no-turn restriction signs |

### Traffic handedness

`RightLanes` and `LeftLanes` always mean the physical side of the road reference spline. They do not hard-code traffic direction. In RHT, physical right lanes follow increasing spline distance; in LHT, physical left lanes do.

The Settings tab shows both the project default and the active RoadScene override. The active RoadScene is the owner of the most recently selected road or junction. Changing either value does not rebuild anything automatically. Use **Apply Active RoadScene** or **Apply All Loaded RoadScenes**, then save the level and external actors. In a World Partition map, load every sector that should be rebuilt before using the all-loaded action.

### Actor Types

| Actor | Placed by | Purpose |
|-------|-----------|---------|
| `RoadScene` | Auto (entering Road mode) | Parent container for everything |
| `RoadActor` | Right-click in Plan tool | A single road |
| `JunctionActor` | Auto (roads crossing) | Intersection |
| `GroundActor` | Auto (bHasGround) | Ground patch between roads |
| `ATrafficLightActor` | Auto (TrafficControl) | Cycling signal with AI detection |
| `ATrafficSignActor` | Auto (TrafficControl) | Stop/Yield sign with detection |
| `ATurnArrowActor` | Auto (AutoTurnArrows) | Direction arrow on road |
