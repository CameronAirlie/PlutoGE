#pragma once
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>

namespace PlutoGE::render {
// Opt-in viewport: data-pan-zoom names a content element inside a single spacer.
// Uses RML projection so interaction remains correct under canvas/UI scaling.
class RmlPanZoom final : public Rml::EventListener {
    Rml::ObserverPtr<Rml::Element> dragging;
    Rml::Vector2f previous;
    static Rml::Element* Viewport(Rml::Element* target) {
        for(auto* e=target;e;e=e->GetParentNode()) if(e->HasAttribute("data-pan-zoom")) return e;
        return nullptr;
    }
    static bool Point(Rml::Element* viewport, Rml::Event& event, Rml::Vector2f& point) {
        point={event.GetParameter<float>("mouse_x",0),event.GetParameter<float>("mouse_y",0)};
        return viewport->Project(point);
    }
public:
    // RmlUi's built-in ScrollIntoView uses untransformed layout boxes. Reveal
    // focused children in the zoomed coordinate space for keyboard/controllers.
    static bool Reveal(Rml::Element* element) {
        auto* viewport=Viewport(element->GetParentNode());
        if(!viewport) return false;
        auto* content=viewport->GetOwnerDocument()->GetElementById(viewport->GetAttribute<Rml::String>("data-pan-zoom",""));
        if(!content) return false;
        viewport->GetContext()->Update();
        const float zoom=viewport->GetAttribute<float>("data-zoom",1.f);
        const auto offset=(element->GetAbsoluteOffset(Rml::BoxArea::Border)-content->GetAbsoluteOffset(Rml::BoxArea::Border))*zoom;
        const auto size=element->GetBox().GetSize(Rml::BoxArea::Border)*zoom;
        auto reveal=[](float scroll,float start,float length,float client) {
            return start<scroll ? start : start+length>scroll+client ? start+length-client : scroll;
        };
        viewport->SetScrollLeft(reveal(viewport->GetScrollLeft(),offset.x,size.x,viewport->GetClientWidth()));
        viewport->SetScrollTop(reveal(viewport->GetScrollTop(),offset.y,size.y,viewport->GetClientHeight()));
        return true;
    }
    void ProcessEvent(Rml::Event& event) override {
        const auto type=event.GetType();
        if(type=="mouseup" && event.GetParameter<int>("button",-1)==1) { dragging={}; return; }
        auto* viewport=Viewport(event.GetTargetElement());
        if(type=="mousedown" && viewport && event.GetParameter<int>("button",-1)==1) {
            if(Point(viewport,event,previous)) dragging=viewport->GetObserverPtr();
            event.StopPropagation(); return;
        }
        if(type=="mousemove" && dragging) {
            viewport=dragging.get();
            if(!viewport->IsVisible(true)) { dragging={}; return; }
            Rml::Vector2f point;
            if(Point(viewport,event,point)) {
                viewport->SetScrollLeft(viewport->GetScrollLeft()-(point.x-previous.x));
                viewport->SetScrollTop(viewport->GetScrollTop()-(point.y-previous.y)); previous=point;
            }
            event.StopPropagation(); return;
        }
        if(type!="mousescroll" || !viewport) return;
        auto* content=viewport->GetOwnerDocument()->GetElementById(viewport->GetAttribute<Rml::String>("data-pan-zoom",""));
        if(!content || !content->GetParentNode()) return;
        float oldZoom=viewport->GetAttribute<float>("data-zoom",1.f);
        if(!std::isfinite(oldZoom) || oldZoom<=0) oldZoom=1.f;
        float zoom=std::clamp(oldZoom*std::pow(1.12f,-event.GetParameter<float>("wheel_delta_y",0)),.4f,1.8f);
        Rml::Vector2f point;
        if(!Point(viewport,event,point)) return;
        point-=viewport->GetAbsoluteOffset(Rml::BoxArea::Content);
        const Rml::Vector2f scroll{viewport->GetScrollLeft(),viewport->GetScrollTop()};
        const auto size=content->GetBox().GetSize(Rml::BoxArea::Border);
        content->SetProperty("transform-origin","0px 0px");
        content->SetProperty("transform","scale("+std::to_string(zoom)+")");
        auto* spacer=content->GetParentNode();
        spacer->SetProperty("width",std::to_string(size.x*zoom)+"px");
        spacer->SetProperty("height",std::to_string(size.y*zoom)+"px");
        viewport->SetAttribute("data-zoom",zoom);
        viewport->GetContext()->Update();
        viewport->SetScrollLeft((scroll.x+point.x)*zoom/oldZoom-point.x);
        viewport->SetScrollTop((scroll.y+point.y)*zoom/oldZoom-point.y);
        event.StopPropagation(); // Consume the wheel; do not also scroll an ancestor.
    }
    void Attach(Rml::Context& context) {
        for(const char* event:{"mousedown","mouseup","mousemove","mousescroll"}) context.AddEventListener(event,this,true);
    }
};
}
