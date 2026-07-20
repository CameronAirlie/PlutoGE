import React, { useLayoutEffect } from "react";
import { DropdownMenu } from "radix-ui";

let nextPopupMenuId = 1;
export interface PopupMenuItem { label: string; shortcut?: string; disabled?: boolean; danger?: boolean; separatorBefore?: boolean; children?: PopupMenuItem[]; action?: () => void; }
export interface PopupMenuState { x: number; y: number; items: PopupMenuItem[]; }

function Items({ items, close }: { items: PopupMenuItem[]; close(): void }): React.JSX.Element {
	return <>{items.map((item, index) => <React.Fragment key={`${item.label}-${index}`}>
		{item.separatorBefore && <DropdownMenu.Separator className="popup-menu-separator" />}
		{item.children?.length ? <DropdownMenu.Sub><DropdownMenu.SubTrigger className={`popup-menu-button ${item.danger ? "danger" : ""}`} disabled={item.disabled}><span>{item.label}</span><i>›</i></DropdownMenu.SubTrigger><DropdownMenu.Portal><DropdownMenu.SubContent className="popup-menu"><Items items={item.children} close={close} /></DropdownMenu.SubContent></DropdownMenu.Portal></DropdownMenu.Sub> : <DropdownMenu.Item className={`popup-menu-button ${item.danger ? "danger" : ""}`} disabled={item.disabled} onSelect={() => { const action = item.action; close(); window.setTimeout(() => action?.(), 0); }}><span>{item.label}</span>{item.shortcut && <kbd>{item.shortcut}</kbd>}</DropdownMenu.Item>}
	</React.Fragment>)}</>;
}

export function PopupMenu({ menu, onClose, className = "" }: { menu?: PopupMenuState; onClose(): void; className?: string; }): React.JSX.Element | null {
	const [token] = React.useState(() => `popup-menu-${nextPopupMenuId++}`);
	useLayoutEffect(() => { if (!menu) return undefined; window.plutoEditor.setViewportOccluded(token, true); return () => window.plutoEditor.setViewportOccluded(token, false); }, [menu, token]);
	if (!menu) return null;
	return <DropdownMenu.Root open modal={false} onOpenChange={(open) => { if (!open) onClose(); }}>
		<DropdownMenu.Trigger aria-hidden tabIndex={-1} style={{ position: "fixed", left: menu.x, top: menu.y, width: 1, height: 1, padding: 0, border: 0, opacity: 0 }} />
		<DropdownMenu.Portal><DropdownMenu.Content className={`popup-menu ${className}`} side="bottom" align="start" sideOffset={0} collisionPadding={4} onCloseAutoFocus={(event) => event.preventDefault()}><Items items={menu.items} close={onClose} /></DropdownMenu.Content></DropdownMenu.Portal>
	</DropdownMenu.Root>;
}
