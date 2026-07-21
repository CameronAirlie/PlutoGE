import React from "react";
import { Dialog, ScrollArea } from "radix-ui";

export function Button({
	variant = "default",
	className = "",
	...props
}: React.ButtonHTMLAttributes<HTMLButtonElement> & {
	variant?: "default" | "primary" | "danger" | "ghost";
}): React.JSX.Element {
	return (
		<button
			type="button"
			className={`ui-button ${variant} ${className}`}
			{...props}
		/>
	);
}

export const TextField = React.forwardRef<
	HTMLInputElement,
	React.InputHTMLAttributes<HTMLInputElement>
>(function TextField(props, ref) {
	return (
		<input
			ref={ref}
			{...props}
			className={`ui-field ${props.className ?? ""}`}
		/>
	);
});

export function TextArea(
	props: React.TextareaHTMLAttributes<HTMLTextAreaElement>,
): React.JSX.Element {
	return (
		<textarea
			{...props}
			className={`ui-field ui-textarea ${props.className ?? ""}`}
		/>
	);
}

export function ScrollBox({
	className = "",
	children,
}: {
	className?: string;
	children: React.ReactNode;
}): React.JSX.Element {
	return (
		<ScrollArea.Root className={`ui-scroll ${className}`}>
			<ScrollArea.Viewport className="ui-scroll-viewport">
				{children}
			</ScrollArea.Viewport>
			<ScrollArea.Scrollbar className="ui-scrollbar" orientation="vertical">
				<ScrollArea.Thumb className="ui-scroll-thumb" />
			</ScrollArea.Scrollbar>
		</ScrollArea.Root>
	);
}

export function Modal({
	open = true,
	title,
	children,
	onOpenChange,
}: {
	open?: boolean;
	title: string;
	children: React.ReactNode;
	onOpenChange(open: boolean): void;
}): React.JSX.Element {
	return (
		<Dialog.Root open={open} onOpenChange={onOpenChange}>
			<Dialog.Portal>
				<Dialog.Overlay className="dialog-backdrop" />
				<Dialog.Content className="ui-dialog">
					<Dialog.Title>{title}</Dialog.Title>
					{children}
				</Dialog.Content>
			</Dialog.Portal>
		</Dialog.Root>
	);
}
