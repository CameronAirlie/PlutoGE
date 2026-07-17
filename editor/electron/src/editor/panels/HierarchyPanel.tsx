import React from 'react';
import { PanelFrame } from '../components/PanelFrame';
import { entityPresets } from '../constants';

function HierarchyNode({ entity, entities, selectedId, disabled }: {
  entity: EditorEntity;
  entities: EditorEntity[];
  selectedId: number;
  disabled: boolean;
}): React.JSX.Element {
  const children = entities.filter((candidate) => candidate.parentId === entity.id);
  return <div className="hierarchy-node">
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
  </div>;
}

export function HierarchyPanel({ editor, selectedEntity }: { editor?: EditorState; selectedEntity?: EditorEntity }): React.JSX.Element {
  const running = editor?.running ?? false;
  const roots = editor?.entities.filter((entity) => entity.parentId === 0) ?? [];
  const create = <select disabled={running || !editor} value="" aria-label="Create entity" onChange={(event) => {
    if (event.currentTarget.value) window.plutoEditor.createEntity(event.currentTarget.value, 0);
  }}><option value="">＋ Create</option>{entityPresets.map((name) => <option key={name}>{name}</option>)}</select>;

  return <PanelFrame id="hierarchy" title="Hierarchy" actions={create} className="hierarchy-panel">
    <div className="scene-label">⌕ {editor?.projectName ? `${editor.projectName} · ` : ''}{editor?.scenePath ? editor.scenePath.split(/[\\/]/).pop() : 'Untitled Scene'}{editor?.dirty ? ' •' : ''}</div>
    <div className="hierarchy-root" onDragOver={(event) => event.preventDefault()} onDrop={(event) => {
      const id = Number(event.dataTransfer.getData('application/x-plutoge-entity'));
      if (id && !running) window.plutoEditor.reparentEntity(id, 0);
    }}>
      {roots.map((entity) => <HierarchyNode key={entity.id} entity={entity} entities={editor?.entities ?? []} selectedId={editor?.selectedEntityId ?? 0} disabled={running} />)}
      {!roots.length && <div className="empty-state">Create an entity to begin.</div>}
    </div>
    <button className="delete-entity" disabled={!selectedEntity || running} onClick={() => selectedEntity && window.plutoEditor.deleteEntity(selectedEntity.id)}>Delete selected</button>
  </PanelFrame>;
}
