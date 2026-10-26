#pragma once

#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/ui/UIEvents.h"
#include "PlutoGE/scene/ui/UIGraphic.h"
#include "PlutoGE/scene/ui/UIInput.h"
#include "PlutoGE/ui/layout/Layout.h"
#include "PlutoGE/ui/render/DrawList.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace PlutoGE::ui
{
    class FontLibrary;
}

namespace PlutoGE::scene
{
    class Entity;
    class Scene;
    class UISelectable;
    class UIStyleSheet;
    class IUIPopup;

    // Camera used to place world-space canvases.
    struct UICameraInfo
    {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
    };

    inline constexpr std::uint32_t kNoUIElement = std::numeric_limits<std::uint32_t>::max();

    // One laid-out element. Pixel values use the target's bottom-left origin.
    struct UIResolvedElement
    {
        Entity *entity = nullptr;
        RectTransformLayout rect;      // pixel bounds of the transformed rect
        RectTransformLayout clipRect;  // pixel clip bounds inherited from ancestors
        float opacity = 1.0f;
        float rotation = 0.0f;
        glm::vec2 scale{1.0f};
        int sortingOrder = 0;
        std::uint64_t paintOrder = 0;
        bool raycastTarget = false;
        bool clipped = false;

        ui::Rect localRect;            // layout rect in canvas units
        ui::Transform2D toPixels;      // canvas units to target pixels (includes rotation and scale)
        std::uint32_t canvas = 0;      // index into GetCanvases()
        std::uint32_t parent = kNoUIElement;
    };

    struct UICanvasFrame
    {
        Entity *entity = nullptr;
        const CanvasComponent *canvas = nullptr;
        int sortingOrder = 0;
        float scale = 1.0f;            // canvas units to pixels
        ui::Transform2D toPixels;
        bool visible = true;           // false when a world canvas is behind the camera
        ui::DrawList drawList;         // filled by BuildDrawLists
    };

    // A semantic event for scripts: what happened to which entity this frame.
    struct UIEventRecord
    {
        UIEventType type = UIEventType::Click;
        UIEntityID entity = 0;
    };

    // Retained runtime state shared by layout, input, scripts, editor
    // overlays and rendering. Components remain the serialized authoring
    // model; each frame the system resolves them:
    //   rect drivers -> collect (hierarchy) -> layout (ui::LayoutTree)
    //   -> transforms and clips -> paint (IUIGraphic) / input and events.
    class UISystem final : public UIEventContext
    {
    public:
        UISystem();
        ~UISystem() override;

        // Lays out every native canvas for `input.viewportSize`, then routes
        // pointer, navigation and text input as UI events.
        void Update(Scene &scene, const UIInputContext &input, float deltaTime);
        // Lays out without touching input state.
        void RebuildLayout(Scene &scene, const glm::vec2 &viewportSize, const UICameraInfo *camera = nullptr);
        // Lays out and paints every native canvas. Returns canvases in draw order.
        const std::vector<UICanvasFrame> &BuildDrawLists(Scene &scene, const glm::vec2 &viewportSize,
                                                         const UICameraInfo *camera = nullptr);

        [[nodiscard]] const std::vector<UIResolvedElement> &GetElements() const { return m_elements; }
        [[nodiscard]] const std::vector<UICanvasFrame> &GetCanvases() const { return m_canvases; }
        [[nodiscard]] const UIResolvedElement *FindElement(std::uint32_t entityId) const override;
        // Topmost raycast-target element under a pixel position, or null.
        [[nodiscard]] const UIResolvedElement *HitTest(const glm::vec2 &pixel,
                                                       bool (*filter)(const UIResolvedElement &) = nullptr) const;
        [[nodiscard]] bool ContainsPoint(const UIResolvedElement &element, const glm::vec2 &pixel) const;

        // The interactable control under the pointer, the pressed control and the focused one.
        [[nodiscard]] std::uint32_t GetHoveredEntity() const { return m_hoveredSelectable; }
        [[nodiscard]] std::uint32_t GetFocusedEntity() const { return m_selected; }
        [[nodiscard]] std::uint32_t GetCapturedEntity() const { return m_pressHandler; }
        // Gameplay input should ignore the pointer / keyboard while these hold.
        // Only interactive elements capture the pointer: a HUD label does not.
        [[nodiscard]] bool IsPointerOverUI() const { return m_pointerOverInteractive || m_pressHandler != 0 || m_dragging; }
        [[nodiscard]] bool IsKeyboardCaptured() const;
        // Semantic events (clicks, value changes) raised by the latest Update.
        [[nodiscard]] const std::vector<UIEventRecord> &GetFrameEvents() const { return m_frameEvents; }
        // Advances once per Update, so readers can tell new frame events from old.
        [[nodiscard]] std::uint64_t GetUpdateSequence() const { return m_updateSequence; }

