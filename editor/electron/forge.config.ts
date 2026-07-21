import type { ForgeConfig } from "@electron-forge/shared-types";
import { MakerZIP } from "@electron-forge/maker-zip";
import { WebpackPlugin } from "@electron-forge/plugin-webpack";
import fs from "node:fs";
import path from "node:path";
import { mainConfig } from "./webpack.main.config";
import { rendererConfig } from "./webpack.renderer.config";

const engineBundle = process.env.PLUTOGE_ENGINE_BUNDLE_DIR;
const electronZipDirectory = process.env.PLUTOGE_ELECTRON_ZIP_DIR;
const distributionBuild = process.env.PLUTOGE_DISTRIBUTION_BUILD === "1";

if (distributionBuild) {
	if (!engineBundle || !fs.existsSync(engineBundle)) {
		throw new Error(
			"PLUTOGE_ENGINE_BUNDLE_DIR must point to the staged native engine bundle for a distribution build.",
		);
	}
	if (path.basename(path.resolve(engineBundle)).toLowerCase() !== "engine") {
		throw new Error(
			"PLUTOGE_ENGINE_BUNDLE_DIR must use a directory named 'engine' so the packaged host can resolve it.",
		);
	}
	if (!electronZipDirectory || !fs.existsSync(electronZipDirectory)) {
		throw new Error(
			"PLUTOGE_ELECTRON_ZIP_DIR must point to the locally staged Electron runtime ZIP directory.",
		);
	}
}

const config: ForgeConfig = {
	packagerConfig: {
		asar: true,
		executableName: "PlutoGEEditor",
		appBundleId: "com.plutoge.editor",
		appCopyright: "Copyright (c) PlutoGE contributors",
		...(electronZipDirectory
			? { electronZipDir: electronZipDirectory }
			: {}),
		...(engineBundle ? { extraResource: [engineBundle] } : {}),
	},
	rebuildConfig: {},
	makers: [new MakerZIP({}, ["win32"])],
	plugins: [
		new WebpackPlugin({
			mainConfig,
			renderer: {
				config: rendererConfig,
				entryPoints: [
					{
						html: "./src/index.html",
						js: "./src/renderer.tsx",
						name: "main_window",
						preload: { js: "./src/preload.ts" },
					},
				],
			},
		}),
	],
};

export default config;
