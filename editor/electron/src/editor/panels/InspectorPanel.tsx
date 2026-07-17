import React, { useEffect, useState } from 'react';
import { PanelFrame } from '../components/PanelFrame';
import { componentTypes, displayComponentName } from '../constants';

function VectorEditor({ label, value, onCommit, disabled }: { label: string; value: Vec3; onCommit(value: Vec3): void; disabled: boolean }): React.JSX.Element {
  const update = (index: number, raw: string): void => {
    const next = [...value] as Vec3;
    const parsed = Number(raw);
    if (Number.isFinite(parsed)) next[index] = parsed;
    onCommit(next);
  };
  return <div className="vector-field"><span>{label}</span><div>
    {value.map((number, index) => <label key={`${label}-${index}`}><i>{'XYZ'[index]}</i><input disabled={disabled} type="number" step="0.1" defaultValue={number} onBlur={(event) => update(index, event.currentTarget.value)} /></label>)}
  </div></div>;
}

function PropertyEditor({ property, entityId, componentIndex, propertyIndex, disabled }: { property: EditorProperty; entityId: number; componentIndex: number; propertyIndex: number; disabled: boolean }): React.JSX.Element {
  const commit = (value: string): void => window.plutoEditor.setComponentProperty(entityId, componentIndex, propertyIndex, value);
  if (property.type === 4) return <label className="property-row"><span>{property.name}</span><input disabled={disabled} type="checkbox" checked={property.value === 'true' || property.value === '1'} onChange={(event) => commit(event.currentTarget.checked ? 'true' : 'false')} /></label>;
  if (property.type === 6 && property.enumOptions.length) return <label className="property-row"><span>{property.name}</span><select disabled={disabled} value={property.value} onChange={(event) => commit(event.currentTarget.value)}>{property.enumOptions.map((option) => <option key={option}>{option}</option>)}</select></label>;
  const numeric = property.type === 0 || property.type === 1 || property.type === 8;
  return <label className="property-row"><span>{property.name}</span><input disabled={disabled} type={numeric ? 'number' : 'text'} step="any" defaultValue={property.value} onBlur={(event) => { if (event.currentTarget.value !== property.value) commit(event.currentTarget.value); }} /></label>;
}

function PostProcessParameterEditor({ parameter, disabled, onCommit }: { parameter: EditorProperty; disabled: boolean; onCommit(value: string): void }): React.JSX.Element {
  if (parameter.type === 4) return <label className="property-row"><span>{parameter.name}</span><input disabled={disabled} type="checkbox" checked={parameter.value === 'true' || parameter.value === '1'} onChange={(event) => onCommit(event.currentTarget.checked ? 'true' : 'false')} /></label>;
  if (parameter.type === 6 && parameter.enumOptions.length) {
    const usesIndex = /^\d+$/.test(parameter.value);
    return <label className="property-row"><span>{parameter.name}</span><select disabled={disabled} value={parameter.value} onChange={(event) => onCommit(event.currentTarget.value)}>{parameter.enumOptions.map((option, index) => <option key={option} value={usesIndex ? String(index) : option}>{option}</option>)}</select></label>;
  }
  const numeric = parameter.type === 0 || parameter.type === 1 || parameter.type === 8;
  return <label className="property-row"><span>{parameter.name}</span><input disabled={disabled} type={numeric ? 'number' : 'text'} step="any" defaultValue={parameter.value} onBlur={(event) => { if (event.currentTarget.value !== parameter.value) onCommit(event.currentTarget.value); }} /></label>;
}

