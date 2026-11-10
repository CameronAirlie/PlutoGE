#pragma once

#include "PlutoGE/render/FogFroxels.h"
#include "../screenspace/ScreenSpacePass.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace PlutoGE::render
{
    // The froxel lighting volume's targets and frame-to-frame state (FogFroxels.h, FogFroxels.slang). Two atlases
    // alternate: one is written this frame while the other, last frame's, is reprojected into it. The renderer
    // records the injection draw itself, since it binds the fog's lighting resources.
    class FogFroxelVolume
    {
    public:
        struct Frame
        {
            // The atlas to render this frame's volume into, and last frame's (null when it cannot be reused).
            rhi::TextureHandle target, history;
            FogFroxelGrid grid;
            // Post-process lanes 6 and 7 (FogFroxels.slang) and the matrix for worldToLocal.
            std::array<glm::vec4, 2> lanes{};
            glm::mat4 previousView{1.0f};
        };

        void SetSettings(const FogFroxelSettings &settings) noexcept;
        [[nodiscard]] const FogFroxelSettings &GetSettings() const noexcept { return m_settings; }

        // Plans this frame's volume for a view of the given size seen through `view`; nullopt when disabled, or when a
        // volume was already planned this frame (a second fog or camera in one frame marches without froxels rather
        // than disturbing the first's history).
        [[nodiscard]] std::optional<Frame> Begin(rhi::IRenderDevice &device, std::uint32_t viewWidth,
                                                 std::uint32_t viewHeight, const glm::mat4 &view,
                                                 const FogFroxelHistory::Appearance &appearance,
                                                 const FogFroxelLighting &lighting, std::uint64_t frame);
        void Reset() noexcept;

    private:
        FogFroxelSettings m_settings;
        FogFroxelHistory m_history;
        std::array<ScreenSpaceTarget, 2> m_atlases;
        std::uint32_t m_current = 0;
        glm::mat4 m_previousView{1.0f};
        std::optional<std::uint64_t> m_lastFrame;
    };
}
