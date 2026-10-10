namespace PlutoGE.ScriptCore.Authoring;

/// <summary>
/// A project-defined editor command discovered in the project's script assembly.
/// Execute is called once from Authoring / Project Commands with the selected entity
/// as GameObject. Scene changes are grouped into one editor undo operation.
/// No update loop is started. Do not retain entity references or start background scene edits.
/// </summary>
public abstract class EditorCommand : ScriptBehaviour
{
    public sealed override void OnCreate() => Execute();
    protected abstract void Execute();
}
