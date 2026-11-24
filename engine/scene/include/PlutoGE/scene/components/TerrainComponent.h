#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RenderCommand.h"
#include "PlutoGE/scene/TerrainBrush.h"
#include "PlutoGE/scene/TerrainHeightData.h"
#include "PlutoGE/scene/TerrainHoleMask.h"
#include "PlutoGE/scene/TerrainLayerStack.h"
#include "PlutoGE/scene/HeavyPropertyCache.h"
#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::render
{
    class Material;
}

namespace PlutoGE::scene
{
    enum class TerrainPaintMode
    {
        Raise,
        Lower,
        Smooth,
        Flatten,
        Layer, // paints the selected material layer (see TerrainLayerStack)
        Hole,  // cuts cells away (see TerrainHoleMask)
        Fill,  // restores cut cells
    };

    struct TerrainComponentConfig
    {
        int width = 65;
        int depth = 65;
        float cellSize = 1.0f;
        float heightScale = 10.0f;
        int chunkSize = 32;
        int lodCount = 4;
        render::Material *material = nullptr;
    };

    // What a terrain's mesh build reads (TerrainComponent.cpp).
    struct TerrainMeshInputs;

    class TerrainComponent : public TypedComponent<TerrainComponent>
    {
    public:
        explicit TerrainComponent(const TerrainComponentConfig &config = {});
        ~TerrainComponent() override = default;

        void Update(float deltaTime) override;
        void SubmitRenderCommands();

        std::vector<Property> Serialize() const override;
        std::vector<Property> SerializeForInspector() const override;
        std::vector<Property> SerializeShared() const override;
        // Missing "HeightSamples" and "HoleMask" keep the current heights and holes (unless Width or Depth changes
        // their count).
        void DeserializeKeepingHeavyData(const std::vector<Property> &properties) override { Deserialize(properties); }
        std::uint64_t GetHeavyDataRevision() const override
        {
            return (m_heightDataPath.empty() ? m_heightsRevision : 0) + m_holesRevision;
        }
        void Deserialize(const std::vector<Property> &properties) override;

        // ---- heightmaps from other tools ---------------------------------------------------------------------------
        // Replaces the heights (and the grid's size) with a heightmap file: 8/16-bit images, raw R16/R32F or
        // .plutoheight (TerrainHeightData.h). Normalized sources map 0..1 to `heightRange` (default 0..HeightScale);
        // float sources keep their values. Heights are clamped to 0..HeightScale. Holes are cleared when the grid
        // changes size.
        bool ImportHeightMap(const std::string &filePath, const TerrainHeightImportOptions &options = {},
                             std::optional<glm::vec2> heightRange = std::nullopt, std::string *error = nullptr);
        // ImportHeightMap with default options; remembers the path as HeightMap (reloaded when a scene names a
        // different one).
        bool LoadHeightMap(const std::string &filePath);

        // ---- heights stored in a .plutoheight file -----------------------------------------------------------------
        // With a height file the heights load from and save to it, and the scene no longer carries them (like the
        // layer weight PNGs). Assigning a path neither loads nor saves.
        const std::string &GetHeightDataPath() const { return m_heightDataPath; }
        void SetHeightDataPath(std::string reference) { m_heightDataPath = std::move(reference); }
        bool LoadHeightData(std::string *error = nullptr);
        bool SaveHeightData(TerrainHeightEncoding encoding = TerrainHeightEncoding::Float32, std::string *error = nullptr) const;
        // The heights and border as they would be saved; and replacing them (the grid takes the data's size).
        TerrainHeightData GetHeightData() const;
        void SetHeightData(TerrainHeightData data);
        // The samples just outside the grid (TerrainHeightBorder): empty repeats the edges, as a lone terrain does.
        const std::vector<float> &GetHeightBorder() const { return m_border; }
        // Replaces the border ring (a neighbouring tile changed); a ring of the wrong size clears it.
        void SetHeightBorder(std::vector<float> border);

