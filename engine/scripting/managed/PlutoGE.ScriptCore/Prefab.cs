using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>A prefab asset that can be spawned into the running scene.</summary>
/// <remarks>
/// Spawned objects are placed, parented and configured before any of their scripts run <c>OnCreate</c>, so
/// <c>OnCreate</c> sees the final position and any values set by a <c>configure</c> callback:
/// <code>
/// bulletPrefab.Instantiate&lt;Bullet&gt;(muzzle.Position, muzzle.RotationQuaternion, bullet =&gt;
/// {
///     bullet.Damage = damage;
///     bullet.Owner = GameObject;
/// });
/// </code>
/// </remarks>
public sealed class Prefab
{
    internal Prefab(string assetReference)
    {
        AssetReference = assetReference;
    }

    public string AssetReference { get; }
    public bool IsValid => !string.IsNullOrWhiteSpace(AssetReference);

    public GameObject? Instantiate() => Instantiate(AssetReference);

    public GameObject? Instantiate(Vector3 position) => Instantiate(AssetReference, position);

    /// <summary>Spawns at <paramref name="position"/> with Euler <paramref name="rotation"/> in degrees.</summary>
    public GameObject? Instantiate(Vector3 position, Vector3 rotation) => Instantiate(AssetReference, position, rotation);

    public GameObject? Instantiate(Vector3 position, Quaternion rotation, GameObject? parent = null) =>
        Instantiate(AssetReference, position, rotation, parent);

    /// <summary>
    /// Spawns the prefab and returns its root's <typeparamref name="T"/> component (a script or engine component).
    /// <paramref name="configure"/> runs before any script on the new object runs <c>OnCreate</c>.
    /// Returns null when the prefab could not be spawned or its root has no <typeparamref name="T"/>.
    /// </summary>
    public T? Instantiate<T>(Vector3 position, Quaternion rotation, Action<T>? configure = null, GameObject? parent = null)
        where T : class =>
        Instantiate(AssetReference, position, rotation, configure, parent);

    public static GameObject? Instantiate(string prefabReference) => Spawn(prefabReference, null, null);

    /// <summary>Parses and caches immutable prefab data before time-critical spawning.</summary>
    public static bool Preload(string prefabReference) => ScriptBridge.PreloadPrefab(prefabReference);

    /// <summary>Returns true when the cached prefab data is current and ready to clone.</summary>
    public static bool IsReady(string prefabReference) => ScriptBridge.IsPrefabReady(prefabReference);

    public static GameObject? Instantiate(string prefabReference, Vector3 position) =>
        Spawn(prefabReference, null, instance => instance.Position = position);

    /// <summary>Spawns at <paramref name="position"/> with Euler <paramref name="rotation"/> in degrees.</summary>
    public static GameObject? Instantiate(string prefabReference, Vector3 position, Vector3 rotation) =>
        Spawn(prefabReference, null, instance =>
        {
            instance.Position = position;
            instance.Rotation = rotation;
        });

    /// <summary>Spawns under <paramref name="parent"/> (or as a scene root) at a position and rotation local to it.</summary>
    public static GameObject? Instantiate(string prefabReference, Vector3 position, Quaternion rotation, GameObject? parent = null) =>
        Spawn(prefabReference, parent, instance =>
        {
            instance.Position = position;
            instance.RotationQuaternion = rotation;
        });

    /// <inheritdoc cref="Instantiate{T}(Vector3, Quaternion, Action{T}?, GameObject?)"/>
    public static T? Instantiate<T>(string prefabReference, Vector3 position, Quaternion rotation, Action<T>? configure = null,
                                    GameObject? parent = null)
        where T : class
    {
        T? component = null;
        Spawn(prefabReference, parent, instance =>
        {
            instance.Position = position;
            instance.RotationQuaternion = rotation;
            component = instance.GetComponent<T>();
            if (component is null)
            {
                Debug.LogWarning($"Prefab '{prefabReference}' has no {typeof(T).Name} on its root.");
            }
            else
            {
                configure?.Invoke(component);
            }
        });
        return component;
    }

    public override string ToString() => AssetReference;

    // Creates the object, lets `beforeStart` place and configure it, then starts its scripts. Scripts start even
    // when `beforeStart` throws, so the object is never left half-initialised; the exception still reaches the caller.
    private static GameObject? Spawn(string prefabReference, GameObject? parent, Action<GameObject>? beforeStart)
    {
        if (!ScriptBridge.CanSpawnPrepared)
        {
            // Hosts built before prepared spawning start scripts during instantiation.
            var legacy = ScriptBridge.InstantiatePrefab(prefabReference);
            if (legacy == 0)
            {
                return null;
            }

            var legacyInstance = new GameObject(legacy);
            if (parent is not null)
            {
                legacyInstance.SetParent(parent);
            }

            beforeStart?.Invoke(legacyInstance);
            return legacyInstance;
        }

        var entityId = ScriptBridge.InstantiatePrefabPrepared(prefabReference, parent?.EntityId ?? 0);
        if (entityId == 0)
        {
            return null;
        }

        var instance = new GameObject(entityId);
        try
        {
            beforeStart?.Invoke(instance);
        }
        finally
        {
            ScriptBridge.StartEntityScripts(entityId);
        }

        return instance;
    }
}
