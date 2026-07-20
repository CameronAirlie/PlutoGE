import React, { useLayoutEffect, useRef, useState } from "react";
import { Popover } from "radix-ui";
import { Button, ScrollBox, TextField } from "./ui";

let nextPickerId = 1;
export interface SearchablePickerItem<T extends string = string> { value: T; label: string; category?: string; keywords?: string[]; disabled?: boolean; }

export function SearchablePicker<T extends string>({ items, onSelect, buttonLabel, searchPlaceholder = "Search…", emptyMessage = "No matching items.", disabled = false, className = "" }: { items: SearchablePickerItem<T>[]; onSelect(value: T): void; buttonLabel: string; searchPlaceholder?: string; emptyMessage?: string; disabled?: boolean; className?: string; }): React.JSX.Element {
	const searchRef = useRef<HTMLInputElement>(null);
	const [open, setOpen] = useState(false); const [query, setQuery] = useState("");
	const [token] = useState(() => `searchable-picker-${nextPickerId++}`);
	useLayoutEffect(() => { if (!open) return undefined; window.plutoEditor.setViewportOccluded(token, true); return () => window.plutoEditor.setViewportOccluded(token, false); }, [open, token]);
	const close = (): void => { setOpen(false); setQuery(""); };
	const normalized = query.trim().toLocaleLowerCase();
	const filtered = items.filter((item) => [item.label, item.category ?? "", ...(item.keywords ?? [])].join(" ").toLocaleLowerCase().includes(normalized));
	const categories = new Map<string, SearchablePickerItem<T>[]>();
	for (const item of filtered) { const category = item.category ?? "Other"; categories.set(category, [...(categories.get(category) ?? []), item]); }
	return <Popover.Root open={open} onOpenChange={(next) => { setOpen(next); if (!next) setQuery(""); }}>
		<Popover.Trigger asChild><Button className={`searchable-picker-trigger ${className}`} disabled={disabled}><span>{buttonLabel}</span><i aria-hidden>⌄</i></Button></Popover.Trigger>
		<Popover.Portal><Popover.Content className="searchable-picker-menu" sideOffset={4} align="start" collisionPadding={6} onOpenAutoFocus={(event) => { event.preventDefault(); searchRef.current?.focus(); }}>
			<TextField ref={searchRef} type="search" value={query} placeholder={searchPlaceholder} aria-label={searchPlaceholder} onChange={(event) => setQuery(event.currentTarget.value)} onKeyDown={(event) => { if (event.key !== "Enter") return; const first = filtered.find((item) => !item.disabled); if (first) { onSelect(first.value); close(); } }} />
			<ScrollBox className="searchable-picker-results">{[...categories].map(([category, categoryItems]) => <section key={category}><h5>{category}</h5>{categoryItems.map((item) => <Button variant="ghost" key={item.value} disabled={item.disabled} onClick={() => { onSelect(item.value); close(); }}>{item.label}</Button>)}</section>)}{!filtered.length && <small>{emptyMessage}</small>}</ScrollBox>
		</Popover.Content></Popover.Portal>
	</Popover.Root>;
}
