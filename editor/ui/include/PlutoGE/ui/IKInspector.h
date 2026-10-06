#pragma once
namespace PlutoGE::scene { class IKComponent; class SkeletonAttachmentComponent; }
namespace PlutoGE::ui
{
    class EditorShell;
    void RenderIKInspectorTools(scene::IKComponent &rig, EditorShell &shell);
    void RenderAttachmentInspectorTools(scene::SkeletonAttachmentComponent &attachment);
}
