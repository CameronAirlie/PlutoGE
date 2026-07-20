export const componentTypes = [
	"MeshComponent",
	"TerrainComponent",
	"FoliageComponent",
	"ClothComponent",
	"ParticleSystemComponent",
	"SplineComponent",
	"OceanComponent",
	"AnimationComponent",
	"CameraComponent",
	"LightComponent",
	"RigidbodyComponent",
	"ColliderComponent",
	"NavAgentComponent",
	"NavigationMeshComponent",
	"IblCaptureComponent",
	"PhysicalSkyComponent",
	"VolumetricCloudComponent",
	"ScriptComponent",
	"SoundEmitterComponent",
	"SoundListenerComponent",
	"CanvasComponent",
	"RectTransformComponent",
	"UIImageComponent",
	"UITextComponent",
	"UIButtonComponent",
];

export const entityPresets = [
	"Empty Entity",
	"Cube",
	"Camera",
	"Directional Light",
	"Point Light",
	"Sky",
	"Ocean",
	"Terrain",
	"Particle System",
];

export const displayComponentName = (type: string): string =>
	type.replace(/Component$/, "").replace(/([a-z])([A-Z])/g, "$1 $2");
