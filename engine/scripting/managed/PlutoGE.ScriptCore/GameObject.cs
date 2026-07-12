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
    public string Name => ScriptBridge.GetEntityName(EntityId);

    public Vector3 Position
    {
        get => ScriptBridge.GetEntityPosition(EntityId);
        set => ScriptBridge.SetEntityPosition(EntityId, value);
    }

    public Vector3 WorldPosition
    {
        get => ScriptBridge.GetEntityWorldPosition(EntityId);
        set => ScriptBridge.SetEntityWorldPosition(EntityId, value);
    }

    public Vector3 Rotation
    {
        get => ScriptBridge.GetEntityRotation(EntityId);
        set => ScriptBridge.SetEntityRotation(EntityId, value);
    }

    public Vector3 WorldRotation
    {
        get => ScriptBridge.GetEntityWorldRotation(EntityId);
        set => ScriptBridge.SetEntityWorldRotation(EntityId, value);
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

    public string[] Tags => ScriptBridge.GetEntityTags(EntityId);

    public bool HasTag(string tag)
    {
        return ScriptBridge.HasEntityTag(EntityId, tag);
    }

    public bool TryInvoke(string methodName, params object?[] args)
    {
        return ScriptBridge.InvokeEntityMethod(EntityId, methodName, args);
    }

    public bool Destroy()
    {
        return ScriptBridge.DestroyEntity(EntityId);
    }

    public static bool Destroy(GameObject? gameObject)
    {
        return gameObject is not null && ScriptBridge.DestroyEntity(gameObject.EntityId);
    }

    public static GameObject? Find(string name)
    {
        var entityId = ScriptBridge.FindEntityByName(name);
        return entityId == 0 ? null : new GameObject(entityId);
    }

    public static GameObject[] FindByTag(string tag)
    {
        var entityIds = ScriptBridge.FindEntitiesByTag(tag);
        var gameObjects = new GameObject[entityIds.Length];
        for (var index = 0; index < entityIds.Length; ++index)
        {
            gameObjects[index] = new GameObject(entityIds[index]);
        }
        return gameObjects;
    }

    public static GameObject? FindWithTag(string tag)
    {
        var entityIds = ScriptBridge.FindEntitiesByTag(tag);
        return entityIds.Length == 0 ? null : new GameObject(entityIds[0]);
    }

    public static GameObject[] FindGameObjectsWithTag(string tag)
    {
        return FindByTag(tag);
    }

    public bool HasComponent<T>() where T : class
    {
        return typeof(T) switch
        {
            var type when type == typeof(MeshComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Mesh),
            var type when type == typeof(CameraComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Camera),
            var type when type == typeof(LightComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Light),
            var type when type == typeof(RigidbodyComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Rigidbody),
            var type when type == typeof(ColliderComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Collider),
            var type when type == typeof(AnimationComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Animation),
            var type when type == typeof(CanvasComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Canvas),
            var type when type == typeof(RectTransformComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.RectTransform),
            var type when type == typeof(UIImageComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.UIImage),
            var type when type == typeof(UITextComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.UIText),
            var type when type == typeof(UIButtonComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.UIButton),
            var type when type == typeof(ParticleSystemComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.ParticleSystem),
            var type when type == typeof(SoundEmitterComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.SoundEmitter),
            _ => ScriptBridge.GetScript<T>(EntityId) is not null,
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

        if (typeof(T) == typeof(RigidbodyComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Rigidbody))
        {
            return new RigidbodyComponent(EntityId) as T;
        }

        if (typeof(T) == typeof(ColliderComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Collider))
        {
            return new ColliderComponent(EntityId) as T;
        }

        if (typeof(T) == typeof(AnimationComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Animation))
        {
            return new AnimationComponent(EntityId) as T;
        }

        if (typeof(T) == typeof(CanvasComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Canvas))
        {
            return new CanvasComponent(EntityId) as T;
        }

        if (typeof(T) == typeof(RectTransformComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.RectTransform))
        {
            return new RectTransformComponent(EntityId) as T;
        }

        if (typeof(T) == typeof(UIImageComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.UIImage))
        {
            return new UIImageComponent(EntityId) as T;
        }

        if (typeof(T) == typeof(UITextComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.UIText))
        {
            return new UITextComponent(EntityId) as T;
        }

        if (typeof(T) == typeof(UIButtonComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.UIButton))
        {
            return new UIButtonComponent(EntityId) as T;
        }

        if (typeof(T) == typeof(ParticleSystemComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.ParticleSystem))
        {
            return new ParticleSystemComponent(EntityId) as T;
        }

        if (typeof(T) == typeof(SoundEmitterComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.SoundEmitter))
        {
            return new SoundEmitterComponent(EntityId) as T;
        }

        return ScriptBridge.GetScript<T>(EntityId);
    }

    public override string ToString()
    {
        return $"GameObject({EntityId})";
    }
}