function PostProcessStackEditor({ effects, effectTypes, presetReference, disabled, onAdd, onRemove, onMove, onEnabled, onParameter, onPreset, onSavePreset }: {
  effects: PostProcessEffectState[]; effectTypes: string[]; presetReference: string; disabled: boolean;
  onAdd(type: string): void; onRemove(index: number): void; onMove(from: number, to: number): void;
  onEnabled(index: number, enabled: boolean): void; onParameter(effectIndex: number, parameterIndex: number, value: string): void;
  onPreset(reference: string): void; onSavePreset(): void;
}): React.JSX.Element {
  const [effectType, setEffectType] = useState(effectTypes[0] ?? '');
  const [preset, setPreset] = useState(presetReference);
  useEffect(() => setPreset(presetReference), [presetReference]);
  return <div className="post-process-stack">
    <div className="preset-row"><input disabled={disabled} value={preset} placeholder="project://presets/editor.pppreset" onChange={(event) => setPreset(event.currentTarget.value)} /><button disabled={disabled || preset === presetReference} onClick={() => onPreset(preset)}>Load</button><button disabled={disabled || !presetReference} onClick={onSavePreset}>Save</button><button disabled={disabled || (!preset && !presetReference)} onClick={() => { setPreset(''); onPreset(''); }}>Clear</button></div>
    <div className="effect-add-row"><select disabled={disabled || !effectTypes.length} value={effectType} onChange={(event) => setEffectType(event.currentTarget.value)}>{effectTypes.map((type) => <option key={type}>{type}</option>)}</select><button disabled={disabled || !effectType} onClick={() => onAdd(effectType)}>Add effect</button></div>
    <div className="effect-list">
      {effects.map((effect, effectIndex) => <details className="effect-card" open key={`${effect.typeName}-${effectIndex}`}><summary><input disabled={disabled} type="checkbox" checked={effect.enabled} onClick={(event) => event.stopPropagation()} onChange={(event) => onEnabled(effectIndex, event.currentTarget.checked)} /><span>{effect.displayName || effect.typeName}</span><div className="effect-actions"><button disabled={disabled || effectIndex === 0} title="Move up" onClick={(event) => { event.preventDefault(); onMove(effectIndex, effectIndex - 1); }}>↑</button><button disabled={disabled || effectIndex + 1 === effects.length} title="Move down" onClick={(event) => { event.preventDefault(); onMove(effectIndex, effectIndex + 1); }}>↓</button><button disabled={disabled} title="Remove effect" onClick={(event) => { event.preventDefault(); onRemove(effectIndex); }}>×</button></div></summary><div className="effect-parameters">{effect.parameters.length ? effect.parameters.map((parameter, parameterIndex) => <PostProcessParameterEditor key={`${parameter.name}-${parameterIndex}`} parameter={parameter} disabled={disabled} onCommit={(value) => onParameter(effectIndex, parameterIndex, value)} />) : <small>No settings.</small>}</div></details>)}
      {!effects.length && <small>No post-processing effects. Add one above or load a preset.</small>}
    </div>
  </div>;
}

function EntityInspector({ entity, running, effectTypes }: { entity?: EditorEntity; running: boolean; effectTypes: string[] }): React.JSX.Element {
  const [componentToAdd, setComponentToAdd] = useState('');
  if (!entity) return <div className="empty-state">Select an entity to inspect it.</div>;
  const updateTransform = (field: 'position' | 'rotation' | 'scale', value: Vec3): void => window.plutoEditor.setEntityTransform(entity.id, field === 'position' ? value : entity.position, field === 'rotation' ? value : entity.rotation, field === 'scale' ? value : entity.scale);
  return <div className="inspector-content">
    <div className="entity-heading"><input type="checkbox" disabled={running} checked={entity.active} onChange={(event) => window.plutoEditor.setEntityActive(entity.id, event.currentTarget.checked)} /><input className="entity-name" disabled={running} defaultValue={entity.name} onBlur={(event) => window.plutoEditor.setEntityName(entity.id, event.currentTarget.value)} /></div>
    <section><h4>Transform</h4><VectorEditor label="Position" value={entity.position} disabled={running} onCommit={(value) => updateTransform('position', value)} /><VectorEditor label="Rotation" value={entity.rotation} disabled={running} onCommit={(value) => updateTransform('rotation', value)} /><VectorEditor label="Scale" value={entity.scale} disabled={running} onCommit={(value) => updateTransform('scale', value)} /></section>
    <section className="components-section"><h4>Components</h4>
      {entity.components.map((component, componentIndex) => <details className="component-card" open key={`${component.type}-${componentIndex}`}><summary><input type="checkbox" disabled={running} checked={component.enabled} onClick={(event) => event.stopPropagation()} onChange={(event) => window.plutoEditor.setComponentEnabled(entity.id, componentIndex, event.currentTarget.checked)} /><span>{displayComponentName(component.type)}</span><button disabled={running} title="Remove component" onClick={(event) => { event.preventDefault(); window.plutoEditor.removeComponent(entity.id, componentIndex); }}>×</button></summary><div className="component-properties">
        {component.properties.length ? component.properties.map((property, propertyIndex) => component.type === 'CameraComponent' && (property.name === 'PostProcessEffectCount' || property.name === 'PostProcessPresetAsset' || property.name.startsWith('PostProcessEffects.')) ? null : <PropertyEditor key={`${property.name}-${propertyIndex}`} property={property} entityId={entity.id} componentIndex={componentIndex} propertyIndex={propertyIndex} disabled={running} />) : <small>No editable serialized properties.</small>}
        {component.type === 'CameraComponent' && <PostProcessStackEditor effects={component.postProcessEffects ?? []} effectTypes={effectTypes} presetReference={component.postProcessPresetReference ?? ''} disabled={running} onAdd={(type) => window.plutoEditor.addCameraPostProcessEffect(entity.id, componentIndex, type)} onRemove={(index) => window.plutoEditor.removeCameraPostProcessEffect(entity.id, componentIndex, index)} onMove={(from, to) => window.plutoEditor.moveCameraPostProcessEffect(entity.id, componentIndex, from, to)} onEnabled={(index, enabled) => window.plutoEditor.setCameraPostProcessEffectEnabled(entity.id, componentIndex, index, enabled)} onParameter={(effectIndex, parameterIndex, value) => window.plutoEditor.setCameraPostProcessParameter(entity.id, componentIndex, effectIndex, parameterIndex, value)} onPreset={(reference) => window.plutoEditor.setCameraPostProcessPreset(entity.id, componentIndex, reference)} onSavePreset={() => window.plutoEditor.saveCameraPostProcessPreset(entity.id, componentIndex)} />}
      </div></details>)}
      <div className="add-component"><select disabled={running} value={componentToAdd} onChange={(event) => setComponentToAdd(event.currentTarget.value)}><option value="">Add component…</option>{componentTypes.map((type) => <option key={type} value={type}>{displayComponentName(type)}</option>)}</select><button disabled={running || !componentToAdd} onClick={() => { window.plutoEditor.addComponent(entity.id, componentToAdd); setComponentToAdd(''); }}>Add</button></div>
    </section>
  </div>;
}