        // ---- editing ---------------------------------------------------------------------------------------------
        // One dab of a sculpt brush centred on a world position; returns the samples changed.
        TerrainSampleRect Sculpt(const glm::vec3 &worldPosition, const TerrainSculptBrush &brush, float deltaTime);
        // One dab of a hole brush (cuts or fills the cells whose centres it covers); true when anything changed.
        bool PaintHoles(const glm::vec3 &worldPosition, const TerrainHoleBrush &brush);
        // One dab of a layer brush; returns the weight texels changed.
        TerrainWeightRect PaintLayer(const glm::vec3 &worldPosition, const TerrainPaintBrush &brush);

        // Legacy convenience for scripts and older tools: the same edits with a brush held by the component (the
        // Paint* and Brush* setters below). It is not saved with the scene.
        TerrainSampleRect SculptAtWorldPosition(const glm::vec3 &worldPosition, float deltaTime);
        bool PaintAtWorldPosition(const glm::vec3 &worldPosition, float deltaTime);
        // Properties older scenes saved for the brush; read, never written.
        bool IsRetiredProperty(std::string_view name) const override;
        TerrainHeightRegion CaptureHeights(const TerrainSampleRect &rect) const;
        // Writes a region back (undo, or a tool's edit); samples outside the grid are ignored.
        void RestoreHeights(const TerrainHeightRegion &region);
        // The whole grid as a rect.
        TerrainSampleRect GetSampleBounds() const { return {0, 0, m_width, m_depth}; }
        // The sample nearest a world position, clamped to the grid.
        glm::ivec2 WorldToSample(const glm::vec3 &worldPosition) const;
        bool Raycast(const glm::vec3 &worldOrigin, const glm::vec3 &worldDirection, glm::vec3 &worldHitPoint) const;
        const std::vector<float> &GetHeightSamples() const { return m_heights; }
        // Bumped by every change to the height samples (collision shape caches rebuild on it).
        std::uint64_t GetHeightsRevision() const { return m_heightsRevision; }

        void SetSize(int width, int depth);
        int GetWidth() const { return m_width; }
        int GetDepth() const { return m_depth; }
        float GetCellSize() const { return m_cellSize; }
        float GetHeightScale() const { return m_heightScale; }
        float GetSurfaceSmoothing() const { return m_surfaceSmoothing; }
        float GetHeightAtLocalPosition(float x, float z) const;
        // The height the mesh draws at sample (x, z), at full detail: the stored sample smoothed by SurfaceSmoothing.
        // Samples outside the grid clamp to its edge. Surfaces that must sit on the drawn terrain (detail grass) use it.
        float GetSurfaceSampleHeight(int x, int z) const;
        // Bounded local surface query. Unlike GetHeightAtLocalPosition, this
        // rejects out-of-terrain coordinates instead of clamping them.
        bool TrySampleSurface(float x, float z, float &height, glm::vec3 &normal) const;
        // The same in world space: the surface's world height and normal under a world position (vertically, along
        // the terrain's up axis); false off the terrain.
        bool TrySampleWorldSurface(const glm::vec3 &worldPosition, float &worldHeight, glm::vec3 &worldNormal) const;
        int GetChunkSize() const { return m_chunkSize; }
        void SetLodCount(int lodCount);
        int GetLodCount() const { return m_lodCount; }
        // Multiplies how far each chunk keeps its detail (2 switches to coarser LODs at twice the distance). Large
        // terrains seen from far away raise it so ridgelines keep their shape; dense small ones lower it.
        void SetLodDistanceScale(float scale);
        float GetLodDistanceScale() const { return m_lodDistanceScale; }
        // Build the mesh (and the layers' macro albedo) on worker threads (a world's tiles): the previous ones, or none,
        // draw until the new ones land, a frame or two later. Off by default: a terrain changed in a frame draws
        // changed in that frame.
        void SetAsyncMeshBuild(bool async)
        {
            m_asyncMeshBuild = async;
            m_layers.SetAsyncMacroAlbedo(async);
        }
        // Whether a mesh has been built and can draw.
        [[nodiscard]] bool HasMesh() const { return m_mesh != nullptr; }
        // Brings the mesh up to date now (waiting for a worker build), as a loading screen wants.
        void FinishMeshBuild();
        // Depth of a wall hung from the terrain's outer edges (0: none). Hides cracks where tiles drawn at different
        // detail meet; it is drawn, not collided with.
        void SetSkirtDepth(float depth);
        float GetSkirtDepth() const { return m_skirtDepth; }
        void SetStatic(bool isStatic) { m_isStatic = isStatic; }
        bool IsStatic() const { return m_isStatic; }
        render::Mesh *GetMeshForBaking();

