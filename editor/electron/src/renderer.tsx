import React from "react";
import { createRoot } from "react-dom/client";
import { App } from "./editor/App";
import { FloatingPanelApp } from "./editor/FloatingPanelApp";
import { panelTitles, type PanelId } from "./editor/components/PanelFrame";
import "./styles.css";

const root = document.getElementById("root");
const requestedPanel = new URLSearchParams(window.location.search).get("panel");
const panel =
	requestedPanel &&
	Object.prototype.hasOwnProperty.call(panelTitles, requestedPanel)
		? (requestedPanel as PanelId)
		: undefined;
if (root)
	createRoot(root).render(panel ? <FloatingPanelApp panel={panel} /> : <App />);
