import type React from "react";
import { useEffect, useMemo, useState } from "react";
import { PanelFrame } from "../components/PanelFrame";

export function ConsolePanel(): React.JSX.Element {
	const [messages, setMessages] = useState<EditorConsoleMessage[]>([]);
	const [filter, setFilter] = useState("");
	const [levels, setLevels] = useState({
		info: true,
		warning: true,
		error: true,
	});

	useEffect(() => {
		void window.plutoEditor.getConsoleMessages().then(setMessages);
		return window.plutoEditor.onConsoleMessage((message) =>
			setMessages((current) => [...current.slice(-999), message]),
		);
	}, []);
	useEffect(
		() => window.plutoEditor.onConsoleCleared(() => setMessages([])),
		[],
	);

	const visible = useMemo(
		() =>
			messages.filter(
				(message) =>
					levels[message.severity] &&
					(!filter ||
						`${message.source} ${message.text}`
							.toLowerCase()
							.includes(filter.toLowerCase())),
			),
		[messages, filter, levels],
	);

	return (
		<PanelFrame
			id="console"
			title="Console"
			actions={
				<>
					<input
						value={filter}
						placeholder="Filter…"
						onChange={(event) => setFilter(event.currentTarget.value)}
					/>
					{(["info", "warning", "error"] as const).map((level) => (
						<label key={level} className="console-level">
							<input
								type="checkbox"
								checked={levels[level]}
								onChange={(event) =>
									setLevels({ ...levels, [level]: event.currentTarget.checked })
								}
							/>
							{level}
						</label>
					))}
					<button
						type="button"
						onClick={() => {
							setMessages([]);
							window.plutoEditor.clearConsole();
						}}
					>
						Clear
					</button>
				</>
			}
			className="console-panel"
		>
			<div className="console-messages">
				{visible.map((message) => (
					<div
						key={message.id}
						className={`console-message ${message.severity}`}
					>
						<time>{new Date(message.time).toLocaleTimeString()}</time>
						<strong>{message.source}</strong>
						<span>{message.text}</span>
					</div>
				))}
				{!visible.length && (
					<div className="empty-state">No matching messages.</div>
				)}
			</div>
		</PanelFrame>
	);
}
