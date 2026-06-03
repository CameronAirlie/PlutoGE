using System;
using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public sealed class GameObject
{
    internal GameObject(uint entityId)
    {
        EntityId = entityId;
    }

    public uint EntityId { get; }
    public bool IsValid => EntityId != 0;

    public Vector3 Position
    {
        get => ScriptBridge.GetEntityPosition(EntityId);
        set => ScriptBridge.SetEntityPosition(EntityId, value);
    }

    public Vector3 Rotation
    {
        get => ScriptBridge.GetEntityRotation(EntityId);
        set => ScriptBridge.SetEntityRotation(EntityId, value);
    }

    public Vector3 Scale
    {
        get => ScriptBridge.GetEntityScale(EntityId);
        set => ScriptBridge.SetEntityScale(EntityId, value);
    }

    public Vector3 Forward => ScriptBridge.GetEntityForward(EntityId);

    public Vector3 Right => ScriptBridge.GetEntityRight(EntityId);

    public bool Active
    {
        get => ScriptBridge.GetEntityActive(EntityId);
        set => ScriptBridge.SetEntityActive(EntityId, value);
    }

    public bool HasComponent<T>() where T : class
    {
        return typeof(T) switch
        {
            var type when type == typeof(MeshComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Mesh),
            var type when type == typeof(CameraComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Camera),
            var type when type == typeof(LightComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Light),
            _ => false,
        };
    }

    public T? GetComponent<T>() where T : class
    {
        if (typeof(T) == typeof(MeshComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Mesh))
        {
            return new MeshComponent(EntityId) as T;
        }

        if (typeof(T) == typeof(CameraComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Camera))
        {
            return new CameraComponent(EntityId) as T;
        }

        if (typeof(T) == typeof(LightComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Light))
        {
            return new LightComponent(EntityId) as T;
        }

        return null;
    }

    public override string ToString()
    {
        return $"GameObject({EntityId})";
    }
}
