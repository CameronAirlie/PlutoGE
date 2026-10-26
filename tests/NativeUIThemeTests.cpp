#include "PlutoGE/scene/components/UIShapeComponent.h"
#include "PlutoGE/scene/ui/UIStyleCatalog.h"
#include "PlutoGE/scene/ui/UITheme.h"
#include "PlutoGE/scene/ui/UIThemeFormat.h"
#include "PlutoGE/scene/ui/UIThemeResolver.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    using namespace PlutoGE::scene;

    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    std::shared_ptr<const UITheme> Layer(std::string_view text)
    {
        std::vector<std::string> errors;
        auto theme = std::make_shared<UITheme>(ParseUITheme(text, &errors));
        Require(errors.empty(), "Layer did not parse: " + (errors.empty() ? std::string() : errors.front()));
        return theme;
    }

    std::string ValueOf(const UIResolvedStyle &style, UIStyleState state, std::string_view component, std::string_view property)
    {
        const auto *value = style.Find(state, component, property);
        return value ? value->value : "<unset>";
    }

    void TestModelEditing()
    {
        UITheme theme;
        theme.AddStyle("Button");
        theme.AddStyle("Primary Button", "Button");
        Require(theme.FindStyle("Primary-Button"), "Free-text names become valid names");
        Require(theme.AddStyle("Button").name == "Button-2", "Taken names get a suffix");
        Require(theme.RenameStyle("Button", "Base-Button"), "Rename must succeed");
        Require(theme.FindStyle("Primary-Button")->basedOn == "Base-Button", "Renaming must update styles based on it");
        Require(!theme.RenameStyle("Base-Button", "Button-2"), "Renaming onto a taken name must fail");
        Require(!theme.RenameStyle("Base-Button", "1bad"), "Invalid names must be rejected");

        theme.AddStyle("Danger", "Primary-Button");
        Require(theme.InheritsFrom("Danger", "Base-Button"), "Inheritance must be transitive");
        Require(theme.RemoveStyle("Primary-Button"), "Remove must succeed");
        Require(theme.FindStyle("Danger")->basedOn == "Base-Button", "Removing a style re-parents its children");

        auto *copy = theme.DuplicateStyle("Danger");
        Require(copy && copy->name == "Danger-Copy" && copy->basedOn == "Base-Button", "Duplicates keep their base");

        theme.AddVariable("accent", UIThemeVariableType::Color, "1,0,0,1");
        theme.AddVariable("accent-dark", UIThemeVariableType::Color, "@accent");
        theme.FindStyle("Danger")->Set(UIStyleState::Normal, "UIShapeComponent", "CornerRadii", "@accent,@accent-dark");
        Require(theme.RenameVariable("accent", "brand"), "Variable rename must succeed");
        Require(theme.FindVariable("accent-dark")->value == "@brand", "Variables referencing it must follow");
        Require(theme.FindStyle("Danger")->Find(UIStyleState::Normal, "UIShapeComponent", "CornerRadii")->value == "@brand,@accent-dark",
                "Only whole names are renamed");

        Require(InferUIThemeVariableType("1,0.5,0,1") == UIThemeVariableType::Color, "Four numbers read as a colour");
        Require(InferUIThemeVariableType("12") == UIThemeVariableType::Number, "One number reads as a number");
        Require(InferUIThemeVariableType("0,-8") == UIThemeVariableType::Vector, "Two numbers read as a vector");
        Require(InferUIThemeVariableType("Roboto") == UIThemeVariableType::Text, "Words read as text");
        Require(GetSingleUIThemeVariableReference(" @accent ") == "accent", "A lone reference is recognised");
        Require(!GetSingleUIThemeVariableReference("@a,@b"), "Mixed values are not a single reference");
    }

    void TestFormat()
    {
        const UITheme starter = MakeStarterUITheme();
        std::vector<std::string> errors;
        const auto reparsed = ParseUITheme(WriteUITheme(starter), &errors);
        Require(errors.empty(), "The starter theme must write valid text");
        Require(reparsed == starter, "Writing then parsing must round-trip");

        UITheme quoted;
        quoted.AddStyle("Label").Set(UIStyleState::Normal, "UITextComponent", "Text", "a; {b} # \"c\"\nline two");
        Require(ParseUITheme(WriteUITheme(quoted)) == quoted, "Values with special characters must round-trip");

        // Sheets written before themes keep working.
        errors.clear();
        const auto legacy = ParseUITheme(R"(
            # theme tokens
            @accent = 0.25,0.5,1,1
            .button { UIShapeComponent.Color = @accent; UIShapeComponent.BorderWidth = 2 }
            .button:hover { UIShapeComponent.Color = 1,1,1,1 }
            .button:active { UIShapeComponent.Color = 0,0,0,1 }
            .button.danger { UIShapeComponent.Color = 1,0,0,1 }
            .bogus:unknown { UIShapeComponent.Color = 1,1,1,1 }
            stray = 1
        )", &errors);
        Require(errors.size() == 3, "Combined selectors, unknown states and stray lines are reported, got " + std::to_string(errors.size()));
        Require(errors[0].rfind("Line 7", 0) == 0, "Errors must name their line: " + errors[0]);
        Require(legacy.styles.size() == 1, "Only the valid style is kept");
        const auto *button = legacy.FindStyle("button");
        Require(button->Find(UIStyleState::Normal, "UIShapeComponent", "Color")->value == "@accent", "References stay symbolic");
        Require(button->Find(UIStyleState::Pressed, "UIShapeComponent", "Color"), "'active' means pressed");
        Require(legacy.FindVariable("accent")->type == UIThemeVariableType::Color, "Untyped variables get an inferred type");

        errors.clear();
        const auto modern = ParseUITheme(R"(
            @import "project://UI/Base.plutotheme"
            @gap: number = 12
            .Card extends .Panel {
                UILayoutComponent.Gap = @gap,@gap
                :hover { UIShapeComponent.BorderWidth = 2 }
            }
            .Card:disabled { UIShapeComponent.Color = 0.5,0.5,0.5,1 }
        )", &errors);
        Require(errors.empty(), "Modern syntax must parse: " + (errors.empty() ? std::string() : errors.front()));
        Require(modern.baseThemes.size() == 1 && modern.baseThemes[0] == "project://UI/Base.plutotheme", "@import adds a base theme");
        const auto *card = modern.FindStyle("Card");
        Require(card->basedOn == "Panel", "extends sets the base style");
        Require(card->Find(UIStyleState::Hovered, "UIShapeComponent", "BorderWidth"), "Nested state blocks belong to their style");
        Require(card->Find(UIStyleState::Disabled, "UIShapeComponent", "Color"), "Standalone state blocks merge into the style");

        errors.clear();
        (void)ParseUITheme(".A { :hover { :pressed { } } }\n.B {", &errors);
        Require(errors.size() == 2, "Nested states inside states and unclosed blocks are reported");
    }

    void TestResolution()
    {
        const auto base = Layer(R"(
            @accent: color = 0,0,1,1
            @accent-hover: color = @accent
            .Button {
                UIShapeComponent.Color = @accent
                UIShapeComponent.BorderWidth = 1
                :hover { UIShapeComponent.Color = @accent-hover }
                :disabled { UIShapeComponent.Color = 0.5,0.5,0.5,1 }
            }
            .Primary extends .Button { UIShapeComponent.Color = 1,0,0,1 }
        )");
        auto resolved = ResolveUITheme({base});
        Require(resolved->GetErrors().empty(), "A valid theme resolves without errors");
        const auto *primary = resolved->FindStyle("Primary");
        Require(primary->chain == std::vector<std::string>{"Primary", "Button"}, "The chain lists the style and its bases");
        Require(ValueOf(*primary, UIStyleState::Normal, "UIShapeComponent", "Color") == "1,0,0,1", "A style's own value wins");
        Require(ValueOf(*primary, UIStyleState::Normal, "UIShapeComponent", "BorderWidth") == "1", "Unset values are inherited");
        Require(ValueOf(*primary, UIStyleState::Hovered, "UIShapeComponent", "Color") == "0,0,1,1", "Variables resolve through variables");
        Require(primary->Find(UIStyleState::Normal, "UIShapeComponent", "BorderWidth")->sourceStyle == "Button", "Sources are recorded");

        std::vector<const UIResolvedProperty *> composed;
        primary->Compose(ToMask(UIStyleState::Hovered), composed);
        Require(composed.size() == 2 && composed[0]->value == "0,0,1,1", "An inherited state value beats a nearer Normal value");
        composed.clear();
        primary->Compose(ToMask(UIStyleState::Hovered) | ToMask(UIStyleState::Disabled), composed);
        Require(composed[0]->value == "0.5,0.5,0.5,1", "Disabled replaces Hover");

        // A later layer overrides variables and extends styles by name.
        const auto dark = Layer(R"(
            @accent = 0,1,0,1
            .Button { UIShapeComponent.BorderWidth = 3 }
            .Ghost extends .Button { }
        )");
        resolved = ResolveUITheme({base, dark});
        Require(ValueOf(*resolved->FindStyle("Button"), UIStyleState::Normal, "UIShapeComponent", "Color") == "0,1,0,1",
                "Overriding a variable re-themes the styles using it");
        Require(ValueOf(*resolved->FindStyle("Button"), UIStyleState::Normal, "UIShapeComponent", "BorderWidth") == "3",
                "A later layer's values win");
        Require(resolved->FindStyle("Button")->Find(UIStyleState::Hovered, "UIShapeComponent", "Color"),
                "Merging keeps the earlier layer's other values");
        Require(resolved->FindStyle("Ghost")->chain.size() == 2, "Later layers can base styles on earlier ones");

        const auto broken = Layer(R"(
            @loop = @loop
            .A extends .B { UIShapeComponent.Color = @loop }
            .B extends .A { }
            .C extends .Missing { UIShapeComponent.Color = @nothing }
            .D { NoSuchComponent.Value = 1 }
        )");
        const auto brokenResolved = ResolveUITheme({broken});
        const auto &errors = brokenResolved->GetErrors();
        const auto mentions = [&](std::string_view text)
        { return std::any_of(errors.begin(), errors.end(), [&](const std::string &error) { return error.find(text) != std::string::npos; }); };
        Require(mentions("refers to itself"), "Variable cycles are reported");
        Require(mentions("based on itself"), "Inheritance cycles are reported");
        Require(mentions("missing style 'Missing'"), "Missing bases are reported");
        Require(mentions("unknown variable @nothing"), "Unknown variables are reported");
        Require(mentions("unknown component 'NoSuchComponent'"), "Unknown components are reported");
    }

    void TestCatalog()
    {
        const auto &catalog = UIStyleCatalog::Get();
        const auto *color = catalog.FindProperty("UIShapeComponent", "Color");
        Require(color && color->type == PropertyType::Color, "Shape colour must be styleable as a colour");
        Require(!catalog.FindProperty("UIShapeComponent", "Enabled"), "Enabled is not a style property");
        Require(!catalog.FindComponent("UIStyleComponent"), "Style components are not styleable");
        Require(catalog.GetComponentLabel("UIShapeComponent") == "Shape", "Labels drop the UI prefix and Component suffix");
        Require(!UIStyleCatalog::IsTypicallyStyled("UITextComponent", "Text"), "Text content is not proposed for styles");

        UIShapeComponent shape;
        shape.SetColor(glm::vec4(0.2f, 0.3f, 0.4f, 1.0f));
        const auto changed = catalog.GetChangedProperties(shape);
        // A solid fill's end colour follows its colour; nothing else changed.
        Require(!changed.empty() && changed[0].name == "Color" &&
                    std::all_of(changed.begin(), changed.end(), [](const Property &property) { return property.name == "Color" || property.name == "EndColor"; }),
                "Only properties that differ from defaults are proposed");
    }
}

int main()
{
    try
    {
        TestModelEditing();
        TestFormat();
        TestResolution();
        TestCatalog();
    }
    catch (const std::exception &error)
    {
        std::cerr << "NativeUIThemeTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "NativeUIThemeTests passed\n";
    return 0;
}
