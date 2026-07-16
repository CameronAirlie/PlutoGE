namespace PlutoGE.ScriptCore;

public readonly record struct AnimationEvent(
    string Name,
    string StringParameter,
    float FloatParameter,
    int IntParameter);