        [[nodiscard]] UIInputMapper &GetInputMapper() { return m_inputMapper; }
        void SetTextureInfo(std::shared_ptr<IUITextureInfo> textures);
        [[nodiscard]] IUITextureInfo &GetTextureInfo() { return *m_textures; }
        void SetClipboard(std::function<std::string()> get, std::function<void(const std::string &)> set);
        // Rect drivers (sliders, scroll views) and style sheets write to the
        // scene. Preview systems that only observe a scene (editor designers)
        // turn them off.
        void SetRectDriversEnabled(bool enabled) { m_rectDriversEnabled = enabled; }

        // UIEventContext
        [[nodiscard]] Entity *FindEntity(UIEntityID entity) const override;
        [[nodiscard]] glm::vec2 ToLocal(UIEntityID entity, glm::vec2 pixel) const override;
        void SetSelected(UIEntityID entity) override;
        [[nodiscard]] UIEntityID GetSelected() const override { return m_selected; }
        void Emit(UIEventType type, UIEntityID source) override;
        [[nodiscard]] float GetDeltaTime() const override { return m_deltaTime; }
        void OpenPopup(UIEntityID owner) override { m_openPopup = owner; }
        [[nodiscard]] std::string GetClipboardText() const override;
        void SetClipboardText(const std::string &text) override;

    private:
        struct ElementLinks
        {
            std::uint32_t layoutNode = 0;
            std::vector<std::uint32_t> children;
            std::vector<IUIEventHandler *> handlers;
            UISelectable *selectable = nullptr;
            IUIPopup *popup = nullptr;
        };

        struct CanvasLayout
        {
            std::uint32_t rootNode = 0;               // virtual layout root
            ui::Rect rootRect;                        // canvas units
            std::vector<std::uint32_t> topElements;   // elements directly under the root
        };

        void Collect(Entity *entity, std::uint32_t canvas, std::uint32_t parentElement, std::uint32_t parentNode,
                     const glm::vec2 &viewportSize, const UICameraInfo *camera, const UIMeasureContext &measure,
                     const UIStyleSheet *sheet);
        std::uint32_t BeginCanvas(Entity &entity, const CanvasComponent &canvas, const glm::vec2 &viewportSize,
                                  const UICameraInfo *camera);
        void SortCanvases();
        void ResolveElements(const glm::vec2 &viewportSize);
        void PaintElement(std::uint32_t element, ui::DrawList &list, ui::FontLibrary &fonts);

        // Input routing.
        [[nodiscard]] std::uint32_t IndexOf(UIEntityID entity) const;
        [[nodiscard]] UIEntityID IdOf(std::uint32_t element) const;
        [[nodiscard]] UIEntityID NearestSelectable(std::uint32_t element) const;
        void DispatchTo(std::uint32_t element, UIEvent &event);
        // Bubbles from `element` up the hierarchy; returns the consumer, or 0.
        UIEntityID Bubble(std::uint32_t element, UIEvent event);
        void ProcessPointer(const UIInputContext &input);
        bool ProcessPopup(const UIInputContext &input);
        void ProcessNavigation(const UIInputContext &input);
        void ProcessText(const UIInputContext &input);
        [[nodiscard]] UIEntityID FindNavigationTarget(UIEntityID from, glm::ivec2 direction) const;
        [[nodiscard]] UIEntityID FindDefaultSelection() const;
        [[nodiscard]] bool IsSelectableUsable(std::uint32_t element) const;

        Scene *m_scene = nullptr;
        std::vector<UIResolvedElement> m_elements;   // pre-order: parents before children
        std::vector<ElementLinks> m_links;           // parallel to m_elements
        std::vector<UICanvasFrame> m_canvases;       // draw order after SortCanvases
        std::vector<CanvasLayout> m_canvasLayouts;   // parallel to m_canvases
        std::vector<std::uint32_t> m_hitOrder;       // elements, topmost first
        std::unordered_map<std::uint32_t, std::size_t> m_elementByEntity;
        // The previous frame's elements, answering queries from rect drivers
        // while the current frame is being collected.
        std::vector<UIResolvedElement> m_previousElements;
        std::unordered_map<std::uint32_t, std::size_t> m_previousByEntity;
        bool m_collecting = false;
        bool m_rectDriversEnabled = true;
        ui::LayoutTree m_layout;
        std::shared_ptr<IUITextureInfo> m_textures;

        UIInputMapper m_inputMapper;
        std::vector<UIEntityID> m_hoverChain;        // pointer target and its ancestors
        UIEntityID m_hoveredSelectable = 0;
        bool m_pointerOverInteractive = false;
        UIEntityID m_pressTarget = 0;
        UIEntityID m_pressHandler = 0;
        UIEntityID m_dragHandler = 0;
        bool m_dragging = false;
        glm::vec2 m_pressPosition{0.0f};
        glm::vec2 m_lastPointer{0.0f};
        UIEntityID m_lastClick = 0;
        float m_lastClickTime = -1.0f;
        float m_time = 0.0f;
        UIEntityID m_selected = 0;
        UIEntityID m_openPopup = 0;
        float m_deltaTime = 0.0f;
        std::vector<UIEventRecord> m_frameEvents;
        std::uint64_t m_updateSequence = 0;
        std::function<std::string()> m_getClipboard;
        std::function<void(const std::string &)> m_setClipboard;
    };
}
