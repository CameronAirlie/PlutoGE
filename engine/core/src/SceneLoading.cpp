#include "PlutoGE/core/SceneLoading.h"
#include "PlutoGE/platform/LoadingWork.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/Scene.h"
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace PlutoGE::core
{
    bool SceneLoading::Run(const std::function<void()> &work, const Presenter &present)
    {
        if (m_active) return false;
        m_active = true;
        ++m_sequence;
        struct ResetActive { bool &active; ~ResetActive() { active = false; } } reset{m_active};
        std::string presentationError;
        auto last = std::chrono::steady_clock::now();
        platform::LoadingWork pump([&]
        {
            const auto now = std::chrono::steady_clock::now();
            m_status.longestFrameMs = std::max(m_status.longestFrameMs,
                std::chrono::duration<double, std::milli>(now - last).count());
            last = now;
            // Checkpoints may be reached from native calls made by managed
            // OnCreate. Never unwind a presentation exception across that ABI.
            if (presentationError.empty())
            {
                try { present(m_status); ++m_status.presentedFrames; }
                catch (const std::exception &error) { presentationError = error.what(); }
                catch (...) { presentationError = "Loading screen presentation failed"; }
            }
        });
        try
        {
            platform::LoadingWork::Checkpoint(true);
            if (!presentationError.empty()) throw std::runtime_error(presentationError);
            work();
            m_status.stage = SceneLoadStage::Complete;
            platform::LoadingWork::Checkpoint(true);
            if (!presentationError.empty()) throw std::runtime_error(presentationError);
        }
        catch (const std::exception &error)
        {
            m_status.stage = SceneLoadStage::Failed;
            m_status.error = error.what();
        }
        catch (...)
        {
            m_status.stage = SceneLoadStage::Failed;
            m_status.error = "Unexpected scene loading failure";
        }
        std::clog << "Scene loading: " << m_status.path << "; frames=" << m_status.presentedFrames
                  << "; longest interval=" << m_status.longestFrameMs << " ms"
                  << (m_status.error.empty() ? "" : "; error=" + m_status.error) << '\n';
        return m_status.stage == SceneLoadStage::Complete;
    }

    bool SceneLoading::Load(const std::string &path, const Activation &activate, const Presenter &present)
    {
        if (m_active) return false;
        m_status = {.stage = SceneLoadStage::Reading, .path = path};
        return Run([&]
        {
            auto text = platform::LoadingWork::Prepare([path]
            {
                std::error_code error;
                const auto size = content::FileSize(path, error);
                if (error || !size || size > 256 * 1024 * 1024)
                    throw std::runtime_error("Scene file is missing, empty or exceeds 256 MiB");
                content::InputFile input(path, std::ios::binary);
                std::string data(static_cast<std::size_t>(size), '\0');
                if (!input.is_open() || !input.read(data.data(), static_cast<std::streamsize>(size)))
                    throw std::runtime_error("Could not read scene file");
                if (data.starts_with("\xef\xbb\xbf")) data.erase(0, 3);
                if (!data.starts_with("SCENE\t1\n") && !data.starts_with("SCENE\t1\r\n"))
                    throw std::runtime_error("Unsupported scene header");
                return data;
            });
            m_status.stage = SceneLoadStage::Constructing;
            platform::LoadingWork::Checkpoint(true);
            std::string error;
            auto candidate = scene::SceneSerializer::LoadFromString(text, &error);
            if (!candidate || !error.empty()) throw std::runtime_error(error.empty() ? "Scene construction failed" : error);
            candidate->SetFilePath(std::filesystem::absolute(path).lexically_normal().string());
            m_status.stage = SceneLoadStage::Activating;
            platform::LoadingWork::Checkpoint(true);
            activate(std::move(candidate));
        }, present);
    }

    bool SceneLoading::Activate(const std::function<void()> &activate, const Presenter &present)
    {
        if (m_active) return false;
        m_status = {.stage = SceneLoadStage::Activating};
        return Run(activate, present);
    }
}