function EditorCameraInspector({ camera, running, hasSelection, effectTypes }: { camera: EditorCameraState; running: boolean; hasSelection: boolean; effectTypes: string[] }): React.JSX.Element {
  const commit = (changes: Partial<EditorCameraState>): void => window.plutoEditor.setEditorCamera({ ...camera, ...changes });
  const scalar = (label: string, field: keyof EditorCameraState, step: number, min?: number): React.JSX.Element => <label className="property-row"><span>{label}</span><input disabled={running} type="number" step={step} min={min} defaultValue={camera[field] as number} onBlur={(event) => { const value = Number(event.currentTarget.value); if (Number.isFinite(value) && value !== camera[field]) commit({ [field]: value }); }} /></label>;
  return <div className="inspector-content camera-inspector"><p className="camera-description">Controls the edit viewport only. Play mode uses the scene’s main Camera component.</p>
    <section><h4>Transform</h4><VectorEditor label="Position" value={camera.position} disabled={running} onCommit={(position) => commit({ position })} />{scalar('Yaw', 'yawDegrees', 1)}{scalar('Pitch', 'pitchDegrees', 1)}</section>
    <section><h4>Projection</h4>{scalar('Field of view', 'fovY', 1, 1)}{scalar('Near plane', 'nearPlane', 0.01, 0.001)}{scalar('Far plane', 'farPlane', 1, 0.002)}</section>
    <section><h4>Navigation</h4>{scalar('Move speed', 'moveSpeed', 0.5, 0.1)}{scalar('Speed multiplier', 'speedAdjustment', 0.1, 0.1)}<label className="property-row"><span>Show grid</span><input disabled={running} type="checkbox" checked={camera.gridVisible} onChange={(event) => commit({ gridVisible: event.currentTarget.checked })} /></label></section>
    <section><h4>Post-processing</h4><PostProcessStackEditor effects={camera.postProcessEffects} effectTypes={effectTypes} presetReference={camera.postProcessPresetReference} disabled={running} onAdd={(type) => window.plutoEditor.addEditorPostProcessEffect(type)} onRemove={(index) => window.plutoEditor.removeEditorPostProcessEffect(index)} onMove={(from, to) => window.plutoEditor.moveEditorPostProcessEffect(from, to)} onEnabled={(index, enabled) => window.plutoEditor.setEditorPostProcessEffectEnabled(index, enabled)} onParameter={(effectIndex, parameterIndex, value) => window.plutoEditor.setEditorPostProcessParameter(effectIndex, parameterIndex, value)} onPreset={(reference) => window.plutoEditor.setEditorPostProcessPreset(reference)} onSavePreset={() => window.plutoEditor.saveEditorPostProcessPreset()} /></section>
    <div className="camera-actions"><button disabled={running || !hasSelection} onClick={() => window.plutoEditor.frameSelected()}>Frame selected</button><button disabled={running} onClick={() => window.plutoEditor.resetEditorCamera()}>Reset camera</button></div>
  </div>;
}

export function InspectorPanel({ editor, selectedEntity, showEditorCamera }: { editor?: EditorState; selectedEntity?: EditorEntity; showEditorCamera: boolean }): React.JSX.Element {
  const title = showEditorCamera ? 'Editor Camera' : 'Inspector';
  return <PanelFrame id="inspector" title={title} className="inspector-panel">
    {showEditorCamera && editor ? <EditorCameraInspector key={JSON.stringify(editor.editorCamera)} camera={editor.editorCamera} running={editor.running} hasSelection={Boolean(selectedEntity)} effectTypes={editor.postProcessEffectTypes} /> : <EntityInspector key={selectedEntity ? JSON.stringify(selectedEntity) : 'none'} entity={selectedEntity} running={editor?.running ?? false} effectTypes={editor?.postProcessEffectTypes ?? []} />}
  </PanelFrame>;
}
