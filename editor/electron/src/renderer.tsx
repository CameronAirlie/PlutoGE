import React, { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import './styles.css';

const componentTypes = [
  'MeshComponent', 'TerrainComponent', 'FoliageComponent', 'ClothComponent',
  'ParticleSystemComponent', 'SplineComponent', 'OceanComponent', 'AnimationComponent',
  'CameraComponent', 'LightComponent', 'RigidbodyComponent', 'ColliderComponent',
  'NavAgentComponent', 'NavigationMeshComponent', 'IblCaptureComponent',
  'PhysicalSkyComponent', 'VolumetricCloudComponent', 'ScriptComponent',
  'SoundEmitterComponent', 'SoundListenerComponent', 'CanvasComponent',
  'RectTransformComponent', 'UIImageComponent', 'UITextComponent', 'UIButtonComponent',
];

const displayComponentName = (type: string): string =>
  type.replace(/Component$/, '').replace(/([a-z])([A-Z])/g, '$1 $2');

function HierarchyNode({ entity, entities, selectedId, disabled }: {
  entity: EditorEntity;
  entities: EditorEntity[];
  selectedId: number;
  disabled: boolean;
}): React.JSX.Element {
  const children = entities.filter((candidate) => candidate.parentId === entity.id);
  return (
    <div className="hierarchy-node">
      <button
        className={`tree-row ${selectedId === entity.id ? 'selected' : ''} ${entity.active ? '' : 'inactive'}`}
        draggable={!disabled}
        onDragStart={(event) => event.dataTransfer.setData('application/x-plutoge-entity', String(entity.id))}
        onDragOver={(event) => event.preventDefault()}
        onDrop={(event) => {
          event.preventDefault();
          const childId = Number(event.dataTransfer.getData('application/x-plutoge-entity'));
          if (!disabled && childId && childId !== entity.id) window.plutoEditor.reparentEntity(childId, entity.id);
        }}
        onClick={() => window.plutoEditor.selectEntity(entity.id)}
      >
        <span>{children.length ? '▾' : '◇'}</span>{entity.name || `Entity ${entity.id}`}
      </button>
      {children.length > 0 && <div className="tree-children">
        {children.map((child) => <HierarchyNode key={child.id} entity={child} entities={entities} selectedId={selectedId} disabled={disabled} />)}
      </div>}
    </div>
  );
}

function VectorEditor({ label, value, onCommit, disabled }: {
  label: string;
  value: Vec3;
  onCommit(value: Vec3): void;
  disabled: boolean;
}): React.JSX.Element {
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

function PropertyEditor({ property, entityId, componentIndex, propertyIndex, disabled }: {
  property: EditorProperty;
  entityId: number;
  componentIndex: number;
  propertyIndex: number;
  disabled: boolean;
}): React.JSX.Element {
  const commit = (value: string): void => window.plutoEditor.setComponentProperty(entityId, componentIndex, propertyIndex, value);
  if (property.type === 4) {
    return <label className="property-row"><span>{property.name}</span><input disabled={disabled} type="checkbox" checked={property.value === 'true' || property.value === '1'} onChange={(event) => commit(event.currentTarget.checked ? 'true' : 'false')} /></label>;
  }
  if (property.type === 6 && property.enumOptions.length) {
    return <label className="property-row"><span>{property.name}</span><select disabled={disabled} value={property.value} onChange={(event) => commit(event.currentTarget.value)}>{property.enumOptions.map((option) => <option key={option}>{option}</option>)}</select></label>;
  }
  const numeric = property.type === 0 || property.type === 1 || property.type === 8;
  return <label className="property-row"><span>{property.name}</span><input disabled={disabled} type={numeric ? 'number' : 'text'} step="any" defaultValue={property.value} onBlur={(event) => {
    if (event.currentTarget.value !== property.value) commit(event.currentTarget.value);
  }} /></label>;
}

function Inspector({ entity, running }: { entity?: EditorEntity; running: boolean }): React.JSX.Element {
  const [componentToAdd, setComponentToAdd] = useState('');
  if (!entity) return <aside className="inspector panel"><h2>Inspector</h2><div className="empty-state">Select an entity to inspect it.</div></aside>;
  const updateTransform = (field: 'position' | 'rotation' | 'scale', value: Vec3): void => {
    window.plutoEditor.setEntityTransform(entity.id,
      field === 'position' ? value : entity.position,
      field === 'rotation' ? value : entity.rotation,
      field === 'scale' ? value : entity.scale);
  };
  return <aside className="inspector panel">
    <h2>Inspector</h2>
    <div className="entity-heading">
      <input type="checkbox" disabled={running} checked={entity.active} onChange={(event) => window.plutoEditor.setEntityActive(entity.id, event.currentTarget.checked)} />
      <input className="entity-name" disabled={running} defaultValue={entity.name} onBlur={(event) => window.plutoEditor.setEntityName(entity.id, event.currentTarget.value)} />
    </div>
    <section>
      <h4>Transform</h4>
      <VectorEditor label="Position" value={entity.position} disabled={running} onCommit={(value) => updateTransform('position', value)} />
      <VectorEditor label="Rotation" value={entity.rotation} disabled={running} onCommit={(value) => updateTransform('rotation', value)} />
      <VectorEditor label="Scale" value={entity.scale} disabled={running} onCommit={(value) => updateTransform('scale', value)} />
    </section>
    <section className="components-section"><h4>Components</h4>
      {entity.components.map((component, componentIndex) => <details className="component-card" open key={`${component.type}-${componentIndex}`}>
        <summary>
          <input type="checkbox" disabled={running} checked={component.enabled} onClick={(event) => event.stopPropagation()} onChange={(event) => window.plutoEditor.setComponentEnabled(entity.id, componentIndex, event.currentTarget.checked)} />
          <span>{displayComponentName(component.type)}</span>
          <button disabled={running} title="Remove component" onClick={(event) => { event.preventDefault(); window.plutoEditor.removeComponent(entity.id, componentIndex); }}>×</button>
        </summary>
        <div className="component-properties">
          {component.properties.length ? component.properties.map((property, propertyIndex) => <PropertyEditor key={`${property.name}-${propertyIndex}`} property={property} entityId={entity.id} componentIndex={componentIndex} propertyIndex={propertyIndex} disabled={running} />) : <small>No editable serialized properties.</small>}
        </div>
      </details>)}
      <div className="add-component"><select disabled={running} value={componentToAdd} onChange={(event) => setComponentToAdd(event.currentTarget.value)}><option value="">Add component…</option>{componentTypes.map((type) => <option key={type} value={type}>{displayComponentName(type)}</option>)}</select><button disabled={running || !componentToAdd} onClick={() => { window.plutoEditor.addComponent(entity.id, componentToAdd); setComponentToAdd(''); }}>Add</button></div>
    </section>
  </aside>;
}

function EditorCameraInspector({ camera, running, hasSelection }: {
  camera: EditorCameraState;
  running: boolean;
  hasSelection: boolean;
}): React.JSX.Element {
  const commit = (changes: Partial<EditorCameraState>): void => window.plutoEditor.setEditorCamera({ ...camera, ...changes });
  const scalar = (label: string, field: keyof EditorCameraState, step: number, min?: number): React.JSX.Element =>
    <label className="property-row"><span>{label}</span><input
      disabled={running}
      type="number"
      step={step}
      min={min}
      defaultValue={camera[field] as number}
      onBlur={(event) => {
        const value = Number(event.currentTarget.value);
        if (Number.isFinite(value) && value !== camera[field]) commit({ [field]: value });
      }}
    /></label>;

  return <aside className="inspector panel camera-inspector">
    <h2>Editor Camera</h2>
    <p className="camera-description">Controls the edit viewport only. Play mode uses the scene’s main Camera component.</p>
    <section>
      <h4>Transform</h4>
      <VectorEditor label="Position" value={camera.position} disabled={running} onCommit={(position) => commit({ position })} />
      {scalar('Yaw', 'yawDegrees', 1)}
      {scalar('Pitch', 'pitchDegrees', 1)}
    </section>
    <section>
      <h4>Projection</h4>
      {scalar('Field of view', 'fovY', 1, 1)}
      {scalar('Near plane', 'nearPlane', 0.01, 0.001)}
      {scalar('Far plane', 'farPlane', 1, 0.002)}
    </section>
    <section>
      <h4>Navigation</h4>
      {scalar('Move speed', 'moveSpeed', 0.5, 0.1)}
      {scalar('Speed multiplier', 'speedAdjustment', 0.1, 0.1)}
      <label className="property-row"><span>Show grid</span><input disabled={running} type="checkbox" checked={camera.gridVisible} onChange={(event) => commit({ gridVisible: event.currentTarget.checked })} /></label>
    </section>
    <section>
      <h4>Post-processing</h4>
      <div className="property-row"><span>Loaded effects</span><small>{camera.postProcessEffectCount}</small></div>
      <div className="property-row"><span>Source</span><small title={camera.postProcessPresetReference}>{camera.postProcessPresetReference || 'Project settings'}</small></div>
    </section>
    <div className="camera-actions">
      <button disabled={running || !hasSelection} onClick={() => window.plutoEditor.frameSelected()}>Frame selected</button>
      <button disabled={running} onClick={() => window.plutoEditor.resetEditorCamera()}>Reset camera</button>
    </div>
  </aside>;
}

function App(): React.JSX.Element {
  const viewport = useRef<HTMLDivElement>(null);
  const [host, setHost] = useState<HostState>({ status: 'starting' });
  const [editor, setEditor] = useState<EditorState>();
  const [showEditorCamera, setShowEditorCamera] = useState(false);
  const selectedEntity = useMemo(() => editor?.entities.find((entity) => entity.id === editor.selectedEntityId), [editor]);

  useEffect(() => {
    const removeHostListener = window.plutoEditor.onHostState(setHost);
    const removeEditorListener = window.plutoEditor.onEditorState(setEditor);
    void window.plutoEditor.getHostState().then(setHost);
    void window.plutoEditor.getEditorState().then((state) => { if (state) setEditor(state); });
    return () => { removeHostListener(); removeEditorListener(); };
  }, []);

  useEffect(() => {
    const shortcuts = (event: KeyboardEvent): void => {
      if (!(event.ctrlKey || event.metaKey) || !editor) {
        if (event.key === 'Delete' && editor?.selectedEntityId && !editor.running) window.plutoEditor.deleteEntity(editor.selectedEntityId);
        return;
      }
      if (event.key.toLowerCase() === 's') { event.preventDefault(); void window.plutoEditor.saveScene(event.shiftKey); }
      if (event.key.toLowerCase() === 'z') { event.preventDefault(); event.shiftKey ? window.plutoEditor.redo() : window.plutoEditor.undo(); }
      if (event.key.toLowerCase() === 'y') { event.preventDefault(); window.plutoEditor.redo(); }
    };
    window.addEventListener('keydown', shortcuts);
    return () => window.removeEventListener('keydown', shortcuts);
  }, [editor]);

  useLayoutEffect(() => {
    const element = viewport.current;
    if (!element) return;
    let animationFrame = 0;
    const updateBounds = (): void => {
      cancelAnimationFrame(animationFrame);
      animationFrame = requestAnimationFrame(() => {
        const bounds = element.getBoundingClientRect();
        const scale = window.devicePixelRatio;
        window.plutoEditor.setViewportBounds({ x: bounds.left * scale, y: bounds.top * scale, width: bounds.width * scale, height: bounds.height * scale });
      });
    };
    const observer = new ResizeObserver(updateBounds);
    observer.observe(element);
    window.addEventListener('resize', updateBounds);
    updateBounds();
    return () => { cancelAnimationFrame(animationFrame); observer.disconnect(); window.removeEventListener('resize', updateBounds); window.plutoEditor.setViewportVisible(false); };
  }, []);

  useEffect(() => {
    const updateVisibility = (): void => window.plutoEditor.setViewportVisible(!document.hidden);
    document.addEventListener('visibilitychange', updateVisibility);
    updateVisibility();
    return () => document.removeEventListener('visibilitychange', updateVisibility);
  }, []);

  const roots = editor?.entities.filter((entity) => entity.parentId === 0) ?? [];
  const running = editor?.running ?? false;
  return <div className="editor-shell">
    <header className="toolbar">
      <div className="brand"><span className="brand-mark">P</span> PlutoGE</div>
      <div className="menu-actions"><button onClick={() => void window.plutoEditor.openProject()}>Project…</button><button disabled={!editor?.projectPath} onClick={() => window.plutoEditor.saveProject()}>Save Project</button><button onClick={() => void window.plutoEditor.newScene()}>New</button><button onClick={() => void window.plutoEditor.openScene()}>Open</button><button onClick={() => void window.plutoEditor.saveScene()}>Save</button></div>
      <div className="toolbar-group"><button className="tool-button" disabled={!editor?.canUndo || running} onClick={() => window.plutoEditor.undo()} title="Undo">↶</button><button className="tool-button" disabled={!editor?.canRedo || running} onClick={() => window.plutoEditor.redo()} title="Redo">↷</button></div>
      <button className={`camera-button ${showEditorCamera ? 'active' : ''}`} disabled={!editor} onClick={() => setShowEditorCamera((visible) => !visible)}>Camera</button>
      <button className={`play-button ${running ? 'running' : ''}`} disabled={!editor} onClick={() => window.plutoEditor.setRuntime(!running)}>{running ? '■ Stop' : '▶ Play'}</button>
    </header>
    <aside className="scene-panel panel">
      <div className="panel-heading"><h2>Scene</h2><select disabled={running} value="" onChange={(event) => { if (event.currentTarget.value) window.plutoEditor.createEntity(event.currentTarget.value, 0); }}><option value="">＋ Create</option>{['Empty Entity', 'Cube', 'Camera', 'Directional Light', 'Point Light', 'Sky', 'Ocean', 'Terrain', 'Particle System'].map((name) => <option key={name}>{name}</option>)}</select></div>
      <div className="search">⌕ {editor?.projectName ? `${editor.projectName} · ` : ''}{editor?.scenePath ? editor.scenePath.split(/[\\/]/).pop() : 'Untitled Scene'}{editor?.dirty ? ' •' : ''}</div>
      <div className="hierarchy-root" onDragOver={(event) => event.preventDefault()} onDrop={(event) => { const id = Number(event.dataTransfer.getData('application/x-plutoge-entity')); if (id && !running) window.plutoEditor.reparentEntity(id, 0); }}>
        {roots.map((entity) => <HierarchyNode key={entity.id} entity={entity} entities={editor?.entities ?? []} selectedId={editor?.selectedEntityId ?? 0} disabled={running} />)}
        {!roots.length && <div className="empty-state">Create an entity to begin.</div>}
      </div>
      <button className="delete-entity" disabled={!selectedEntity || running} onClick={() => selectedEntity && window.plutoEditor.deleteEntity(selectedEntity.id)}>Delete selected</button>
    </aside>
    <main className="viewport-frame"><div ref={viewport} className="viewport" aria-label="Engine viewport">{host.status !== 'ready' && <div className="viewport-message"><strong>{host.status === 'starting' ? 'Starting engine…' : 'Viewport unavailable'}</strong>{host.message && <span>{host.message}</span>}{host.status === 'error' && <button onClick={() => void window.plutoEditor.restartHost()}>Restart host</button>}</div>}</div></main>
    {showEditorCamera && editor
      ? <EditorCameraInspector key={JSON.stringify(editor.editorCamera)} camera={editor.editorCamera} running={running} hasSelection={Boolean(selectedEntity)} />
      : <Inspector key={selectedEntity ? JSON.stringify(selectedEntity) : 'none'} entity={selectedEntity} running={running} />}
    <footer className="statusbar"><span className={`status-dot ${host.status}`} /> Engine {host.status}<span>{running ? 'Play mode · Scene camera' : editor?.dirty ? 'Unsaved changes' : 'Scene saved'}</span>{editor && <span title="visible / submitted · renderable / registered meshes">Draw {editor.viewportStats.visibleRenderCommands}/{editor.viewportStats.submittedRenderCommands} · Mesh {editor.viewportStats.renderableMeshComponents}/{editor.viewportStats.registeredMeshComponents}</span>}<span className="status-hint">Hold RMB + WASD/QE to fly · Shift boosts · Wheel changes speed</span></footer>
  </div>;
}

const root = document.getElementById('root');
if (root) createRoot(root).render(<App />);
