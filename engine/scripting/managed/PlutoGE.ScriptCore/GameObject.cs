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

    /// <summary>Local rotation without managed-side Euler conversion.</summary>
    public Quaternion RotationQuaternion
    {
        get => ScriptBridge.GetEntityRotationQuaternion(EntityId);
        set => ScriptBridge.SetEntityRotationQuaternion(EntityId, Quaternion.Normalize(value));
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

    /// <summary>Adds a tag. Returns false if it was already present or the object no longer exists.</summary>
    public bool AddTag(string tag) => ScriptBridge.AddEntityTag(EntityId, tag);

    /// <summary>Removes a tag. Returns false if it was not present.</summary>
    public bool RemoveTag(string tag) => ScriptBridge.RemoveEntityTag(EntityId, tag);

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

    /// <summary>
    /// Every entity in the active scene with a <typeparamref name="T"/> component, in hierarchy order.
    /// T may be a native component wrapper or a ScriptBehaviour type. Inactive entities and
    /// disabled components are skipped unless includeInactive is true. This walks the whole
    /// scene: call it on load or on change, not every frame.
    /// </summary>
    public static GameObject[] FindWithComponent<T>(bool includeInactive = false) where T : class
    {
        var ids = TryNativeComponentType(typeof(T), out var nativeType)
            ? ScriptBridge.FindEntitiesWithComponent(nativeType, includeInactive)
            : ScriptBridge.FindEntitiesWithComponent(ScriptBridge.NativeComponentType.Script, includeInactive)
                .Where(id => ScriptBridge.GetScript<T>(id) is not null).ToArray();
        var result = new GameObject[ids.Length];
        for (var index = 0; index < ids.Length; ++index) result[index] = new GameObject(ids[index]);
        return result;
    }

    /// <summary>The <typeparamref name="T"/> component of every matching entity; see FindWithComponent.</summary>
    public static T[] FindComponents<T>(bool includeInactive = false) where T : class
    {
        var objects = FindWithComponent<T>(includeInactive);
        var components = new List<T>(objects.Length);
        foreach (var gameObject in objects)
            if (gameObject.GetComponent<T>() is { } component) components.Add(component);
        return components.ToArray();
    }

    /// <summary>World transform including scale, in System.Numerics layout (use Vector3.Transform).</summary>
    public Matrix4x4 WorldMatrix => ScriptBridge.TryGetEntityWorldMatrix(EntityId, out var matrix) ? matrix : Matrix4x4.Identity;

    private static bool TryNativeComponentType(Type type, out ScriptBridge.NativeComponentType nativeType)
    {
        ScriptBridge.NativeComponentType? match = type switch
        {
            _ when type == typeof(MeshComponent) => ScriptBridge.NativeComponentType.Mesh,
            _ when type == typeof(CameraComponent) => ScriptBridge.NativeComponentType.Camera,
            _ when type == typeof(LightComponent) => ScriptBridge.NativeComponentType.Light,
            _ when type == typeof(RigidbodyComponent) => ScriptBridge.NativeComponentType.Rigidbody,
            _ when type == typeof(ColliderComponent) => ScriptBridge.NativeComponentType.Collider,
            _ when type == typeof(AnimationComponent) => ScriptBridge.NativeComponentType.Animation,
            _ when type == typeof(ActiveRagdollComponent) => ScriptBridge.NativeComponentType.ActiveRagdoll,
            _ when type == typeof(CanvasComponent) => ScriptBridge.NativeComponentType.Canvas,
            _ when type == typeof(RectTransformComponent) => ScriptBridge.NativeComponentType.RectTransform,
            _ when type == typeof(UIImageComponent) => ScriptBridge.NativeComponentType.UIImage,
            _ when type == typeof(UITextComponent) => ScriptBridge.NativeComponentType.UIText,
            _ when type == typeof(UIButtonComponent) => ScriptBridge.NativeComponentType.UIButton,
            _ when type == typeof(ParticleSystemComponent) => ScriptBridge.NativeComponentType.ParticleSystem,
            _ when type == typeof(SoundEmitterComponent) => ScriptBridge.NativeComponentType.SoundEmitter,
            _ when type == typeof(RmlWidgetComponent) => ScriptBridge.NativeComponentType.RmlWidget,
            _ when type == typeof(TerrainComponent) => ScriptBridge.NativeComponentType.Terrain,
            _ => null,
        };
        nativeType = match ?? default;
        return match.HasValue;
    }

    public bool HasComponent<T>() where T : class
    {
        if (RegisteredComponent.IsRegisteredType(typeof(T)))
            return ScriptBridge.HasRegisteredComponent(EntityId, RegisteredComponent.TypeNameOf(typeof(T)));
        return typeof(T) switch
        {
            var type when type == typeof(MeshComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Mesh),
            var type when type == typeof(CameraComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Camera),
            var type when type == typeof(LightComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Light),
            var type when type == typeof(RigidbodyComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Rigidbody),
            var type when type == typeof(ColliderComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Collider),
            var type when type == typeof(AnimationComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Animation),
            var type when type == typeof(ActiveRagdollComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.ActiveRagdoll),
            var type when type == typeof(CanvasComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Canvas),
            var type when type == typeof(RectTransformComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.RectTransform),
            var type when type == typeof(UIImageComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.UIImage),
            var type when type == typeof(UITextComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.UIText),
            var type when type == typeof(UIButtonComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.UIButton),
            var type when type == typeof(ParticleSystemComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.ParticleSystem),
            var type when type == typeof(SoundEmitterComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.SoundEmitter),
            var type when type == typeof(RmlWidgetComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.RmlWidget),
            var type when type == typeof(TerrainComponent) => ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Terrain),
            _ => ScriptBridge.GetScript<T>(EntityId) is not null,
        };
    }

    /// <summary>Adds a native component reached through the property bridge (or returns the existing one).</summary>
    public T? AddComponent<T>() where T : RegisteredComponent =>
        ScriptBridge.AddRegisteredComponent(EntityId, RegisteredComponent.TypeNameOf(typeof(T)))
            ? RegisteredComponent.Create<T>(EntityId)
            : null;

    public bool RemoveComponent<T>() where T : RegisteredComponent =>
        ScriptBridge.RemoveRegisteredComponent(EntityId, RegisteredComponent.TypeNameOf(typeof(T)));

    public T? GetComponent<T>() where T : class
    {
        if (RegisteredComponent.IsRegisteredType(typeof(T)))
            return HasComponent<T>() ? RegisteredComponent.Create<T>(EntityId) : null;

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

        if (typeof(T) == typeof(ActiveRagdollComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.ActiveRagdoll))
        {
            return new ActiveRagdollComponent(EntityId) as T;
        }
        if (typeof(T) == typeof(TerrainComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.Terrain))
        {
            return new TerrainComponent(EntityId) as T;
        }
        if (typeof(T) == typeof(RmlWidgetComponent) && ScriptBridge.HasComponent(EntityId, ScriptBridge.NativeComponentType.RmlWidget))
        {
            return new RmlWidgetComponent(EntityId) as T;
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