        // Holes: cells cut out of the terrain (a cave mouth in a hillside). A hole cell is not drawn and does not
        // collide, TrySampleSurface and Raycast report no surface there, and the Hole and Fill paint modes edit them.
        // Cell (x, z) spans samples x..x+1 and z..z+1; the mask always has (width - 1) x (depth - 1) cells.
        const TerrainHoleMask &GetHoles() const { return m_holes; }
        // Replaces the mask (resized to the terrain's cells).
        void SetHoles(TerrainHoleMask holes);
        // Returns whether the cell changed.
        bool SetHole(int cellX, int cellZ, bool hole);
        // Turns the holes' stepped rims into diagonal ones (TerrainHoleMask::SmoothEdges). Returns whether it changed.
        bool SmoothHoleEdges();
        // Derived holes: masks other systems keep on the terrain (a TerrainVolumeComponent cuts the cells its dug ground
        // replaces). They are not saved and not edited by the hole brush; GetHoles and the HoleMask property stay the
        // authored holes alone. Each source owns one mask; an empty mask removes it.
        void SetDerivedHoles(std::string_view source, TerrainHoleMask holes);
        [[nodiscard]] const TerrainHoleMask *FindDerivedHoles(std::string_view source) const;
        // What the terrain draws, collides with and answers queries about: the authored holes united with every
        // derived mask. The same object as GetHoles when there are no derived holes.
        const TerrainHoleMask &GetEffectiveHoles() const { return m_derivedHoles.empty() ? m_holes : m_effectiveHoles; }
        // Whether the effective holes cut the point.
        bool IsHoleAtLocalPosition(float x, float z) const;
        bool IsHoleAtWorldPosition(const glm::vec3 &worldPosition) const;
        // Bumped by every change to the holes, authored or derived (physics and other caches rebuild on it).
        std::uint64_t GetHolesRevision() const { return m_holesRevision; }

        void SetMaterial(render::Material *material);
        render::Material *GetMaterial() const { return m_material; }
        void SetMaterialAssetReference(const std::string &materialAssetReference);
        const std::string &GetMaterialAssetReference() const { return m_materialAssetReference; }
        void SetPaintedAlbedoPath(const std::string &path);
        const std::string &GetPaintedAlbedoPath() const { return m_paintedAlbedoPath; }
        const std::string &GetHeightMapPath() const { return m_heightMapPath; }

        // The legacy brush (session state for SculptAtWorldPosition / PaintAtWorldPosition; not saved).
        void SetPaintEnabled(bool enabled) { m_paintEnabled = enabled; }
        bool IsPaintEnabled() const { return m_paintEnabled; }
        void SetPaintMode(TerrainPaintMode mode) { m_paintMode = mode; }
        TerrainPaintMode GetPaintMode() const { return m_paintMode; }
        void SetBrushRadius(float radius) { m_brushRadius = glm::max(radius, 0.05f); }
        float GetBrushRadius() const { return m_brushRadius; }
        void SetBrushStrength(float strength) { m_brushStrength = glm::max(strength, 0.0f); }
        float GetBrushStrength() const { return m_brushStrength; }
        void SetFlattenHeight(float height) { m_flattenHeight = height; }
        float GetFlattenHeight() const { return m_flattenHeight; }

