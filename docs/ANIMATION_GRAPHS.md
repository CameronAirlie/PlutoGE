# Animation graphs

`.plutoanimgraph` assets describe a hierarchical state machine plus a stack of
masked layers. This page covers the model, how it reaches the runtime, and
where to extend it.

## Model

| Concept | Purpose |
| --- | --- |
| **State** | Plays one clip or a 1D/2D blend space. Names are unique graph-wide; scripts play states by name. |
| **Group** | A sub-state machine. Holds states and other groups, and has an *entry state*. Groups only organise authoring. |
| **Transition** | From a state or group to a state or group, gated by conditions and/or exit time. |
| **Any State** | A transition from group `0` (the root). It applies to every state. |
| **Layer / bone mask** | Additive or override clips/graphs restricted to part of the skeleton. |

Graphs stay readable through these rules:

- **A transition from a group** fires from *any state inside it* (Unreal's state
  aliases). Put Jump, Fall or Hit on the `Grounded` group once instead of
  copying them onto every grounded state.
- **A transition to a group** enters its entry state.
- **Priority:** for each state, Any State transitions are checked first, then
  group transitions from the outermost group inward, then the state's own
  transitions. Within one source, file order is priority.
- A group transition skips its own destination unless `canTransitionToSelf`
  is set.

## Runtime

`animgraph::CompileTransitions` (`engine/assets`) flattens the hierarchy into
state-to-state transitions in priority order. `AnimationComponent` consumes
that flat list (`BuildRuntimeStates`), so groups cost nothing at runtime and
playback code does not know about them. Layer graphs use the same path.

## File format (version 5)

Line-based `Key=field|field|...`, read by `ParseAnimationGraph` and written
by `WriteAnimationGraph` (`AnimationGraphSerialization.h`). Version 5 adds:

```
Group=id|name|parentId|entryStateId|x|y
GroupNote=id|free text (\n and \\ escaped)
GroupLayout=groupId|anyStateX|anyStateY|parentX|parentY   (0 = root)
State=...existing fields...|groupId
StateNote=id|free text
Transition=...existing fields...|fromKind|toKind|canTransitionToSelf   (kind: S or G)
```

Older files load unchanged. `#` comment lines directly above a `State=` line
become that state's description, so notes written by hand survive the first
save from the editor.

## Editor

`editor/ui/src/animgraph/` splits the editor by responsibility:

| File | Responsibility |
| --- | --- |
| `AnimationGraphDocument` | Graph, undo/redo, navigation, selection and every structural command. Widget edits made through `MutableGraph()` merge into one undo step when the gesture ends. |
| `StateMachineView` | Pure view model for one group: visible nodes, proxies (Any State, parent), and transitions bundled into edges. Has no ImGui dependency and is unit tested. |
| `StateMachineCanvas` | Draws the view and turns input into document commands; keeps pan and zoom per group. |
| `AnimationGraphInspector` | Context editor for a transition or edge bundle, state, group, Any State, multi-selection, or graph overview. |
| `AnimationGraphSidebar` / `AnimationGraphLayers` | Outline (search, drag to regroup), parameters (with live values), layers and masks, issues. |
| `AnimationGraphLiveSession` | Attaches to the selected entity's `AnimationComponent` for live state, transition and parameter display. |

Validation (`AnimationGraphValidation.h`) reports errors, transitions that can
never fire, script-only states, and transitions duplicated across states.
`ConsolidateGroupTransitions` (the inspector's **Merge into group
transitions**) turns those duplicates into group transitions. It only merges a
transition that every covered state checks *first*, so evaluation order is
unchanged.

## Tool

`PlutoGEAnimationGraphTool`, built on demand from `tools/AnimationGraphTool.cpp`:

```
PlutoGEAnimationGraphTool report <graph>...        duplicated transitions and problems
PlutoGEAnimationGraphTool consolidate <in> <out>   merge shared transitions, outer groups first
PlutoGEAnimationGraphTool compare <a> <b>          exit 0 when both compile to identical behaviour
```

To migrate a flat graph: group its states (in the editor or by script), run
`consolidate`, then `compare` the result with the original.

### Extending

- **New motion type** (for example a blend tree): add data to
  `AnimationGraphState`, serialise it in `AnimationGraphSerialization.cpp`,
  build it in `BuildRuntimeStates`, and add a mode to `DrawMotion` in the
  inspector.
- **New condition or parameter type:** extend the enums, then
  `ConditionModesFor`, `DescribeCondition` and the runtime condition evaluation.
- **New node kind on the canvas:** add a `ViewNodeKind`, emit it from
  `BuildStateMachineView`, and give it a colour and connection rules in
  `StateMachineCanvas.cpp`.
- **New diagnostics:** add a check in `AnimationGraphValidation.cpp`. The
  canvas badges and the Issues tab pick it up automatically.
