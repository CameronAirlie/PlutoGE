namespace PlutoGE.ScriptCore;

/// <summary>Handles local dedicated-server console commands on the simulation thread.
/// Return false for unrecognized commands. Use Debug.Log for command output.</summary>
public interface IServerCommandHandler
{
    bool HandleServerCommand(string command);
}
