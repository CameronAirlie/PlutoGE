#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>

#include <string>
#include <utility>
#include <vector>

namespace PlutoGE::scene
{
    enum class CanvasRenderMode
    {
        ScreenSpaceOverlay = 0,
    };

    enum class UIAnchorPreset
    {
        TopLeft = 0,
        TopCenter,
        TopRight,
        MiddleLeft,
        MiddleCenter,
        MiddleRight,
        BottomLeft,
        BottomCenter,
        BottomRight,
        Stretch,
    };

    class CanvasComponent : public TypedComponent<CanvasComponent>
    {
    public:
        void Update(float deltaTime) override {}

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        CanvasRenderMode GetRenderMode() const { return m_renderMode; }
        void SetRenderMode(CanvasRenderMode renderMode) { m_renderMode = renderMode; }
        float GetScaleFactor() const { return m_scaleFactor; }
        void SetScaleFactor(float scaleFactor) { m_scaleFactor = scaleFactor; }
        int GetSortingOrder() const { return m_sortingOrder; }
        void SetSortingOrder(int sortingOrder) { m_sortingOrder = sortingOrder; }

    private:
        CanvasRenderMode m_renderMode = CanvasRenderMode::ScreenSpaceOverlay;
        float m_scaleFactor = 1.0f;
        int m_sortingOrder = 0;
    };

    class RectTransformComponent : public TypedComponent<RectTransformComponent>
    {
    public:
        void Update(float deltaTime) override {}

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        glm::vec2 GetAnchoredPosition() const { return m_anchoredPosition; }
        void SetAnchoredPosition(const glm::vec2 &anchoredPosition) { m_anchoredPosition = anchoredPosition; }
        glm::vec2 GetSizeDelta() const { return m_sizeDelta; }
        void SetSizeDelta(const glm::vec2 &sizeDelta) { m_sizeDelta = sizeDelta; }
        glm::vec2 GetPivot() const { return m_pivot; }
        void SetPivot(const glm::vec2 &pivot) { m_pivot = pivot; }
        glm::vec2 GetAnchorMin() const { return m_anchorMin; }
        void SetAnchorMin(const glm::vec2 &anchorMin) { m_anchorMin = anchorMin; }
        glm::vec2 GetAnchorMax() const { return m_anchorMax; }
        void SetAnchorMax(const glm::vec2 &anchorMax) { m_anchorMax = anchorMax; }
        UIAnchorPreset GetAnchorPreset() const { return m_anchorPreset; }
        void SetAnchorPreset(UIAnchorPreset preset);

    private:
        glm::vec2 m_anchoredPosition{0.0f};
        glm::vec2 m_sizeDelta{100.0f, 40.0f};
        glm::vec2 m_pivot{0.5f, 0.5f};
        glm::vec2 m_anchorMin{0.5f, 0.5f};
        glm::vec2 m_anchorMax{0.5f, 0.5f};
        UIAnchorPreset m_anchorPreset = UIAnchorPreset::MiddleCenter;
    };

    class UIImageComponent : public TypedComponent<UIImageComponent>
    {
    public:
        void Update(float deltaTime) override {}

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        glm::vec4 GetColor() const { return m_color; }
        void SetColor(const glm::vec4 &color) { m_color = color; }
        const std::string &GetTexturePath() const { return m_texturePath; }
        void SetTexturePath(std::string texturePath) { m_texturePath = std::move(texturePath); }

    private:
        glm::vec4 m_color{1.0f};
        std::string m_texturePath;
    };

    class UITextComponent : public TypedComponent<UITextComponent>
    {
    public:
        void Update(float deltaTime) override {}

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        const std::string &GetText() const { return m_text; }
        void SetText(std::string text) { m_text = std::move(text); }
        glm::vec4 GetColor() const { return m_color; }
        void SetColor(const glm::vec4 &color) { m_color = color; }
        float GetFontSize() const { return m_fontSize; }
        void SetFontSize(float fontSize) { m_fontSize = fontSize; }
        const std::string &GetFontPath() const { return m_fontPath; }
        void SetFontPath(std::string fontPath) { m_fontPath = std::move(fontPath); }

    private:
        std::string m_text;
        glm::vec4 m_color{1.0f};
        float m_fontSize = 18.0f;
        std::string m_fontPath;
    };

    class UIButtonComponent : public TypedComponent<UIButtonComponent>
    {
    public:
        void Update(float deltaTime) override {}

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        bool IsInteractable() const { return m_interactable; }
        void SetInteractable(bool interactable) { m_interactable = interactable; }
        bool IsHovered() const { return m_hovered; }
        bool WasPressed() const { return m_pressed; }
        bool WasReleased() const { return m_released; }
        bool WasClicked() const { return m_clicked; }
        void SetRuntimeState(bool hovered, bool pressed, bool released, bool clicked);

    private:
        bool m_interactable = true;
        bool m_hovered = false;
        bool m_pressed = false;
        bool m_released = false;
        bool m_clicked = false;
    };
}