        // Material layers: surfaces painted onto the terrain and blended by weight (see TerrainLayerStack). With
        // no layers the terrain draws with its material as before; with layers it draws with a private copy of
        // that material carrying the layers (GetRenderMaterial).
        TerrainLayerStack &GetLayers() { return m_layers; }
        const TerrainLayerStack &GetLayers() const { return m_layers; }
        render::Material *GetRenderMaterial();
        // One dab of the selected layer at a world position, with the brush radius, layer opacity and hardness.
        TerrainWeightRect PaintLayerAtWorldPosition(const glm::vec3 &worldPosition);
        // One dab of `layer` with an explicit brush (`radius` in world units), e.g. from gameplay scripts.
        TerrainWeightRect PaintLayerAtWorldPosition(const glm::vec3 &worldPosition, std::size_t layer, float radius,
                                                    const TerrainLayerBrush &brush);
        // The terrain's 0..1 UV under a world position (the weight maps' coordinates).
        glm::vec2 WorldToTerrainUv(const glm::vec3 &worldPosition) const;
        void SetPaintLayer(int layer) { m_paintLayer = glm::max(layer, 0); }
        int GetPaintLayer() const { return m_paintLayer; }
        void SetLayerOpacity(float opacity) { m_layerOpacity = glm::clamp(opacity, 0.0f, 1.0f); }
        float GetLayerOpacity() const { return m_layerOpacity; }
        void SetBrushHardness(float hardness) { m_brushHardness = glm::clamp(hardness, 0.0f, 1.0f); }
        float GetBrushHardness() const { return m_brushHardness; }

    private:
        enum class HeavyDetail { Omit, Shared };
        std::vector<Property> SerializeProperties(HeavyDetail detail) const;
        float GetHeightSample(int x, int z) const;
        void SetHeightSample(int x, int z, float height);
        // Replaces the grid, heights and border together (imports and height files).
        void ReplaceHeights(int width, int depth, std::vector<float> heights, std::vector<float> border);
        void HeightsChanged();
        float SampleHeight(float x, float z) const;
        void EnsureHeightStorage();
        void HolesChanged();
        // Fits the authored and derived masks to the grid's cells and rebuilds the effective mask.
        void ResizeHoleMasks();
        void RebuildEffectiveHoles();
        void MarkMeshDirty();
        // Builds the mesh now (waiting for a build in flight).
        void RebuildMesh();
        [[nodiscard]] TerrainMeshInputs CaptureMeshInputs() const;
        void StartMeshBuild();
        // Takes a finished worker build.
        void PollMeshBuild();
        void RebuildMaterialFromReference();
        static TerrainLayerStack::Services EngineLayerServices();

        int m_width = 65;
        int m_depth = 65;
        float m_cellSize = 1.0f;
        float m_heightScale = 10.0f;
        float m_surfaceSmoothing = 0.35f;
        int m_chunkSize = 32;
        int m_lodCount = 4;
        float m_lodDistanceScale = 1.0f;
        float m_skirtDepth = 0.0f;
        bool m_isStatic = false;
        std::vector<float> m_heights;
        std::vector<float> m_border; // TerrainHeightBorder ring, or empty
        std::string m_heightDataPath;
        std::uint64_t m_heightsRevision = 0;
        HeavyPropertyCache m_heightsText;
        TerrainHoleMask m_holes;                                       // authored, saved
        std::map<std::string, TerrainHoleMask, std::less<>> m_derivedHoles; // by source, not saved
        TerrainHoleMask m_effectiveHoles;                              // m_holes united with m_derivedHoles
        std::uint64_t m_holesRevision = 0;
        HeavyPropertyCache m_holesText;

        std::unique_ptr<render::Mesh> m_mesh;
        std::future<render::MeshConfig> m_meshBuild;
        bool m_asyncMeshBuild = false;
        render::Material *m_material = nullptr;
        std::string m_materialAssetReference;
        std::string m_paintedAlbedoPath;
        std::string m_heightMapPath;
        bool m_meshDirty = true;

        bool m_paintEnabled = false;
        TerrainPaintMode m_paintMode = TerrainPaintMode::Raise;
        float m_brushRadius = 3.0f;
        float m_brushStrength = 2.0f;
        float m_flattenHeight = 0.0f;

        TerrainLayerStack m_layers{EngineLayerServices()};
        std::unique_ptr<render::Material> m_layeredMaterial;
        const render::Material *m_layeredBase = nullptr;
        std::uint64_t m_layeredBaseRevision = 0;
        int m_paintLayer = 0;
        float m_layerOpacity = 0.5f;
        float m_brushHardness = 0.5f;

        glm::mat4 m_previousModelMatrix = glm::mat4(1.0f);
        bool m_hasPreviousModelMatrix = false;
    };
}
