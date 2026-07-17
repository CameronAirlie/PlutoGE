import React from 'react';

export type PanelId = 'hierarchy' | 'viewport' | 'inspector' | 'content';

export function PanelFrame({ id, title, actions, children, className = '' }: {
  id: PanelId;
  title: string;
  actions?: React.ReactNode;
  children: React.ReactNode;
  className?: string;
}): React.JSX.Element {
  return <section className={`dock-panel ${className}`} data-panel-id={id}>
    <header
      className="dock-panel-header"
      draggable
      title="Drag this tab onto another panel to move it"
      onDragStart={(event) => {
        event.dataTransfer.effectAllowed = 'move';
        event.dataTransfer.setData('application/x-plutoge-panel', id);
      }}
    >
      <span className="panel-grip">⠿</span>
      <h2>{title}</h2>
      {actions && <div className="panel-actions" onMouseDown={(event) => event.stopPropagation()}>{actions}</div>}
    </header>
    <div className="dock-panel-body">{children}</div>
  </section>;
}
