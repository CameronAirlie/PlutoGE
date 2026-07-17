import React, { useEffect } from 'react';
import { createPortal } from 'react-dom';

export interface PopupMenuItem {
  label: string;
  shortcut?: string;
  disabled?: boolean;
  danger?: boolean;
  separatorBefore?: boolean;
  children?: PopupMenuItem[];
  action?: () => void;
}

export interface PopupMenuState {
  x: number;
  y: number;
  items: PopupMenuItem[];
}

function MenuItems({ items, close }: { items: PopupMenuItem[]; close(): void }): React.JSX.Element {
  return <>{items.map((item, index) => <React.Fragment key={`${item.label}-${index}`}>
    {item.separatorBefore && <div className="popup-menu-separator" role="separator" />}
    <div className={`popup-menu-entry ${item.children?.length ? 'has-children' : ''}`}>
      <button
        type="button"
        role="menuitem"
        className={item.danger ? 'danger' : ''}
        disabled={item.disabled}
        onClick={() => {
          if (item.disabled || item.children?.length) return;
          item.action?.();
          close();
        }}
      >
        <span>{item.label}</span>
        {item.shortcut && <kbd>{item.shortcut}</kbd>}
        {item.children?.length ? <i>›</i> : null}
      </button>
      {item.children?.length ? <div className="popup-submenu" role="menu"><MenuItems items={item.children} close={close} /></div> : null}
    </div>
  </React.Fragment>)}</>;
}

export function PopupMenu({ menu, onClose, className = '' }: {
  menu?: PopupMenuState;
  onClose(): void;
  className?: string;
}): React.JSX.Element | null {
  useEffect(() => {
    if (!menu) return undefined;
    const close = (): void => onClose();
    const keydown = (event: KeyboardEvent): void => { if (event.key === 'Escape') close(); };
    window.addEventListener('pointerdown', close);
    window.addEventListener('blur', close);
    window.addEventListener('resize', close);
    window.addEventListener('keydown', keydown);
    return () => {
      window.removeEventListener('pointerdown', close);
      window.removeEventListener('blur', close);
      window.removeEventListener('resize', close);
      window.removeEventListener('keydown', keydown);
    };
  }, [menu, onClose]);

  if (!menu) return null;
  const width = 224;
  const estimatedHeight = Math.min(420, menu.items.length * 30 + 12);
  const left = Math.max(4, Math.min(menu.x, window.innerWidth - width - 4));
  const top = Math.max(4, Math.min(menu.y, window.innerHeight - estimatedHeight - 4));
  return createPortal(<div
    className={`popup-menu ${menu.x > window.innerWidth - width * 2 ? 'open-left' : ''} ${className}`}
    role="menu"
    style={{ left, top }}
    onPointerDown={(event) => event.stopPropagation()}
    onContextMenu={(event) => event.preventDefault()}
  ><MenuItems items={menu.items} close={onClose} /></div>, document.body);
}
