#include "PlutoGE/ui/hierarchy/UIWidgetPresets.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/UIDropdownComponent.h"
#include "PlutoGE/scene/components/UIInputFieldComponent.h"
#include "PlutoGE/scene/components/UILayoutComponents.h"
#include "PlutoGE/scene/components/UIScrollViewComponent.h"
#include "PlutoGE/scene/components/UIShapeComponent.h"
#include "PlutoGE/scene/components/UISliderComponent.h"
#include "PlutoGE/scene/components/UIToggleComponent.h"

#include <array>
#include <memory>
#include <string>

namespace PlutoGE::ui
{
    namespace
    {
        // A small consistent default theme for new widgets.
        constexpr glm::vec4 kSurface{0.13f, 0.14f, 0.17f, 0.96f};
        constexpr glm::vec4 kControl{0.20f, 0.22f, 0.27f, 1.0f};
        constexpr glm::vec4 kAccent{0.26f, 0.52f, 0.96f, 1.0f};
        constexpr glm::vec4 kBorder{0.34f, 0.37f, 0.44f, 1.0f};
        constexpr glm::vec4 kText{0.94f, 0.95f, 0.97f, 1.0f};
        constexpr glm::vec4 kMuted{0.58f, 0.61f, 0.68f, 1.0f};

        scene::Entity *Add(scene::Scene &scene, scene::Entity *parent, std::string name)
        {
            return scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = std::move(name)}), parent);
        }

        scene::RectTransformComponent *Rect(scene::Entity *entity, glm::vec2 size,
                                            scene::UIAnchorPreset preset = scene::UIAnchorPreset::MiddleCenter)
        {
            auto *rect = entity->CreateComponent<scene::RectTransformComponent>();
            rect->SetAnchorPreset(preset);
            rect->SetSizeDelta(size);
            return rect;
        }

        // Fills the parent, inset by `inset` on every side.
        scene::RectTransformComponent *Stretch(scene::Entity *entity, float inset = 0.0f)
        {
            auto *rect = Rect(entity, glm::vec2(-2.0f * inset), scene::UIAnchorPreset::Stretch);
            return rect;
        }

        scene::UIShapeComponent *Shape(scene::Entity *entity, glm::vec4 color, float radius, float border = 0.0f,
                                       glm::vec4 borderColor = kBorder)
        {
            auto *shape = entity->CreateComponent<scene::UIShapeComponent>();
            shape->SetColor(color);
            shape->SetCornerRadii(CornerRadii::All(radius));
            shape->SetBorderWidth(border);
            shape->SetBorderColor(borderColor);
            return shape;
        }

        scene::UITextComponent *Text(scene::Entity *entity, std::string value, float size,
                                     scene::UITextAlignment alignment = scene::UITextAlignment::MiddleCenter,
                                     glm::vec4 color = kText)
        {
            auto *text = entity->CreateComponent<scene::UITextComponent>();
            text->SetText(std::move(value));
            text->SetFontSize(size);
            text->SetAlignment(alignment);
            text->SetColor(color);
            text->SetWrap(false);
            return text;
        }

        bool InsideCanvas(const scene::Entity *entity)
        {
            for (; entity; entity = entity->GetParent())
                if (entity->GetComponent<scene::CanvasComponent>())
                    return true;
            return false;
        }

        scene::Entity *CreateCanvasEntity(scene::Scene &scene, scene::Entity *parent)
        {
            auto *canvas = Add(scene, parent, "Canvas");
            auto *component = canvas->CreateComponent<scene::CanvasComponent>();
            component->SetBackend(scene::UIRenderBackend::Native);
            component->SetScaleMode(scene::CanvasScaleMode::ScaleWithScreenSize);
            component->SetReferenceResolution({1920.0f, 1080.0f});
            Stretch(canvas)->SetRaycastTarget(false);
            return canvas;
        }

        // The parent a widget goes under: the context parent when inside a
        // canvas, otherwise a new canvas.
        scene::Entity *WidgetParent(const EntityPresetContext &context)
        {
            return InsideCanvas(context.parent) ? context.parent : CreateCanvasEntity(context.scene, context.parent);
        }

        scene::Entity *CreateCanvas(const EntityPresetContext &context)
        {
            return CreateCanvasEntity(context.scene, context.parent);
        }

        scene::Entity *CreatePanel(const EntityPresetContext &context)
        {
            auto *panel = Add(context.scene, WidgetParent(context), "Panel");
            Rect(panel, {480.0f, 320.0f});
            auto *shape = Shape(panel, kSurface, 14.0f);
            shape->SetShadowEnabled(true);
            shape->SetShadow({.color = {0.0f, 0.0f, 0.0f, 0.45f}, .offset = {0.0f, -6.0f}, .blur = 24.0f});
            return panel;
        }

        scene::Entity *CreateText(const EntityPresetContext &context)
        {
            auto *entity = Add(context.scene, WidgetParent(context), "Text");
            Rect(entity, {240.0f, 48.0f});
            Text(entity, "New Text", 28.0f);
            entity->CreateComponent<scene::UILayoutItemComponent>()->SetHeightMode(SizeMode::Content);
            return entity;
        }

        scene::Entity *CreateImage(const EntityPresetContext &context)
        {
            auto *entity = Add(context.scene, WidgetParent(context), "Image");
            Rect(entity, {128.0f, 128.0f});
            entity->CreateComponent<scene::UIImageComponent>();
            return entity;
        }

        scene::Entity *CreateButton(const EntityPresetContext &context)
        {
            auto *button = Add(context.scene, WidgetParent(context), "Button");
            Rect(button, {220.0f, 56.0f});
            Shape(button, kAccent, 10.0f);
            button->CreateComponent<scene::UIButtonComponent>();
            auto *label = Add(context.scene, button, "Label");
            Stretch(label);
            Text(label, "Button", 24.0f);
            return button;
        }

        scene::Entity *CreateToggle(const EntityPresetContext &context)
        {
            auto *toggle = Add(context.scene, WidgetParent(context), "Toggle");
            Rect(toggle, {220.0f, 36.0f});
            auto *component = toggle->CreateComponent<scene::UIToggleComponent>();
            component->SetIsOn(true);

            auto *box = Add(context.scene, toggle, "Box");
            auto *boxRect = Rect(box, {32.0f, 32.0f}, scene::UIAnchorPreset::MiddleLeft);
            boxRect->SetAnchoredPosition({2.0f, 0.0f});
            Shape(box, kControl, 7.0f, 2.0f);
            auto *check = Add(context.scene, box, "Checkmark");
            Stretch(check, 7.0f);
            Shape(check, kAccent, 3.0f);
            component->SetGraphic(check->GetID());

            auto *label = Add(context.scene, toggle, "Label");
            auto *labelRect = Stretch(label);
            labelRect->SetMargin({44.0f, 0.0f, 0.0f, 0.0f});
            Text(label, "Toggle", 22.0f, scene::UITextAlignment::MiddleLeft);
            return toggle;
        }

        scene::Entity *CreateSlider(const EntityPresetContext &context)
        {
            auto *slider = Add(context.scene, WidgetParent(context), "Slider");
            Rect(slider, {280.0f, 10.0f});
            Shape(slider, kControl, 5.0f);
            auto *component = slider->CreateComponent<scene::UISliderComponent>();
            component->SetValue(0.5f);

            auto *fill = Add(context.scene, slider, "Fill");
            Stretch(fill);
            Shape(fill, kAccent, 5.0f);
            auto *handle = Add(context.scene, slider, "Handle");
            Rect(handle, {26.0f, 16.0f});
            Shape(handle, kText, 13.0f);
            component->SetFill(fill->GetID());
            component->SetHandle(handle->GetID());
            return slider;
        }

        scene::Entity *CreateScrollView(const EntityPresetContext &context)
        {
            auto *view = Add(context.scene, WidgetParent(context), "Scroll View");
            Rect(view, {360.0f, 260.0f})->SetClipChildren(true);
            Shape(view, kSurface, 12.0f);
            auto *scroll = view->CreateComponent<scene::UIScrollViewComponent>();

            auto *content = Add(context.scene, view, "Content");
            auto *contentRect = Rect(content, {0.0f, 0.0f});
            contentRect->SetAnchorMin({0.0f, 1.0f});
            contentRect->SetAnchorMax({1.0f, 1.0f});
            contentRect->SetPivot({0.5f, 1.0f});
            auto *layout = content->CreateComponent<scene::UILayoutComponent>();
            layout->SetDirection(FlexDirection::Column);
            layout->SetAlignItems(AlignItems::Stretch);
            layout->SetGap({0.0f, 8.0f});
            layout->SetPadding(Edges::All(12.0f));
            content->CreateComponent<scene::UILayoutItemComponent>()->SetHeightMode(SizeMode::Content);
            scroll->SetContent(content->GetID());

            for (int index = 1; index <= 8; ++index)
            {
                auto *item = Add(context.scene, content, "Item " + std::to_string(index));
                Rect(item, {0.0f, 44.0f});
                Shape(item, kControl, 8.0f);
                auto *label = Add(context.scene, item, "Label");
                Stretch(label);
                Text(label, "Item " + std::to_string(index), 20.0f, scene::UITextAlignment::MiddleLeft)
                    ->SetPadding({14.0f, 0.0f, 14.0f, 0.0f});
            }
            return view;
        }

        scene::Entity *CreateInputField(const EntityPresetContext &context)
        {
            auto *field = Add(context.scene, WidgetParent(context), "Input Field");
            Rect(field, {300.0f, 48.0f});
            Shape(field, kControl, 8.0f, 2.0f);
            Text(field, "", 22.0f, scene::UITextAlignment::MiddleLeft)->SetPadding({14.0f, 0.0f, 14.0f, 0.0f});
            auto *input = field->CreateComponent<scene::UIInputFieldComponent>();

            auto *placeholder = Add(context.scene, field, "Placeholder");
            Stretch(placeholder);
            Text(placeholder, "Enter text...", 22.0f, scene::UITextAlignment::MiddleLeft, kMuted)
                ->SetPadding({14.0f, 0.0f, 14.0f, 0.0f});
            placeholder->GetComponent<scene::RectTransformComponent>()->SetRaycastTarget(false);
            input->SetPlaceholder(placeholder->GetID());
            return field;
        }

        scene::Entity *CreateDropdown(const EntityPresetContext &context)
        {
            auto *dropdown = Add(context.scene, WidgetParent(context), "Dropdown");
            Rect(dropdown, {280.0f, 48.0f});
            Shape(dropdown, kControl, 8.0f, 2.0f);
            Text(dropdown, "", 22.0f, scene::UITextAlignment::MiddleLeft)->SetPadding({14.0f, 0.0f, 40.0f, 0.0f});
            dropdown->CreateComponent<scene::UIDropdownComponent>()->SetOptions({"Option A", "Option B", "Option C"});
            auto *arrow = Add(context.scene, dropdown, "Arrow");
            auto *arrowRect = Rect(arrow, {28.0f, 28.0f}, scene::UIAnchorPreset::MiddleRight);
            arrowRect->SetAnchoredPosition({-8.0f, 0.0f});
            arrowRect->SetRaycastTarget(false);
            Text(arrow, "v", 20.0f, scene::UITextAlignment::MiddleCenter, kMuted);
            return dropdown;
        }

        scene::Entity *CreateLayout(const EntityPresetContext &context, const char *name, LayoutMode mode,
                                    FlexDirection direction)
        {
            auto *group = Add(context.scene, WidgetParent(context), name);
            Rect(group, {480.0f, 240.0f});
            auto *layout = group->CreateComponent<scene::UILayoutComponent>();
            layout->SetMode(mode);
            layout->SetDirection(direction);
            layout->SetGap({12.0f, 12.0f});
            layout->SetPadding(Edges::All(12.0f));
            if (mode == LayoutMode::Grid)
            {
                layout->SetColumns({GridTrack::Fraction(), GridTrack::Fraction(), GridTrack::Fraction()});
                layout->SetJustifyItems(AlignItems::Stretch);
                layout->SetAlignItems(AlignItems::Stretch);
            }
            return group;
        }

        scene::Entity *CreateRow(const EntityPresetContext &context)
        {
            return CreateLayout(context, "Row", LayoutMode::Flex, FlexDirection::Row);
        }

        scene::Entity *CreateColumn(const EntityPresetContext &context)
        {
            return CreateLayout(context, "Column", LayoutMode::Flex, FlexDirection::Column);
        }

        scene::Entity *CreateGrid(const EntityPresetContext &context)
        {
            return CreateLayout(context, "Grid", LayoutMode::Grid, FlexDirection::Row);
        }

        constexpr std::array kPresets{
            EntityPresetDefinition{"Canvas", "Create UI Canvas", false, &CreateCanvas},
            EntityPresetDefinition{"Panel", "Create UI Panel", true, &CreatePanel},
            EntityPresetDefinition{"Text", "Create UI Text", false, &CreateText},
            EntityPresetDefinition{"Image", "Create UI Image", false, &CreateImage},
            EntityPresetDefinition{"Button", "Create UI Button", true, &CreateButton},
            EntityPresetDefinition{"Toggle", "Create UI Toggle", false, &CreateToggle},
            EntityPresetDefinition{"Slider", "Create UI Slider", false, &CreateSlider},
            EntityPresetDefinition{"Input Field", "Create UI Input Field", false, &CreateInputField},
            EntityPresetDefinition{"Dropdown", "Create UI Dropdown", false, &CreateDropdown},
            EntityPresetDefinition{"Scroll View", "Create UI Scroll View", false, &CreateScrollView},
            EntityPresetDefinition{"Row", "Create UI Row", true, &CreateRow},
            EntityPresetDefinition{"Column", "Create UI Column", false, &CreateColumn},
            EntityPresetDefinition{"Grid", "Create UI Grid", false, &CreateGrid},
        };
    }

    std::span<const EntityPresetDefinition> GetUIWidgetPresets()
    {
        return kPresets;
    }
}
