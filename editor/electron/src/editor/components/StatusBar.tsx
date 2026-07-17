import React from 'react';

export function StatusBar({ host, editor }: { host: HostState; editor?: EditorState }): React.JSX.Element {
  const running = editor?.running ?? false;
  return <footer className="statusbar">
    <span className={`status-dot ${host.status}`} />
    <span>Engine {host.status}</span>
    <span>{running ? 'Play mode · Scene camera' : editor?.dirty ? 'Unsaved changes' : 'Scene saved'}</span>
    {editor && <span title="visible / submitted · renderable / registered meshes">Draw {editor.viewportStats.visibleRenderCommands}/{editor.viewportStats.submittedRenderCommands} · Mesh {editor.viewportStats.renderableMeshComponents}/{editor.viewportStats.registeredMeshComponents}</span>}
    <span className="status-hint">Drag tabs to rearrange · Drag separators to resize · Hold RMB + WASD/QE to fly</span>
  </footer>;
}
