#include "PlutoGE/ui/panels/AnimationClipEditorPanel.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include <ImSequencer.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numeric>
#include <limits>
#include <optional>
#include <bit>
#include <functional>
#include <glm/gtc/quaternion.hpp>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float Fps = 60.0f;
        int Frame(float time) { return std::max(0, static_cast<int>(std::lround(time * Fps))); }
        float Time(int frame, float duration) { return std::clamp(frame / Fps, 0.0f, duration); }

        const char *PathName(render::AnimationTargetPath path)
        {
            switch (path)
            {
            case render::AnimationTargetPath::Translation: return "Translation";
            case render::AnimationTargetPath::Rotation: return "Rotation";
            case render::AnimationTargetPath::Scale: return "Scale";
            }
            return "Transform";
        }

        unsigned int PathColor(render::AnimationTargetPath path)
        {
            constexpr std::array<unsigned int, 3> colors{0xff6ecf72, 0xffe59b5a, 0xffd06bd8};
            return colors[std::clamp(static_cast<int>(path), 0, 2)];
        }

        struct TimelineItem
        {
            enum class Kind { Bone, Channel, Events } kind;
            std::size_t channel = std::numeric_limits<std::size_t>::max();
            render::AnimationTargetPath path = render::AnimationTargetPath::Translation;
            int startFrame = 0, endFrame = 0;
            std::vector<int> keyFrames;
            std::string label;
            std::string bone;
            unsigned int color = 0xffffffff;
        };

        std::string TargetName(const render::AnimationChannel &channel);

        // Owns ImSequencer's integer handles and commits them to the clip after drawing.
        // This keeps a UI dependency out of the renderer's float-based asset model.
        class ClipSequence final : public ImSequencer::SequenceInterface
        {
        public:
            ClipSequence(const render::AnimationClip &clip, std::vector<std::string> &expandedBones,
                         int &selectedItem, int &selectedKey, int &currentFrame)
                : m_duration(clip.duration), m_expandedBones(expandedBones), m_selectedItem(selectedItem),
                  m_selectedKey(selectedKey), m_currentFrame(currentFrame)
            {
                std::vector<std::string> bones;
                for (const auto &channel : clip.channels)
                    if (std::find(bones.begin(), bones.end(), TargetName(channel)) == bones.end()) bones.push_back(TargetName(channel));
                std::sort(bones.begin(), bones.end());
                for (const auto &target : bones)
                {
                    const bool expanded = IsExpanded(target);
                    m_items.push_back({TimelineItem::Kind::Bone, {}, render::AnimationTargetPath::Translation,
                                       0, GetFrameMax(), {}, std::string(expanded ? "v " : "> ") + target,
                                       target, 0xff4c4c4c});
                    if (!IsExpanded(target)) continue;
                    for (int pathIndex = 0; pathIndex < 3; ++pathIndex)
                    {
                        const auto path = static_cast<render::AnimationTargetPath>(pathIndex);
                        const auto found = std::find_if(clip.channels.begin(), clip.channels.end(), [&](const auto &channel) {
                            return TargetName(channel) == target && channel.path == path;
                        });
                        const std::size_t channelIndex = found == clip.channels.end()
                                                             ? std::numeric_limits<std::size_t>::max()
                                                             : static_cast<std::size_t>(found - clip.channels.begin());
                        std::vector<int> frames;
                        if (found != clip.channels.end())
                        {
                            frames.reserve(found->times.size());
                            std::transform(found->times.begin(), found->times.end(), std::back_inserter(frames), Frame);
                        }
                        const auto bounds = std::minmax_element(frames.begin(), frames.end());
                        m_items.push_back({TimelineItem::Kind::Channel, channelIndex, path,
                                           frames.empty() ? 0 : *bounds.first, frames.empty() ? 0 : *bounds.second,
                                           std::move(frames), "    " + std::string(PathName(path)), target, PathColor(path)});
                    }
                }
                std::vector<int> eventFrames;
                eventFrames.reserve(clip.events.size());
                for (const auto &event : clip.events) eventFrames.push_back(Frame(event.time));
                const auto eventBounds = std::minmax_element(eventFrames.begin(), eventFrames.end());
                m_items.push_back({TimelineItem::Kind::Events, {}, render::AnimationTargetPath::Translation,
                                   eventFrames.empty() ? 0 : *eventBounds.first,
                                   eventFrames.empty() ? 0 : *eventBounds.second,
                                   std::move(eventFrames), "Animation Events", {}, 0xff58b8d8});
            }

            int GetFrameMin() const override { return 0; }
            int GetFrameMax() const override { return std::max(1, static_cast<int>(std::ceil(m_duration * Fps))); }
            int GetItemCount() const override { return static_cast<int>(m_items.size()); }
            const char *GetItemLabel(int index) const override { return m_items[static_cast<std::size_t>(index)].label.c_str(); }
            const char *GetCollapseFmt() const override { return "%d Frames / %d keys"; }
            void Get(int index, int **start, int **end, int *type, unsigned int *color) override
            {
                auto &item = m_items[static_cast<std::size_t>(index)];
                if (start) *start = &item.startFrame;
                if (end) *end = &item.endFrame;
                if (type) *type = item.kind == TimelineItem::Kind::Events ? 1 : 0;
                if (color) *color = item.color;
            }
            void CustomDrawCompact(int index, ImDrawList *drawList, const ImRect &rect, const ImRect &clippingRect) override
            {
                const auto &item = m_items[static_cast<std::size_t>(index)];
                const ImRect legendRect({clippingRect.Min.x - 200.0f, rect.Min.y},
                                        {clippingRect.Min.x, rect.Max.y});
                if (item.kind == TimelineItem::Kind::Bone)
                {
                    if (legendRect.Contains(ImGui::GetMousePos()) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        ToggleBone(item.bone);
                    return;
                }
                if (legendRect.Contains(ImGui::GetMousePos()) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    m_selectedItem = index;
                    m_selectedKey = -1;
                }
                const float pixelsPerFrame = (rect.Max.x - rect.Min.x) / static_cast<float>(GetFrameMax() - GetFrameMin() + 2);
                drawList->PushClipRect(clippingRect.Min, clippingRect.Max, true);
                for (std::size_t key = 0; key < item.keyFrames.size(); ++key)
                {
                    const int frame = item.keyFrames[key];
                    const float x = rect.Min.x + (frame - GetFrameMin() + 0.5f) * pixelsPerFrame;
                    const float y = (rect.Min.y + rect.Max.y) * 0.5f;
                    if (ImRect({x - 7, y - 7}, {x + 7, y + 7}).Contains(ImGui::GetMousePos()) &&
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        m_selectedItem = index;
                        m_selectedKey = static_cast<int>(key);
                        m_currentFrame = frame;
                    }
                    if (item.kind == TimelineItem::Kind::Events)
                    {
                        drawList->AddCircleFilled({x, y}, 5.0f, item.color);
                        drawList->AddLine({x, y - 8}, {x, y + 8}, 0xffffffff, 1.5f);
                    }
                    else
                    {
                        drawList->AddQuadFilled({x, y - 5}, {x + 5, y}, {x, y + 5}, {x - 5, y}, item.color);
                        drawList->AddQuad({x, y - 5}, {x + 5, y}, {x, y + 5}, {x - 5, y}, 0xffffffff, 1.0f);
                    }
                }
                drawList->PopClipRect();
            }
            const TimelineItem *Item(int index) const
            {
                return index >= 0 && index < static_cast<int>(m_items.size()) ? &m_items[static_cast<std::size_t>(index)] : nullptr;
            }
        private:
            bool IsExpanded(const std::string &bone) const
            {
                return std::find(m_expandedBones.begin(), m_expandedBones.end(), bone) != m_expandedBones.end();
            }
            void ToggleBone(const std::string &bone)
            {
                const auto found = std::find(m_expandedBones.begin(), m_expandedBones.end(), bone);
                if (found == m_expandedBones.end()) m_expandedBones.push_back(bone);
                else m_expandedBones.erase(found);
            }

            float m_duration;
            std::vector<TimelineItem> m_items;
            std::vector<std::string> &m_expandedBones;
            int &m_selectedItem;
            int &m_selectedKey;
            int &m_currentFrame;
        };

        std::string TargetName(const render::AnimationChannel &channel)
        {
            return channel.targetName.empty() ? "Joint " + std::to_string(channel.jointIndex) : channel.targetName;
        }

        glm::vec4 DefaultValue(render::AnimationTargetPath path)
        {
            if (path == render::AnimationTargetPath::Rotation) return {0, 0, 0, 1};
            if (path == render::AnimationTargetPath::Scale) return {1, 1, 1, 0};
            return {0, 0, 0, 0};
        }

        glm::vec4 SampleChannel(const render::AnimationChannel &channel, float time)
        {
            const std::size_t count = std::min(channel.times.size(), channel.values.size());
            if (count == 0) return DefaultValue(channel.path);
            std::size_t before = 0, after = 0;
            float beforeTime = -std::numeric_limits<float>::infinity();
            float afterTime = std::numeric_limits<float>::infinity();
            for (std::size_t i = 0; i < count; ++i)
            {
                if (channel.times[i] <= time && channel.times[i] > beforeTime) { before = i; beforeTime = channel.times[i]; }
                if (channel.times[i] >= time && channel.times[i] < afterTime) { after = i; afterTime = channel.times[i]; }
            }
            if (!std::isfinite(beforeTime)) return channel.values[after];
            if (!std::isfinite(afterTime)) return channel.values[before];
            if (channel.interpolation == render::AnimationInterpolation::Step || before == after) return channel.values[before];
            const float blend = afterTime > beforeTime ? std::clamp((time - beforeTime) / (afterTime - beforeTime), 0.0f, 1.0f) : 0.0f;
            if (channel.path == render::AnimationTargetPath::Rotation)
            {
                const auto &a = channel.values[before];
                const auto &b = channel.values[after];
                const glm::quat rotation = glm::normalize(glm::slerp(glm::quat(a.w, a.x, a.y, a.z),
                                                                     glm::quat(b.w, b.x, b.y, b.z), blend));
                return {rotation.x, rotation.y, rotation.z, rotation.w};
            }
            return glm::mix(channel.values[before], channel.values[after], blend);
        }

        std::optional<std::size_t> KeyAtFrame(const render::AnimationChannel &channel, int frame)
        {
            for (std::size_t i = 0; i < channel.times.size(); ++i)
                if (Frame(channel.times[i]) == frame) return i;
            return std::nullopt;
        }

        std::size_t ClipSignature(const render::AnimationClip &clip)
        {
            std::size_t signature = std::hash<std::string>{}(clip.name);
            auto combine = [&](std::size_t value) {
                signature ^= value + 0x9e3779b9u + (signature << 6u) + (signature >> 2u);
            };
            combine(std::bit_cast<std::uint32_t>(clip.duration));
            combine(clip.channels.size());
            for (const auto &channel : clip.channels)
            {
                combine(std::hash<std::string>{}(channel.targetName));
                combine(static_cast<std::size_t>(channel.path));
                combine(static_cast<std::size_t>(channel.interpolation));
                for (const float time : channel.times) combine(std::bit_cast<std::uint32_t>(time));
                for (const auto &value : channel.values)
                    for (const float component : {value.x, value.y, value.z, value.w}) combine(std::bit_cast<std::uint32_t>(component));
            }
            combine(clip.events.size());
            for (const auto &event : clip.events)
            {
                combine(std::bit_cast<std::uint32_t>(event.time));
                combine(std::hash<std::string>{}(event.name));
                combine(std::hash<std::string>{}(event.stringParameter));
                combine(std::bit_cast<std::uint32_t>(event.floatParameter));
                combine(static_cast<std::size_t>(event.intParameter));
            }
            return signature;
        }

        render::AnimationChannel *EnsureChannel(render::AnimationClip &clip, const std::string &bone,
                                                render::AnimationTargetPath path)
        {
            const auto existing = std::find_if(clip.channels.begin(), clip.channels.end(), [&](const auto &channel) {
                return TargetName(channel) == bone && channel.path == path;
            });
            if (existing != clip.channels.end()) return &*existing;
            const auto source = std::find_if(clip.channels.begin(), clip.channels.end(), [&](const auto &channel) {
                return TargetName(channel) == bone;
            });
            if (source == clip.channels.end()) return nullptr;
            clip.channels.push_back(*source);
            auto &channel = clip.channels.back();
            channel.path = path;
            channel.times.clear();
            channel.values.clear();
            clip.channelCount = static_cast<int>(clip.channels.size());
            return &channel;
        }

        void SortChannel(render::AnimationChannel &channel)
        {
            std::vector<std::size_t> order(channel.times.size());
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return channel.times[a] < channel.times[b]; });
            const auto times = channel.times;
            const auto values = channel.values;
            for (std::size_t i = 0; i < order.size(); ++i)
            {
                channel.times[i] = times[order[i]];
                if (i < channel.values.size() && order[i] < values.size()) channel.values[i] = values[order[i]];
            }
        }
    }

    AnimationClipEditorPanel::~AnimationClipEditorPanel()
    {
        StopPreview();
    }

    bool AnimationClipEditorPanel::BeginPreview()
    {
        StopPreview();
        auto *entity = EditorShell::GetInstance().GetSelectedEntity();
        auto *animation = entity ? entity->GetComponent<scene::AnimationComponent>() : nullptr;
        if (!entity || !animation) return false;

        m_previewEntityId = entity->GetID();
        m_previewOriginalClips = animation->GetClips();
        m_previewOriginalClipIndex = animation->GetCurrentClipIndex();
        m_previewOriginalTime = animation->GetTime();
        m_previewOriginalSpeed = animation->GetSpeed();
        m_previewOriginalPlaying = animation->IsPlaying();
        m_previewOriginalLooping = animation->IsLooping();
        animation->Pause();
        animation->SetClipsFromImportedAnimations({m_clip});
        animation->SetCurrentClipIndex(0);
        animation->SetEditorPreviewMode(true);
        animation->Pause();
        animation->SetLooping(false);
        animation->SetTime(Time(m_currentFrame, m_clip.duration));
        m_previewClipSignature = ClipSignature(m_clip);
        return true;
    }

    void AnimationClipEditorPanel::StopPreview()
    {
        if (m_previewEntityId != 0)
        {
            auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
            auto *entity = scene ? scene->FindEntityByID(m_previewEntityId) : nullptr;
            if (auto *animation = entity ? entity->GetComponent<scene::AnimationComponent>() : nullptr)
            {
                animation->SetEditorPreviewMode(false);
                animation->SetClipsFromImportedAnimations(m_previewOriginalClips);
                animation->SetCurrentClipIndex(m_previewOriginalClipIndex);
                animation->SetLooping(m_previewOriginalLooping);
                animation->SetSpeed(m_previewOriginalSpeed);
                animation->SetTime(m_previewOriginalTime);
                animation->SetPlaying(m_previewOriginalPlaying);
            }
        }
        m_previewEntityId = 0;
        m_previewOriginalClips.clear();
        m_previewPlaying = false;
        m_previewClipSignature = 0;
    }

    void AnimationClipEditorPanel::UpdatePreview(bool advancePlayback)
    {
        if (m_previewEntityId == 0) return;
        auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
        auto *entity = scene ? scene->FindEntityByID(m_previewEntityId) : nullptr;
        auto *animation = entity ? entity->GetComponent<scene::AnimationComponent>() : nullptr;
        if (!animation)
        {
            m_previewEntityId = 0;
            m_previewOriginalClips.clear();
            m_previewPlaying = false;
            return;
        }

        const std::size_t signature = ClipSignature(m_clip);
        if (signature != m_previewClipSignature)
        {
            const float time = Time(m_currentFrame, m_clip.duration);
            animation->SetClipsFromImportedAnimations({m_clip});
            animation->SetCurrentClipIndex(0);
            animation->SetEditorPreviewMode(true);
            animation->Pause();
            animation->SetTime(time);
            m_previewClipSignature = signature;
        }

        if (advancePlayback && m_previewPlaying && m_clip.duration > 0.0f)
        {
            float time = Time(m_currentFrame, m_clip.duration) + ImGui::GetIO().DeltaTime;
            if (time > m_clip.duration)
            {
                if (m_previewLooping) time = std::fmod(time, m_clip.duration);
                else { time = m_clip.duration; m_previewPlaying = false; }
            }
            m_currentFrame = Frame(time);
        }
        animation->SetLooping(false);
        animation->SetTime(Time(m_currentFrame, m_clip.duration));
    }

    void AnimationClipEditorPanel::LoadActiveClip()
    {
        StopPreview();
        auto &editor = EditorShell::GetInstance();
        m_loadedReference = editor.GetActiveAnimationClipAssetReference();
        m_clip = {};
        if (!m_loadedReference.empty())
            core::Engine::GetInstance().GetAssetManager().LoadAnimationClipAsset(m_loadedReference, m_clip);
        m_currentFrame = m_firstFrame = 0;
        m_selectedItem = -1;
        m_selectedKey = -1;
        m_expandedBones.clear();
        m_dirty = false;
    }

    void AnimationClipEditorPanel::Render()
    {
        auto &editor = EditorShell::GetInstance();
        const auto &reference = editor.GetActiveAnimationClipAssetReference();
        if (reference != m_loadedReference) LoadActiveClip();
        if (reference.empty())
        {
            ImGui::TextDisabled("Open an animation clip from the Content Browser.");
            return;
        }

        ImGui::TextUnformatted(reference.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%.3fs | 60 fps | %zu channels", m_clip.duration, m_clip.channels.size());

        auto *selectedEntity = editor.GetSelectedEntity();
        const bool selectedCanPreview = selectedEntity && selectedEntity->GetComponent<scene::AnimationComponent>();
        if (m_previewEntityId == 0)
        {
            ImGui::BeginDisabled(!selectedCanPreview);
            if (ImGui::Button("Preview on Selected Entity")) BeginPreview();
            ImGui::EndDisabled();
            if (!selectedCanPreview)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("Select a scene entity with an AnimationComponent.");
            }
        }
        else
        {
            if (ImGui::Button("Start"))
            {
                if (Time(m_currentFrame, m_clip.duration) >= m_clip.duration) m_currentFrame = 0;
                m_previewPlaying = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!m_previewPlaying);
            if (ImGui::Button("Pause")) m_previewPlaying = false;
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Stop"))
            {
                m_previewPlaying = false;
                m_currentFrame = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("End Preview")) StopPreview();
            ImGui::SameLine();
            ImGui::Checkbox("Loop", &m_previewLooping);
            ImGui::SameLine();
            if (auto *scene = editor.GetEngine().GetScene(); scene)
            {
                if (auto *entity = scene->FindEntityByID(m_previewEntityId)) ImGui::TextDisabled("Previewing: %s", entity->GetName().c_str());
            }
        }

        UpdatePreview(true);

        bool addedEvent = false;
        if (ImGui::Button("+ Add Event"))
        {
            m_clip.events.push_back({.time = Time(m_currentFrame, m_clip.duration), .name = "AnimationEvent"});
            m_selectedKey = static_cast<int>(m_clip.events.size()) - 1;
            addedEvent = true;
            m_dirty = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Playhead %.3fs", Time(m_currentFrame, m_clip.duration));

        ClipSequence sequence(m_clip, m_expandedBones, m_selectedItem, m_selectedKey, m_currentFrame);
        if (addedEvent) m_selectedItem = sequence.GetItemCount() - 1;
        if (sequence.GetItemCount() > 0)
        {
            const float timelineHeight = std::max(180.0f, ImGui::GetContentRegionAvail().y - 190.0f);
            ImGui::BeginChild("AnimationTimeline", ImVec2(0, timelineHeight), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImSequencer::Sequencer(&sequence, &m_currentFrame, &m_expanded, &m_selectedItem, &m_firstFrame,
                                   ImSequencer::SEQUENCER_CHANGE_FRAME);
            ImGui::EndChild();
        }
        else
        {
            ImGui::BeginChild("EmptyAnimationTimeline", ImVec2(ImGui::GetContentRegionAvail().x, 150), ImGuiChildFlags_Borders);
            ImGui::TextDisabled("No transform keys or events. Add one at the playhead.");
            ImGui::EndChild();
        }

        if (const auto *item = sequence.Item(m_selectedItem); item && item->kind == TimelineItem::Kind::Bone)
            m_selectedItem = -1;
        const auto *selected = sequence.Item(m_selectedItem);
        if (selected)
        {
            ImGui::SeparatorText(selected->kind == TimelineItem::Kind::Events ? "Animation Events" : "Selected Transform Track");
            if (selected->kind == TimelineItem::Kind::Events && !m_clip.events.empty())
            {
                const auto nearest = std::min_element(m_clip.events.begin(), m_clip.events.end(), [&](const auto &a, const auto &b) {
                    return std::abs(Frame(a.time) - m_currentFrame) < std::abs(Frame(b.time) - m_currentFrame);
                });
                const int eventIndex = m_selectedKey >= 0 && m_selectedKey < static_cast<int>(m_clip.events.size())
                                           ? m_selectedKey : static_cast<int>(nearest - m_clip.events.begin());
                ImGui::SetNextItemWidth(260);
                if (ImGui::BeginCombo("Event", nearest->name.c_str()))
                {
                    for (int i = 0; i < static_cast<int>(m_clip.events.size()); ++i)
                        if (ImGui::Selectable((m_clip.events[static_cast<std::size_t>(i)].name + " @ " + std::to_string(m_clip.events[static_cast<std::size_t>(i)].time) + "s").c_str(), i == eventIndex))
                        {
                            m_currentFrame = Frame(m_clip.events[static_cast<std::size_t>(i)].time);
                            m_selectedKey = i;
                        }
                    ImGui::EndCombo();
                }
                auto &event = m_clip.events[static_cast<std::size_t>(eventIndex)];
                char name[128]{}, text[256]{};
                strncpy_s(name, event.name.c_str(), _TRUNCATE);
                strncpy_s(text, event.stringParameter.c_str(), _TRUNCATE);
                if (ImGui::InputText("Name", name, sizeof(name))) { event.name = name; m_dirty = true; }
                if (ImGui::InputText("String Parameter", text, sizeof(text))) { event.stringParameter = text; m_dirty = true; }
                m_dirty |= ImGui::DragFloat("Float Parameter", &event.floatParameter, 0.01f);
                m_dirty |= ImGui::DragInt("Int Parameter", &event.intParameter);
                if (ImGui::DragFloat("Time", &event.time, 0.01f, 0.0f, m_clip.duration, "%.3f s")) m_dirty = true;
            }
            else if (selected->kind == TimelineItem::Kind::Channel)
            {
                if (selected->channel >= m_clip.channels.size())
                {
                    ImGui::TextDisabled("This transform has no keyframes.");
                    if (ImGui::Button("+ Add Keyframe at Playhead"))
                    {
                        if (auto *channel = EnsureChannel(m_clip, selected->bone, selected->path))
                        {
                            channel->times.push_back(Time(m_currentFrame, m_clip.duration));
                            channel->values.push_back(DefaultValue(selected->path));
                            m_selectedKey = 0;
                            m_dirty = true;
                        }
                    }
                }
                else
                {
                auto &channel = m_clip.channels[selected->channel];
                const auto nearest = std::min_element(channel.times.begin(), channel.times.end(), [&](float a, float b) {
                    return std::abs(Frame(a) - m_currentFrame) < std::abs(Frame(b) - m_currentFrame);
                });
                const std::size_t key = m_selectedKey >= 0 && m_selectedKey < static_cast<int>(channel.times.size())
                                            ? static_cast<std::size_t>(m_selectedKey)
                                            : nearest == channel.times.end() ? 0 : static_cast<std::size_t>(nearest - channel.times.begin());
                if (!channel.times.empty())
                {
                    ImGui::SetNextItemWidth(180);
                    if (ImGui::BeginCombo("Keyframe", (std::to_string(channel.times[key]) + "s").c_str()))
                    {
                        for (std::size_t i = 0; i < channel.times.size(); ++i)
                            if (ImGui::Selectable((std::to_string(channel.times[i]) + "s").c_str(), i == key))
                            {
                                m_currentFrame = Frame(channel.times[i]);
                                m_selectedKey = static_cast<int>(i);
                            }
                        ImGui::EndCombo();
                    }
                    if (ImGui::DragFloat("Time", &channel.times[key], 0.01f, 0.0f, m_clip.duration, "%.3f s")) m_dirty = true;
                }
                const float playheadTime = Time(m_currentFrame, m_clip.duration);
                const auto playheadKey = KeyAtFrame(channel, m_currentFrame);
                glm::vec4 sampledValue = SampleChannel(channel, playheadTime);
                glm::vec4 *displayedValue = playheadKey && *playheadKey < channel.values.size()
                                                ? &channel.values[*playheadKey] : &sampledValue;
                ImGui::TextDisabled(playheadKey ? "Keyframe value at playhead" : "Interpolated value at playhead");
                const bool valueChanged = channel.path == render::AnimationTargetPath::Rotation
                                              ? ImGui::DragFloat4("Playhead Value", &displayedValue->x, 0.01f)
                                              : ImGui::DragFloat3("Playhead Value", &displayedValue->x, 0.01f);
                if (valueChanged)
                {
                    if (!playheadKey)
                    {
                        channel.times.push_back(playheadTime);
                        channel.values.push_back(sampledValue);
                        m_selectedKey = static_cast<int>(channel.times.size()) - 1;
                    }
                    else
                        m_selectedKey = static_cast<int>(*playheadKey);
                    m_dirty = true;
                }
                int interpolation = static_cast<int>(channel.interpolation);
                if (ImGui::Combo("Interpolation", &interpolation, "Linear\0Step\0"))
                {
                    channel.interpolation = static_cast<render::AnimationInterpolation>(interpolation);
                    m_dirty = true;
                }
                if (ImGui::Button("+ Add Keyframe at Playhead"))
                {
                    channel.times.push_back(Time(m_currentFrame, m_clip.duration));
                    channel.values.push_back(SampleChannel(channel, Time(m_currentFrame, m_clip.duration)));
                    m_selectedKey = static_cast<int>(channel.times.size()) - 1;
                    m_dirty = true;
                }
                }
            }
            if (ImGui::Button("Delete Selected"))
            {
                if (selected->kind == TimelineItem::Kind::Events && !m_clip.events.empty())
                {
                    const auto key = m_selectedKey >= 0 && m_selectedKey < static_cast<int>(m_clip.events.size())
                                         ? m_selectedKey : 0;
                    m_clip.events.erase(m_clip.events.begin() + key);
                }
                else if (selected->channel < m_clip.channels.size())
                {
                    auto &channel = m_clip.channels[selected->channel];
                    const auto nearest = m_selectedKey >= 0 && m_selectedKey < static_cast<int>(channel.times.size())
                                             ? channel.times.begin() + m_selectedKey
                                             : std::min_element(channel.times.begin(), channel.times.end(), [&](float a, float b) {
                                                   return std::abs(Frame(a) - m_currentFrame) < std::abs(Frame(b) - m_currentFrame);
                                               });
                    if (nearest != channel.times.end())
                    {
                        const auto key = static_cast<std::size_t>(nearest - channel.times.begin());
                        channel.times.erase(nearest);
                        if (key < channel.values.size()) channel.values.erase(channel.values.begin() + key);
                    }
                }
                m_selectedItem = -1;
                m_selectedKey = -1;
                m_dirty = true;
            }
        }

        // Timeline scrubbing and value edits happen after the playback update above;
        // apply them immediately so the viewport pose matches this UI frame.
        UpdatePreview(false);

        ImGui::Separator();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button("Save"))
        {
            for (auto &channel : m_clip.channels) SortChannel(channel);
            std::stable_sort(m_clip.events.begin(), m_clip.events.end(), [](const auto &a, const auto &b) { return a.time < b.time; });
            std::string error;
            if (core::Engine::GetInstance().GetAssetManager().SaveAnimationClipAsset(reference, m_clip, &error))
            {
                m_dirty = false;
                editor.MarkProjectDirty();
                editor.Log(EditorShell::ConsoleSeverity::Info, "Saved animation clip: " + reference);
            }
            else editor.Log(EditorShell::ConsoleSeverity::Error, error);
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert")) LoadActiveClip();
        ImGui::EndDisabled();
    }
}
