using System.Collections.Concurrent;
using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe class ScriptBridge
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeVector3
    {
        public float X;
        public float Y;
        public float Z;

        public NativeVector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public readonly Vector3 ToManaged()
        {
            return new Vector3(X, Y, Z);
        }

        public static NativeVector3 FromManaged(Vector3 value)
        {
            return new NativeVector3(value.X, value.Y, value.Z);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeQuaternion
    {
        public float X;
        public float Y;
        public float Z;
        public float W;

        public readonly Quaternion ToManaged() => new(X, Y, Z, W);
        public static NativeQuaternion FromManaged(Quaternion value) => new()
        {
            X = value.X, Y = value.Y, Z = value.Z, W = value.W
        };
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeRaycastHit
    {
        public uint EntityId;
        public NativeVector3 Point;
        public NativeVector3 Normal;
        public float Distance;
    }

    internal enum NativeComponentType
    {
        Mesh = 0,
        Camera = 1,
        Light = 2,
        Script = 3,
        Rigidbody = 4,
        Collider = 5,
        Animation = 6,
        Canvas = 7,
        RectTransform = 8,
        UIImage = 9,
        UIText = 10,
        UIButton = 11,
        ParticleSystem = 12,
        SoundEmitter = 13,
        RmlWidget = 14,
        ActiveRagdoll = 15,
    }

    private sealed class ScriptLoadContext : AssemblyLoadContext
    {
        private readonly AssemblyDependencyResolver _resolver;

        public ScriptLoadContext(string mainAssemblyPath)
            : base($"PlutoGE-Scripts:{Path.GetFileNameWithoutExtension(mainAssemblyPath)}", isCollectible: true)
        {
            _resolver = new AssemblyDependencyResolver(mainAssemblyPath);
        }

        protected override Assembly? Load(AssemblyName assemblyName)
        {
            if (assemblyName.Name == typeof(ScriptBehaviour).Assembly.GetName().Name)
            {
                return typeof(ScriptBehaviour).Assembly;
            }

            var resolvedPath = _resolver.ResolveAssemblyToPath(assemblyName);
            return resolvedPath is null ? null : LoadFromAssemblyPath(resolvedPath);
        }
    }

    private sealed record ScriptFieldMetadata(string Name, int Type, string ReferenceTypeName, object? DefaultValue, MemberInfo Member);

    private sealed record ScriptClassMetadata(string AssemblyName, string NamespaceName, string ClassName, Type Type, bool IsScriptableObject, IReadOnlyList<string> AssignableTypeNames, IReadOnlyList<ScriptFieldMetadata> Fields)
    {
        public string FullName => string.IsNullOrEmpty(NamespaceName) ? ClassName : $"{NamespaceName}.{ClassName}";
    }

    private static readonly ConcurrentDictionary<long, ScriptBehaviour> Instances = new();
    private static readonly Dictionary<uint, List<ScriptBehaviour>> InstancesByEntity = new();
    private static readonly Dictionary<string, ScriptClassMetadata> ScriptClasses = new(StringComparer.Ordinal);
    private static readonly Dictionary<(Type Type, string Name, int Arity), MethodInfo[]> InvokableMethods = new();
    private static readonly object Gate = new();

    private static ScriptLoadContext? _loadContext;
    private static Assembly? _loadedAssembly;
    private static long _nextInstanceHandle;
    private static string _lastError = string.Empty;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityPosition;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityWorldPosition;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setEntityPosition;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setEntityWorldPosition;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityRotation;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityWorldRotation;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setEntityRotation;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setEntityWorldRotation;
    private static delegate* unmanaged[Cdecl]<uint, NativeQuaternion> _getEntityRotationQuaternion;
    private static delegate* unmanaged[Cdecl]<uint, NativeQuaternion, void> _setEntityRotationQuaternion;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityScale;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setEntityScale;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityForward;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityRight;
    private static delegate* unmanaged[Cdecl]<uint, int> _getEntityActive;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setEntityActive;
    private static delegate* unmanaged[Cdecl]<uint, int> _getEntityTagCount;
    private static delegate* unmanaged[Cdecl]<uint, int, nint> _getEntityTag;
    private static delegate* unmanaged[Cdecl]<uint, int> _destroyEntity;
    private static delegate* unmanaged[Cdecl]<uint, nint> _getEntityName;
    private static delegate* unmanaged[Cdecl]<byte*, uint> _findEntityByName;
    private static delegate* unmanaged[Cdecl]<byte*, int> _getEntityCountByTag;
    private static delegate* unmanaged[Cdecl]<byte*, int, uint> _getEntityByTag;
    private static delegate* unmanaged[Cdecl]<byte*, uint> _instantiatePrefab;
    private static delegate* unmanaged[Cdecl]<byte*, int> _preloadPrefab;
    private static delegate* unmanaged[Cdecl]<byte*, int> _isPrefabReady;
    private static delegate* unmanaged[Cdecl]<byte*, int> _loadScene;
    private static delegate* unmanaged[Cdecl]<nint> _getActiveScenePath;
    private static delegate* unmanaged[Cdecl]<void> _quitApplication;
    private static delegate* unmanaged[Cdecl]<byte*, nint> _loadScriptableObjectAsset;
    private static delegate* unmanaged[Cdecl]<uint, int, int> _hasComponent;
    private static delegate* unmanaged[Cdecl]<uint, int, int> _getComponentEnabled;
    private static delegate* unmanaged[Cdecl]<uint, int, int, void> _setComponentEnabled;
    private static delegate* unmanaged[Cdecl]<uint, int> _getCameraMain;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setCameraMain;
    private static delegate* unmanaged[Cdecl]<uint, float> _getCameraFov;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setCameraFov;
    private static delegate* unmanaged[Cdecl]<uint, float> _getLightIntensity;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setLightIntensity;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getLightColor;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setLightColor;
    private static delegate* unmanaged[Cdecl]<uint, int> _getMeshStatic;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setMeshStatic;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getMeshColor;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setMeshColor;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getMeshEmission;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setMeshEmission;
    private static delegate* unmanaged[Cdecl]<uint, int> _getAnimationClipCount;
    private static delegate* unmanaged[Cdecl]<uint, int> _getAnimationClipIndex;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setAnimationClipIndex;
    private static delegate* unmanaged[Cdecl]<uint, int, nint> _getAnimationClipName;
    private static delegate* unmanaged[Cdecl]<uint, int, float> _getAnimationClipDuration;
    private static delegate* unmanaged[Cdecl]<uint, int> _getAnimationPlaying;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setAnimationPlaying;
    private static delegate* unmanaged[Cdecl]<uint, int> _getAnimationLooping;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setAnimationLooping;
    private static delegate* unmanaged[Cdecl]<uint, int> _getAnimationAutoplay;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setAnimationAutoplay;
    private static delegate* unmanaged[Cdecl]<uint, float> _getAnimationSpeed;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setAnimationSpeed;
    private static delegate* unmanaged[Cdecl]<uint, float> _getAnimationTime;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setAnimationTime;
    private static delegate* unmanaged[Cdecl]<uint, void> _animationPlay;
    private static delegate* unmanaged[Cdecl]<uint, void> _animationPause;
    private static delegate* unmanaged[Cdecl]<uint, void> _animationStop;
    private static delegate* unmanaged[Cdecl]<uint, byte*, int, void> _setAnimationBoolParameter;
    private static delegate* unmanaged[Cdecl]<uint, byte*, float, void> _setAnimationFloatParameter;
    private static delegate* unmanaged[Cdecl]<uint, byte*, int, void> _setAnimationIntParameter;
    private static delegate* unmanaged[Cdecl]<uint, byte*, void> _setAnimationTriggerParameter;
    private static delegate* unmanaged[Cdecl]<uint, byte*, void> _resetAnimationTriggerParameter;
    private static delegate* unmanaged[Cdecl]<uint, byte*, void> _animationPlayState;
    private static delegate* unmanaged[Cdecl]<uint, int> _getRagdollEnabled;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setRagdollEnabled;
    private static delegate* unmanaged[Cdecl]<uint, float> _getRagdollWeight;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setRagdollWeight;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _addRagdollImpulse;
    private static delegate* unmanaged[Cdecl]<uint, void> _resetRagdoll;
    private static delegate* unmanaged[Cdecl]<uint, float> _getActiveRagdollPositionStrength;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setActiveRagdollPositionStrength;
    private static delegate* unmanaged[Cdecl]<uint, float> _getActiveRagdollRotationStrength;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setActiveRagdollRotationStrength;
    private static delegate* unmanaged[Cdecl]<uint, float> _getActiveRagdollDamping;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setActiveRagdollDamping;
    private static delegate* unmanaged[Cdecl]<uint, float> _getRigidbodyMass;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setRigidbodyMass;
    private static delegate* unmanaged[Cdecl]<uint, float> _getRigidbodyLinearDrag;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setRigidbodyLinearDrag;
    private static delegate* unmanaged[Cdecl]<uint, float> _getRigidbodyAngularDrag;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setRigidbodyAngularDrag;
    private static delegate* unmanaged[Cdecl]<uint, float> _getRigidbodyFriction;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setRigidbodyFriction;
    private static delegate* unmanaged[Cdecl]<uint, int> _getRigidbodyUseGravity;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setRigidbodyUseGravity;
    private static delegate* unmanaged[Cdecl]<uint, int> _getRigidbodyKinematic;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setRigidbodyKinematic;
    private static delegate* unmanaged[Cdecl]<uint, int> _getRigidbodyFreezeRotation;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setRigidbodyFreezeRotation;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getRigidbodyVelocity;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setRigidbodyVelocity;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getRigidbodyAngularVelocity;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setRigidbodyAngularVelocity;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _addRigidbodyForce;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _addRigidbodyImpulse;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, NativeVector3, void> _addRigidbodyForceAtPosition;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, NativeVector3, void> _addRigidbodyImpulseAtPosition;
    private static delegate* unmanaged[Cdecl]<uint, int> _getColliderShape;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setColliderShape;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getColliderCenter;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setColliderCenter;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getColliderSize;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setColliderSize;
    private static delegate* unmanaged[Cdecl]<uint, float> _getColliderRadius;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setColliderRadius;
    private static delegate* unmanaged[Cdecl]<uint, float> _getColliderHeight;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setColliderHeight;
    private static delegate* unmanaged[Cdecl]<uint, int> _getColliderTrigger;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setColliderTrigger;
    private static delegate* unmanaged[Cdecl]<uint, int> _getColliderBlocksAudio;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setColliderBlocksAudio;
    private static delegate* unmanaged[Cdecl]<uint, int> _getParticleSystemPlaying;
    private static delegate* unmanaged[Cdecl]<uint, int> _getParticleSystemParticleCount;
    private static delegate* unmanaged[Cdecl]<uint, void> _particleSystemPlay;
    private static delegate* unmanaged[Cdecl]<uint, void> _particleSystemPause;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _particleSystemStop;
    private static delegate* unmanaged[Cdecl]<uint, void> _particleSystemClear;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _particleSystemEmit;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, int, void> _particleSystemEmitAt;
    private static delegate* unmanaged[Cdecl]<uint, nint> _getParticleSystemAssetReference;
    private static delegate* unmanaged[Cdecl]<uint, nint, void> _setParticleSystemAssetReference;
    private static delegate* unmanaged[Cdecl]<uint, int> _getParticleSystemLooping;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setParticleSystemLooping;
    private static delegate* unmanaged[Cdecl]<uint, int> _getParticleSystemPlayOnAwake;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setParticleSystemPlayOnAwake;
    private static delegate* unmanaged[Cdecl]<uint, float> _getParticleSystemDuration;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setParticleSystemDuration;
    private static delegate* unmanaged[Cdecl]<uint, float> _getParticleSystemStartLifetime;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setParticleSystemStartLifetime;
    private static delegate* unmanaged[Cdecl]<uint, float> _getParticleSystemStartSpeed;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setParticleSystemStartSpeed;
    private static delegate* unmanaged[Cdecl]<uint, float> _getParticleSystemStartSize;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setParticleSystemStartSize;
    private static delegate* unmanaged[Cdecl]<uint, float> _getParticleSystemGravityModifier;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setParticleSystemGravityModifier;
    private static delegate* unmanaged[Cdecl]<uint, float> _getParticleSystemEmissionRate;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setParticleSystemEmissionRate;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getParticleSystemStartColor;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setParticleSystemStartColor;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getParticleSystemShapeSize;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setParticleSystemShapeSize;
    private static delegate* unmanaged[Cdecl]<uint, int> _getParticleSystemSimulationSpace;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setParticleSystemSimulationSpace;
    private static delegate* unmanaged[Cdecl]<uint, int> _getParticleSystemShape;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setParticleSystemShape;
    private static delegate* unmanaged[Cdecl]<uint, int> _getSoundEmitterPlaying;
    private static delegate* unmanaged[Cdecl]<uint, void> _soundEmitterPlay;
    private static delegate* unmanaged[Cdecl]<uint, void> _soundEmitterPlayOneShot;
    private static delegate* unmanaged[Cdecl]<uint, float, float, void> _soundEmitterPlayOneShotScaled;
    private static delegate* unmanaged[Cdecl]<uint, void> _soundEmitterPause;
    private static delegate* unmanaged[Cdecl]<uint, void> _soundEmitterStop;
    private static delegate* unmanaged[Cdecl]<uint, nint> _getSoundEmitterClipReference;
    private static delegate* unmanaged[Cdecl]<uint, nint, void> _setSoundEmitterClipReference;
    private static delegate* unmanaged[Cdecl]<uint, int> _getSoundEmitterLooping;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setSoundEmitterLooping;
    private static delegate* unmanaged[Cdecl]<uint, int> _getSoundEmitterSpatialized;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setSoundEmitterSpatialized;
    private static delegate* unmanaged[Cdecl]<uint, int> _getSoundEmitterPlayOnAwake;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setSoundEmitterPlayOnAwake;
    private static delegate* unmanaged[Cdecl]<uint, float> _getSoundEmitterVolume;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setSoundEmitterVolume;
    private static delegate* unmanaged[Cdecl]<uint, float> _getSoundEmitterPitch;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setSoundEmitterPitch;
    private static delegate* unmanaged[Cdecl]<uint, float> _getCanvasScaleFactor;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setCanvasScaleFactor;
    private static delegate* unmanaged[Cdecl]<uint, int> _getCanvasSortingOrder;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setCanvasSortingOrder;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getRectAnchoredPosition;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setRectAnchoredPosition;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getRectSizeDelta;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setRectSizeDelta;
    private static delegate* unmanaged[Cdecl]<uint, int> _getRectAnchorPreset;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setRectAnchorPreset;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getUIImageColor;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setUIImageColor;
    private static delegate* unmanaged[Cdecl]<uint, float> _getUIImageAlpha;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setUIImageAlpha;
    private static delegate* unmanaged[Cdecl]<uint, nint> _getUIImageTexture;
    private static delegate* unmanaged[Cdecl]<uint, nint, void> _setUIImageTexture;
    private static delegate* unmanaged[Cdecl]<uint, int> _getUIImagePreserveAspect;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setUIImagePreserveAspect;
    private static delegate* unmanaged[Cdecl]<uint, float> _getUIImageFillAmount;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setUIImageFillAmount;
    private static delegate* unmanaged[Cdecl]<uint, nint> _getUIText;
    private static delegate* unmanaged[Cdecl]<uint, nint, void> _setUIText;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getUITextColor;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setUITextColor;
    private static delegate* unmanaged[Cdecl]<uint, float> _getUITextFontSize;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setUITextFontSize;
    private static delegate* unmanaged[Cdecl]<uint, int> _getUIButtonInteractable;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setUIButtonInteractable;
    private static delegate* unmanaged[Cdecl]<uint, int> _getUIButtonHovered;
    private static delegate* unmanaged[Cdecl]<uint, int> _getUIButtonPressed;
    private static delegate* unmanaged[Cdecl]<uint, int> _getUIButtonReleased;
    private static delegate* unmanaged[Cdecl]<uint, int> _getUIButtonClicked;
    private static delegate* unmanaged[Cdecl]<uint, int> _getCanvasScaleMode;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setCanvasScaleMode;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getCanvasReferenceResolution;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setCanvasReferenceResolution;
    private static delegate* unmanaged[Cdecl]<uint, float> _getRectRotation;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setRectRotation;
    private static delegate* unmanaged[Cdecl]<uint, float> _getRectOpacity;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setRectOpacity;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getRectLocalScale;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setRectLocalScale;
    private static delegate* unmanaged[Cdecl]<uint, int> _getRectLayoutMode;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setRectLayoutMode;
    private static delegate* unmanaged[Cdecl]<uint, int> _getUIImageType;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setUIImageType;
    private static delegate* unmanaged[Cdecl]<uint, float> _getUIImageThickness;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setUIImageThickness;
    private static delegate* unmanaged[Cdecl]<uint, int> _getUITextAlignment;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setUITextAlignment;
    private static delegate* unmanaged[Cdecl]<ulong> _getUIUpdateSequence;
    private static delegate* unmanaged[Cdecl]<nint, int, int> _rmlShowDocument;
    private static delegate* unmanaged[Cdecl]<nint, int> _rmlReloadDocument;
    private static delegate* unmanaged[Cdecl]<nint, nint, nint, int> _rmlSetText;
    private static delegate* unmanaged[Cdecl]<nint, nint, nint> _rmlGetText;
    private static delegate* unmanaged[Cdecl]<nint, nint, nint, nint, int> _rmlSetAttribute;
    private static delegate* unmanaged[Cdecl]<nint, nint, nint, nint> _rmlGetAttribute;
    private static delegate* unmanaged[Cdecl]<nint, nint, nint, int, int> _rmlSetClass;
    private static delegate* unmanaged[Cdecl]<nint, nint, nint, nint, int> _rmlSetStyle;
    private static delegate* unmanaged[Cdecl]<nint, nint, nint, int> _rmlSubscribeEvent;
    private static delegate* unmanaged[Cdecl]<nint, nint, nint, int> _rmlConsumeEvent;
    private static delegate* unmanaged[Cdecl]<float> _getSceneTimeScale;
    private static delegate* unmanaged[Cdecl]<float, void> _setSceneTimeScale;
    private static delegate* unmanaged[Cdecl]<uint, nint> _getRmlWidgetSource;
    private static delegate* unmanaged[Cdecl]<uint, nint, void> _setRmlWidgetSource;
    private static delegate* unmanaged[Cdecl]<uint, int> _getRmlWidgetVisible;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setRmlWidgetVisible;
    private static delegate* unmanaged[Cdecl]<int, int> _getKeyDown;
    private static delegate* unmanaged[Cdecl]<int, int> _getKeyPressed;
    private static delegate* unmanaged[Cdecl]<int, int> _getKeyReleased;
    private static delegate* unmanaged[Cdecl]<int, int> _getMouseButtonDown;
    private static delegate* unmanaged[Cdecl]<int, int> _getMouseButtonPressed;
    private static delegate* unmanaged[Cdecl]<int, int> _getMouseButtonReleased;
    private static delegate* unmanaged[Cdecl]<NativeVector3> _getMousePosition;
    private static delegate* unmanaged[Cdecl]<NativeVector3> _getMouseDelta;
    private static delegate* unmanaged[Cdecl]<NativeVector3> _getMouseScrollDelta;
    private static delegate* unmanaged[Cdecl]<int> _getQuitRequested;
    private static delegate* unmanaged[Cdecl]<int> _getCursorLocked;
    private static delegate* unmanaged[Cdecl]<int, void> _setCursorLocked;
    private static delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3, float, uint, NativeRaycastHit*, int> _physicsRaycast;
    private static delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3, float, uint, nint, NativeRaycastHit*, int> _physicsRaycastTagged;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, float, NativeVector3> _physicsMoveKinematic;
    private static delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3, nint, NativeVector3, float, float, uint> _spawnDecal;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, float, float, NativeVector3*, int> _navigationProjectPoint;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, NativeVector3, float, float, NativeVector3*, int, int*, int> _navigationFindPath;
    private static delegate* unmanaged[Cdecl]<int, nint, void> _logMessage;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "LoadScriptAssembly")]
    public static int LoadScriptAssembly(nint assemblyPathPtr, nint sourceAssemblyPathPtr)
    {
        try
        {
            var assemblyPath = Marshal.PtrToStringUTF8(assemblyPathPtr);
            if (string.IsNullOrWhiteSpace(assemblyPath))
            {
                SetError("Managed assembly path was empty.");
                return 0;
            }

            var sourceAssemblyPath = Marshal.PtrToStringUTF8(sourceAssemblyPathPtr);
            if (string.IsNullOrWhiteSpace(sourceAssemblyPath))
            {
                sourceAssemblyPath = assemblyPath;
            }

            lock (Gate)
            {
                ResetLoadedAssembly();

                var fullPath = Path.GetFullPath(assemblyPath);
                var fullSourcePath = Path.GetFullPath(sourceAssemblyPath);
                var builtinAssembly = typeof(ScriptBehaviour).Assembly;
                var isBuiltinAssembly = string.Equals(
                    Path.GetFullPath(builtinAssembly.Location),
                    fullPath,
                    StringComparison.OrdinalIgnoreCase);

                if (isBuiltinAssembly)
                {
                    _loadedAssembly = builtinAssembly;
                }
                else
                {
                    _loadContext = new ScriptLoadContext(fullPath);
                    _loadedAssembly = _loadContext.LoadFromAssemblyPath(fullPath);
                }

                Application.ConfigureForScriptAssembly(
                    fullSourcePath,
                    _loadedAssembly.GetName().Name ?? Path.GetFileNameWithoutExtension(fullPath));

                ScriptClasses.Clear();
                foreach (var scriptClass in DiscoverBuiltinScriptClasses())
                {
                    ScriptClasses[scriptClass.FullName] = scriptClass;
                }

                if (!isBuiltinAssembly)
                {
                    foreach (var scriptClass in DiscoverScriptClasses(_loadedAssembly))
                    {
                        ScriptClasses[scriptClass.FullName] = scriptClass;
                    }
                }

                _lastError = string.Empty;
                return 1;
            }
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    private static IEnumerable<ScriptClassMetadata> DiscoverBuiltinScriptClasses()
    {
        foreach (var scriptClass in DiscoverScriptClasses(typeof(ScriptBehaviour).Assembly))
        {
            if (scriptClass.FullName.StartsWith("PlutoGE.ScriptCore.Examples.", StringComparison.Ordinal))
            {
                yield return scriptClass;
            }
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "GetScriptMetadata")]
    public static nint GetScriptMetadata()
    {
        lock (Gate)
        {
            var metadata = BuildMetadataPayload();
            return Marshal.StringToCoTaskMemUTF8(metadata);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "GetLastError")]
    public static nint GetLastError()
    {
        lock (Gate)
        {
            return Marshal.StringToCoTaskMemUTF8(_lastError);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "FreeNativeString")]
    public static void FreeNativeString(nint textPtr)
    {
        if (textPtr != 0)
        {
            Marshal.FreeCoTaskMem(textPtr);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterGameObjectApi")]
    public static int RegisterGameObjectApi(
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityPosition,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityWorldPosition,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setEntityPosition,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityRotation,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setEntityRotation,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityScale,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setEntityScale,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityForward,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityRight,
        delegate* unmanaged[Cdecl]<uint, int> getEntityActive,
        delegate* unmanaged[Cdecl]<uint, int, void> setEntityActive,
        delegate* unmanaged[Cdecl]<uint, int> getEntityTagCount,
        delegate* unmanaged[Cdecl]<uint, int, nint> getEntityTag,
        delegate* unmanaged[Cdecl]<uint, int> destroyEntity,
        delegate* unmanaged[Cdecl]<uint, nint> getEntityName,
        delegate* unmanaged[Cdecl]<byte*, uint> findEntityByName,
        delegate* unmanaged[Cdecl]<byte*, int> getEntityCountByTag,
        delegate* unmanaged[Cdecl]<byte*, int, uint> getEntityByTag,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setEntityWorldPosition,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityWorldRotation,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setEntityWorldRotation,
        delegate* unmanaged[Cdecl]<uint, NativeQuaternion> getEntityRotationQuaternion,
        delegate* unmanaged[Cdecl]<uint, NativeQuaternion, void> setEntityRotationQuaternion)
    {
        if (getEntityPosition == null ||
            getEntityWorldPosition == null ||
            setEntityPosition == null ||
            getEntityRotation == null ||
            setEntityRotation == null ||
            getEntityScale == null ||
            setEntityScale == null ||
            getEntityForward == null ||
            getEntityRight == null ||
            getEntityActive == null ||
            setEntityActive == null ||
            getEntityTagCount == null ||
            getEntityTag == null ||
            destroyEntity == null ||
            getEntityName == null ||
            findEntityByName == null ||
            getEntityCountByTag == null ||
            getEntityByTag == null ||
            setEntityWorldPosition == null ||
            getEntityWorldRotation == null ||
            setEntityWorldRotation == null ||
            getEntityRotationQuaternion == null ||
            setEntityRotationQuaternion == null)
        {
            SetError("Managed game object API registration received a null function pointer.");
            return 0;
        }

        _getEntityPosition = getEntityPosition;
        _getEntityWorldPosition = getEntityWorldPosition;
        _setEntityPosition = setEntityPosition;
        _setEntityWorldPosition = setEntityWorldPosition;
        _getEntityRotation = getEntityRotation;
        _getEntityWorldRotation = getEntityWorldRotation;
        _setEntityRotation = setEntityRotation;
        _setEntityWorldRotation = setEntityWorldRotation;
        _getEntityRotationQuaternion = getEntityRotationQuaternion;
        _setEntityRotationQuaternion = setEntityRotationQuaternion;
        _getEntityScale = getEntityScale;
        _setEntityScale = setEntityScale;
        _getEntityForward = getEntityForward;
        _getEntityRight = getEntityRight;
        _getEntityActive = getEntityActive;
        _setEntityActive = setEntityActive;
        _getEntityTagCount = getEntityTagCount;
        _getEntityTag = getEntityTag;
        _destroyEntity = destroyEntity;
        _getEntityName = getEntityName;
        _findEntityByName = findEntityByName;
        _getEntityCountByTag = getEntityCountByTag;
        _getEntityByTag = getEntityByTag;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterPrefabApi")]
    public static int RegisterPrefabApi(
        delegate* unmanaged[Cdecl]<byte*, uint> instantiatePrefab,
        delegate* unmanaged[Cdecl]<byte*, int> preloadPrefab,
        delegate* unmanaged[Cdecl]<byte*, int> isPrefabReady)
    {
        if (instantiatePrefab == null || preloadPrefab == null || isPrefabReady == null)
        {
            SetError("Managed prefab API registration received a null function pointer.");
            return 0;
        }

        _instantiatePrefab = instantiatePrefab;
        _preloadPrefab = preloadPrefab;
        _isPrefabReady = isPrefabReady;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterSceneApi")]
    public static int RegisterSceneApi(
        delegate* unmanaged[Cdecl]<byte*, int> loadScene,
        delegate* unmanaged[Cdecl]<nint> getActiveScenePath,
        delegate* unmanaged[Cdecl]<void> quitApplication)
    {
        if (loadScene == null || getActiveScenePath == null || quitApplication == null)
        {
            SetError("Managed scene API registration received a null function pointer.");
            return 0;
        }

        _loadScene = loadScene;
        _getActiveScenePath = getActiveScenePath;
        _quitApplication = quitApplication;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterScriptableObjectApi")]
    public static int RegisterScriptableObjectApi(delegate* unmanaged[Cdecl]<byte*, nint> loadAsset)
    {
        if (loadAsset == null)
        {
            SetError("Managed scriptable object API registration received a null function pointer.");
            return 0;
        }

        _loadScriptableObjectAsset = loadAsset;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterComponentApi")]
    public static int RegisterComponentApi(
        delegate* unmanaged[Cdecl]<uint, int, int> hasComponent,
        delegate* unmanaged[Cdecl]<uint, int, int> getComponentEnabled,
        delegate* unmanaged[Cdecl]<uint, int, int, void> setComponentEnabled)
    {
        if (hasComponent == null || getComponentEnabled == null || setComponentEnabled == null)
        {
            SetError("Managed component API registration received a null function pointer.");
            return 0;
        }

        _hasComponent = hasComponent;
        _getComponentEnabled = getComponentEnabled;
        _setComponentEnabled = setComponentEnabled;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterCameraComponentApi")]
    public static int RegisterCameraComponentApi(
        delegate* unmanaged[Cdecl]<uint, int> getCameraMain,
        delegate* unmanaged[Cdecl]<uint, int, void> setCameraMain,
        delegate* unmanaged[Cdecl]<uint, float> getCameraFov,
        delegate* unmanaged[Cdecl]<uint, float, void> setCameraFov)
    {
        if (getCameraMain == null || setCameraMain == null || getCameraFov == null || setCameraFov == null)
        {
            SetError("Managed camera component API registration received a null function pointer.");
            return 0;
        }

        _getCameraMain = getCameraMain;
        _setCameraMain = setCameraMain;
        _getCameraFov = getCameraFov;
        _setCameraFov = setCameraFov;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterLightComponentApi")]
    public static int RegisterLightComponentApi(
        delegate* unmanaged[Cdecl]<uint, float> getLightIntensity,
        delegate* unmanaged[Cdecl]<uint, float, void> setLightIntensity,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getLightColor,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setLightColor)
    {
        if (getLightIntensity == null || setLightIntensity == null || getLightColor == null || setLightColor == null)
        {
            SetError("Managed light component API registration received a null function pointer.");
            return 0;
        }

        _getLightIntensity = getLightIntensity;
        _setLightIntensity = setLightIntensity;
        _getLightColor = getLightColor;
        _setLightColor = setLightColor;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterMeshComponentApi")]
    public static int RegisterMeshComponentApi(
        delegate* unmanaged[Cdecl]<uint, int> getMeshStatic,
        delegate* unmanaged[Cdecl]<uint, int, void> setMeshStatic,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getMeshColor,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setMeshColor,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getMeshEmission,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setMeshEmission)
    {
        if (getMeshStatic == null || setMeshStatic == null || getMeshColor == null || setMeshColor == null ||
            getMeshEmission == null || setMeshEmission == null)
        {
            SetError("Managed mesh component API registration received a null function pointer.");
            return 0;
        }

        _getMeshStatic = getMeshStatic;
        _setMeshStatic = setMeshStatic;
        _getMeshColor = getMeshColor;
        _setMeshColor = setMeshColor;
        _getMeshEmission = getMeshEmission;
        _setMeshEmission = setMeshEmission;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterAnimationComponentApi")]
    public static int RegisterAnimationComponentApi(
        delegate* unmanaged[Cdecl]<uint, int> getClipCount,
        delegate* unmanaged[Cdecl]<uint, int> getClipIndex,
        delegate* unmanaged[Cdecl]<uint, int, void> setClipIndex,
        delegate* unmanaged[Cdecl]<uint, int, nint> getClipName,
        delegate* unmanaged[Cdecl]<uint, int, float> getClipDuration,
        delegate* unmanaged[Cdecl]<uint, int> getPlaying,
        delegate* unmanaged[Cdecl]<uint, int, void> setPlaying,
        delegate* unmanaged[Cdecl]<uint, int> getLooping,
        delegate* unmanaged[Cdecl]<uint, int, void> setLooping,
        delegate* unmanaged[Cdecl]<uint, int> getAutoplay,
        delegate* unmanaged[Cdecl]<uint, int, void> setAutoplay,
        delegate* unmanaged[Cdecl]<uint, float> getSpeed,
        delegate* unmanaged[Cdecl]<uint, float, void> setSpeed,
        delegate* unmanaged[Cdecl]<uint, float> getTime,
        delegate* unmanaged[Cdecl]<uint, float, void> setTime,
        delegate* unmanaged[Cdecl]<uint, void> play,
        delegate* unmanaged[Cdecl]<uint, void> pause,
        delegate* unmanaged[Cdecl]<uint, void> stop,
        delegate* unmanaged[Cdecl]<uint, byte*, int, void> setBoolParameter,
        delegate* unmanaged[Cdecl]<uint, byte*, float, void> setFloatParameter,
        delegate* unmanaged[Cdecl]<uint, byte*, int, void> setIntParameter,
        delegate* unmanaged[Cdecl]<uint, byte*, void> setTriggerParameter,
        delegate* unmanaged[Cdecl]<uint, byte*, void> resetTriggerParameter,
        delegate* unmanaged[Cdecl]<uint, byte*, void> playState,
        delegate* unmanaged[Cdecl]<uint, int> getRagdollEnabled,
        delegate* unmanaged[Cdecl]<uint, int, void> setRagdollEnabled,
        delegate* unmanaged[Cdecl]<uint, float> getRagdollWeight,
        delegate* unmanaged[Cdecl]<uint, float, void> setRagdollWeight,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> addRagdollImpulse,
        delegate* unmanaged[Cdecl]<uint, void> resetRagdoll,
        delegate* unmanaged[Cdecl]<uint, float> getActivePositionStrength,
        delegate* unmanaged[Cdecl]<uint, float, void> setActivePositionStrength,
        delegate* unmanaged[Cdecl]<uint, float> getActiveRotationStrength,
        delegate* unmanaged[Cdecl]<uint, float, void> setActiveRotationStrength,
        delegate* unmanaged[Cdecl]<uint, float> getActiveDamping,
        delegate* unmanaged[Cdecl]<uint, float, void> setActiveDamping)
    {
        if (getClipCount == null || getClipIndex == null || setClipIndex == null || getClipName == null || getClipDuration == null ||
            getPlaying == null || setPlaying == null || getLooping == null || setLooping == null || getAutoplay == null ||
            setAutoplay == null || getSpeed == null || setSpeed == null || getTime == null || setTime == null ||
            play == null || pause == null || stop == null || setBoolParameter == null || setFloatParameter == null ||
            setIntParameter == null || setTriggerParameter == null || resetTriggerParameter == null || playState == null ||
            getRagdollEnabled == null || setRagdollEnabled == null || getRagdollWeight == null || setRagdollWeight == null ||
            addRagdollImpulse == null || resetRagdoll == null || getActivePositionStrength == null ||
            setActivePositionStrength == null || getActiveRotationStrength == null || setActiveRotationStrength == null ||
            getActiveDamping == null || setActiveDamping == null)
        {
            SetError("Managed animation component API registration received a null function pointer.");
            return 0;
        }

        _getAnimationClipCount = getClipCount;
        _getAnimationClipIndex = getClipIndex;
        _setAnimationClipIndex = setClipIndex;
        _getAnimationClipName = getClipName;
        _getAnimationClipDuration = getClipDuration;
        _getAnimationPlaying = getPlaying;
        _setAnimationPlaying = setPlaying;
        _getAnimationLooping = getLooping;
        _setAnimationLooping = setLooping;
        _getAnimationAutoplay = getAutoplay;
        _setAnimationAutoplay = setAutoplay;
        _getAnimationSpeed = getSpeed;
        _setAnimationSpeed = setSpeed;
        _getAnimationTime = getTime;
        _setAnimationTime = setTime;
        _animationPlay = play;
        _animationPause = pause;
        _animationStop = stop;
        _setAnimationBoolParameter = setBoolParameter;
        _setAnimationFloatParameter = setFloatParameter;
        _setAnimationIntParameter = setIntParameter;
        _setAnimationTriggerParameter = setTriggerParameter;
        _resetAnimationTriggerParameter = resetTriggerParameter;
        _animationPlayState = playState;
        _getRagdollEnabled = getRagdollEnabled;
        _setRagdollEnabled = setRagdollEnabled;
        _getRagdollWeight = getRagdollWeight;
        _setRagdollWeight = setRagdollWeight;
        _addRagdollImpulse = addRagdollImpulse;
        _resetRagdoll = resetRagdoll;
        _getActiveRagdollPositionStrength = getActivePositionStrength;
        _setActiveRagdollPositionStrength = setActivePositionStrength;
        _getActiveRagdollRotationStrength = getActiveRotationStrength;
        _setActiveRagdollRotationStrength = setActiveRotationStrength;
        _getActiveRagdollDamping = getActiveDamping;
        _setActiveRagdollDamping = setActiveDamping;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterRigidbodyComponentApi")]
    public static int RegisterRigidbodyComponentApi(
        delegate* unmanaged[Cdecl]<uint, float> getMass,
        delegate* unmanaged[Cdecl]<uint, float, void> setMass,
        delegate* unmanaged[Cdecl]<uint, float> getLinearDrag,
        delegate* unmanaged[Cdecl]<uint, float, void> setLinearDrag,
        delegate* unmanaged[Cdecl]<uint, float> getAngularDrag,
        delegate* unmanaged[Cdecl]<uint, float, void> setAngularDrag,
        delegate* unmanaged[Cdecl]<uint, float> getFriction,
        delegate* unmanaged[Cdecl]<uint, float, void> setFriction,
        delegate* unmanaged[Cdecl]<uint, int> getUseGravity,
        delegate* unmanaged[Cdecl]<uint, int, void> setUseGravity,
        delegate* unmanaged[Cdecl]<uint, int> getKinematic,
        delegate* unmanaged[Cdecl]<uint, int, void> setKinematic,
        delegate* unmanaged[Cdecl]<uint, int> getFreezeRotation,
        delegate* unmanaged[Cdecl]<uint, int, void> setFreezeRotation,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getVelocity,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setVelocity,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getAngularVelocity,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setAngularVelocity,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> addForce,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> addImpulse,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, NativeVector3, void> addForceAtPosition,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, NativeVector3, void> addImpulseAtPosition)
    {
        if (getMass == null || setMass == null || getLinearDrag == null || setLinearDrag == null ||
            getAngularDrag == null || setAngularDrag == null || getFriction == null || setFriction == null ||
            getUseGravity == null || setUseGravity == null ||
            getKinematic == null || setKinematic == null || getFreezeRotation == null || setFreezeRotation == null ||
            getVelocity == null || setVelocity == null || getAngularVelocity == null || setAngularVelocity == null ||
            addForce == null || addImpulse == null || addForceAtPosition == null || addImpulseAtPosition == null)
        {
            SetError("Managed rigidbody component API registration received a null function pointer.");
            return 0;
        }

        _getRigidbodyMass = getMass;
        _setRigidbodyMass = setMass;
        _getRigidbodyLinearDrag = getLinearDrag;
        _setRigidbodyLinearDrag = setLinearDrag;
        _getRigidbodyAngularDrag = getAngularDrag;
        _setRigidbodyAngularDrag = setAngularDrag;
        _getRigidbodyFriction = getFriction;
        _setRigidbodyFriction = setFriction;
        _getRigidbodyUseGravity = getUseGravity;
        _setRigidbodyUseGravity = setUseGravity;
        _getRigidbodyKinematic = getKinematic;
        _setRigidbodyKinematic = setKinematic;
        _getRigidbodyFreezeRotation = getFreezeRotation;
        _setRigidbodyFreezeRotation = setFreezeRotation;
        _getRigidbodyVelocity = getVelocity;
        _setRigidbodyVelocity = setVelocity;
        _getRigidbodyAngularVelocity = getAngularVelocity;
        _setRigidbodyAngularVelocity = setAngularVelocity;
        _addRigidbodyForce = addForce;
        _addRigidbodyImpulse = addImpulse;
        _addRigidbodyForceAtPosition = addForceAtPosition;
        _addRigidbodyImpulseAtPosition = addImpulseAtPosition;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterColliderComponentApi")]
    public static int RegisterColliderComponentApi(
        delegate* unmanaged[Cdecl]<uint, int> getShape,
        delegate* unmanaged[Cdecl]<uint, int, void> setShape,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getCenter,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setCenter,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getSize,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setSize,
        delegate* unmanaged[Cdecl]<uint, float> getRadius,
        delegate* unmanaged[Cdecl]<uint, float, void> setRadius,
        delegate* unmanaged[Cdecl]<uint, float> getHeight,
        delegate* unmanaged[Cdecl]<uint, float, void> setHeight,
        delegate* unmanaged[Cdecl]<uint, int> getTrigger,
        delegate* unmanaged[Cdecl]<uint, int, void> setTrigger,
        delegate* unmanaged[Cdecl]<uint, int> getBlocksAudio,
        delegate* unmanaged[Cdecl]<uint, int, void> setBlocksAudio)
    {
        if (getShape == null || setShape == null || getCenter == null || setCenter == null ||
            getSize == null || setSize == null || getRadius == null || setRadius == null ||
            getHeight == null || setHeight == null || getTrigger == null || setTrigger == null ||
            getBlocksAudio == null || setBlocksAudio == null)
        {
            SetError("Managed collider component API registration received a null function pointer.");
            return 0;
        }

        _getColliderShape = getShape;
        _setColliderShape = setShape;
        _getColliderCenter = getCenter;
        _setColliderCenter = setCenter;
        _getColliderSize = getSize;
        _setColliderSize = setSize;
        _getColliderRadius = getRadius;
        _setColliderRadius = setRadius;
        _getColliderHeight = getHeight;
        _setColliderHeight = setHeight;
        _getColliderTrigger = getTrigger;
        _setColliderTrigger = setTrigger;
        _getColliderBlocksAudio = getBlocksAudio;
        _setColliderBlocksAudio = setBlocksAudio;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterParticleSystemComponentApi")]
    public static int RegisterParticleSystemComponentApi(
        delegate* unmanaged[Cdecl]<uint, int> getPlaying,
        delegate* unmanaged[Cdecl]<uint, int> getParticleCount,
        delegate* unmanaged[Cdecl]<uint, void> play,
        delegate* unmanaged[Cdecl]<uint, void> pause,
        delegate* unmanaged[Cdecl]<uint, int, void> stop,
        delegate* unmanaged[Cdecl]<uint, void> clear,
        delegate* unmanaged[Cdecl]<uint, int, void> emit,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, int, void> emitAt,
        delegate* unmanaged[Cdecl]<uint, nint> getAssetReference,
        delegate* unmanaged[Cdecl]<uint, nint, void> setAssetReference,
        delegate* unmanaged[Cdecl]<uint, int> getLooping,
        delegate* unmanaged[Cdecl]<uint, int, void> setLooping,
        delegate* unmanaged[Cdecl]<uint, int> getPlayOnAwake,
        delegate* unmanaged[Cdecl]<uint, int, void> setPlayOnAwake,
        delegate* unmanaged[Cdecl]<uint, float> getDuration,
        delegate* unmanaged[Cdecl]<uint, float, void> setDuration,
        delegate* unmanaged[Cdecl]<uint, float> getStartLifetime,
        delegate* unmanaged[Cdecl]<uint, float, void> setStartLifetime,
        delegate* unmanaged[Cdecl]<uint, float> getStartSpeed,
        delegate* unmanaged[Cdecl]<uint, float, void> setStartSpeed,
        delegate* unmanaged[Cdecl]<uint, float> getStartSize,
        delegate* unmanaged[Cdecl]<uint, float, void> setStartSize,
        delegate* unmanaged[Cdecl]<uint, float> getGravityModifier,
        delegate* unmanaged[Cdecl]<uint, float, void> setGravityModifier,
        delegate* unmanaged[Cdecl]<uint, float> getEmissionRate,
        delegate* unmanaged[Cdecl]<uint, float, void> setEmissionRate,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getStartColor,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setStartColor,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getShapeSize,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setShapeSize,
        delegate* unmanaged[Cdecl]<uint, int> getSimulationSpace,
        delegate* unmanaged[Cdecl]<uint, int, void> setSimulationSpace,
        delegate* unmanaged[Cdecl]<uint, int> getShape,
        delegate* unmanaged[Cdecl]<uint, int, void> setShape)
    {
        if (getPlaying == null || getParticleCount == null || play == null || pause == null || stop == null ||
            clear == null || emit == null || emitAt == null || getAssetReference == null || setAssetReference == null ||
            getLooping == null || setLooping == null ||
            getPlayOnAwake == null || setPlayOnAwake == null || getDuration == null || setDuration == null ||
            getStartLifetime == null || setStartLifetime == null || getStartSpeed == null || setStartSpeed == null ||
            getStartSize == null || setStartSize == null || getGravityModifier == null || setGravityModifier == null ||
            getEmissionRate == null || setEmissionRate == null || getStartColor == null || setStartColor == null ||
            getShapeSize == null || setShapeSize == null || getSimulationSpace == null || setSimulationSpace == null ||
            getShape == null || setShape == null)
        {
            SetError("Managed particle system component API registration received a null function pointer.");
            return 0;
        }

        _getParticleSystemPlaying = getPlaying;
        _getParticleSystemParticleCount = getParticleCount;
        _particleSystemPlay = play;
        _particleSystemPause = pause;
        _particleSystemStop = stop;
        _particleSystemClear = clear;
        _particleSystemEmit = emit;
        _particleSystemEmitAt = emitAt;
        _getParticleSystemAssetReference = getAssetReference;
        _setParticleSystemAssetReference = setAssetReference;
        _getParticleSystemLooping = getLooping;
        _setParticleSystemLooping = setLooping;
        _getParticleSystemPlayOnAwake = getPlayOnAwake;
        _setParticleSystemPlayOnAwake = setPlayOnAwake;
        _getParticleSystemDuration = getDuration;
        _setParticleSystemDuration = setDuration;
        _getParticleSystemStartLifetime = getStartLifetime;
        _setParticleSystemStartLifetime = setStartLifetime;
        _getParticleSystemStartSpeed = getStartSpeed;
        _setParticleSystemStartSpeed = setStartSpeed;
        _getParticleSystemStartSize = getStartSize;
        _setParticleSystemStartSize = setStartSize;
        _getParticleSystemGravityModifier = getGravityModifier;
        _setParticleSystemGravityModifier = setGravityModifier;
        _getParticleSystemEmissionRate = getEmissionRate;
        _setParticleSystemEmissionRate = setEmissionRate;
        _getParticleSystemStartColor = getStartColor;
        _setParticleSystemStartColor = setStartColor;
        _getParticleSystemShapeSize = getShapeSize;
        _setParticleSystemShapeSize = setShapeSize;
        _getParticleSystemSimulationSpace = getSimulationSpace;
        _setParticleSystemSimulationSpace = setSimulationSpace;
        _getParticleSystemShape = getShape;
        _setParticleSystemShape = setShape;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterSoundEmitterComponentApi")]
    public static int RegisterSoundEmitterComponentApi(
        delegate* unmanaged[Cdecl]<uint, int> getPlaying,
        delegate* unmanaged[Cdecl]<uint, void> play,
        delegate* unmanaged[Cdecl]<uint, void> playOneShot,
        delegate* unmanaged[Cdecl]<uint, float, float, void> playOneShotScaled,
        delegate* unmanaged[Cdecl]<uint, void> pause,
        delegate* unmanaged[Cdecl]<uint, void> stop,
        delegate* unmanaged[Cdecl]<uint, nint> getClipReference,
        delegate* unmanaged[Cdecl]<uint, nint, void> setClipReference,
        delegate* unmanaged[Cdecl]<uint, int> getLooping,
        delegate* unmanaged[Cdecl]<uint, int, void> setLooping,
        delegate* unmanaged[Cdecl]<uint, int> getSpatialized,
        delegate* unmanaged[Cdecl]<uint, int, void> setSpatialized,
        delegate* unmanaged[Cdecl]<uint, int> getPlayOnAwake,
        delegate* unmanaged[Cdecl]<uint, int, void> setPlayOnAwake,
        delegate* unmanaged[Cdecl]<uint, float> getVolume,
        delegate* unmanaged[Cdecl]<uint, float, void> setVolume,
        delegate* unmanaged[Cdecl]<uint, float> getPitch,
        delegate* unmanaged[Cdecl]<uint, float, void> setPitch)
    {
        if (getPlaying == null || play == null || playOneShot == null || playOneShotScaled == null || pause == null || stop == null ||
            getClipReference == null || setClipReference == null || getLooping == null || setLooping == null ||
            getSpatialized == null || setSpatialized == null || getPlayOnAwake == null || setPlayOnAwake == null ||
            getVolume == null || setVolume == null || getPitch == null || setPitch == null)
        {
            SetError("Managed sound emitter component API registration received a null function pointer.");
            return 0;
        }

        _getSoundEmitterPlaying = getPlaying;
        _soundEmitterPlay = play;
        _soundEmitterPlayOneShot = playOneShot;
        _soundEmitterPlayOneShotScaled = playOneShotScaled;
        _soundEmitterPause = pause;
        _soundEmitterStop = stop;
        _getSoundEmitterClipReference = getClipReference;
        _setSoundEmitterClipReference = setClipReference;
        _getSoundEmitterLooping = getLooping;
        _setSoundEmitterLooping = setLooping;
        _getSoundEmitterSpatialized = getSpatialized;
        _setSoundEmitterSpatialized = setSpatialized;
        _getSoundEmitterPlayOnAwake = getPlayOnAwake;
        _setSoundEmitterPlayOnAwake = setPlayOnAwake;
        _getSoundEmitterVolume = getVolume;
        _setSoundEmitterVolume = setVolume;
        _getSoundEmitterPitch = getPitch;
        _setSoundEmitterPitch = setPitch;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterRuntimeUIApi")]
    public static int RegisterRuntimeUIApi(
        delegate* unmanaged[Cdecl]<uint, float> getCanvasScaleFactor,
        delegate* unmanaged[Cdecl]<uint, float, void> setCanvasScaleFactor,
        delegate* unmanaged[Cdecl]<uint, int> getCanvasSortingOrder,
        delegate* unmanaged[Cdecl]<uint, int, void> setCanvasSortingOrder,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getRectAnchoredPosition,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setRectAnchoredPosition,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getRectSizeDelta,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setRectSizeDelta,
        delegate* unmanaged[Cdecl]<uint, int> getRectAnchorPreset,
        delegate* unmanaged[Cdecl]<uint, int, void> setRectAnchorPreset,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getUIImageColor,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setUIImageColor,
        delegate* unmanaged[Cdecl]<uint, float> getUIImageAlpha,
        delegate* unmanaged[Cdecl]<uint, float, void> setUIImageAlpha,
        delegate* unmanaged[Cdecl]<uint, nint> getUIImageTexture,
        delegate* unmanaged[Cdecl]<uint, nint, void> setUIImageTexture,
        delegate* unmanaged[Cdecl]<uint, int> getUIImagePreserveAspect,
        delegate* unmanaged[Cdecl]<uint, int, void> setUIImagePreserveAspect,
        delegate* unmanaged[Cdecl]<uint, float> getUIImageFillAmount,
        delegate* unmanaged[Cdecl]<uint, float, void> setUIImageFillAmount,
        delegate* unmanaged[Cdecl]<uint, nint> getUIText,
        delegate* unmanaged[Cdecl]<uint, nint, void> setUIText,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getUITextColor,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setUITextColor,
        delegate* unmanaged[Cdecl]<uint, float> getUITextFontSize,
        delegate* unmanaged[Cdecl]<uint, float, void> setUITextFontSize,
        delegate* unmanaged[Cdecl]<uint, int> getUIButtonInteractable,
        delegate* unmanaged[Cdecl]<uint, int, void> setUIButtonInteractable,
        delegate* unmanaged[Cdecl]<uint, int> getUIButtonHovered,
        delegate* unmanaged[Cdecl]<uint, int> getUIButtonPressed,
        delegate* unmanaged[Cdecl]<uint, int> getUIButtonReleased,
        delegate* unmanaged[Cdecl]<uint, int> getUIButtonClicked)
    {
        if (getCanvasScaleFactor == null || setCanvasScaleFactor == null ||
            getCanvasSortingOrder == null || setCanvasSortingOrder == null ||
            getRectAnchoredPosition == null || setRectAnchoredPosition == null ||
            getRectSizeDelta == null || setRectSizeDelta == null ||
            getRectAnchorPreset == null || setRectAnchorPreset == null ||
            getUIImageColor == null || setUIImageColor == null || getUIImageAlpha == null || setUIImageAlpha == null ||
            getUIImageTexture == null || setUIImageTexture == null ||
            getUIImagePreserveAspect == null || setUIImagePreserveAspect == null ||
            getUIImageFillAmount == null || setUIImageFillAmount == null ||
            getUIText == null || setUIText == null ||
            getUITextColor == null || setUITextColor == null ||
            getUITextFontSize == null || setUITextFontSize == null ||
            getUIButtonInteractable == null || setUIButtonInteractable == null ||
            getUIButtonHovered == null || getUIButtonPressed == null ||
            getUIButtonReleased == null || getUIButtonClicked == null)
        {
            SetError("Managed runtime UI API registration received a null function pointer.");
            return 0;
        }

        _getCanvasScaleFactor = getCanvasScaleFactor;
        _setCanvasScaleFactor = setCanvasScaleFactor;
        _getCanvasSortingOrder = getCanvasSortingOrder;
        _setCanvasSortingOrder = setCanvasSortingOrder;
        _getRectAnchoredPosition = getRectAnchoredPosition;
        _setRectAnchoredPosition = setRectAnchoredPosition;
        _getRectSizeDelta = getRectSizeDelta;
        _setRectSizeDelta = setRectSizeDelta;
        _getRectAnchorPreset = getRectAnchorPreset;
        _setRectAnchorPreset = setRectAnchorPreset;
        _getUIImageColor = getUIImageColor;
        _setUIImageColor = setUIImageColor;
        _getUIImageAlpha = getUIImageAlpha;
        _setUIImageAlpha = setUIImageAlpha;
        _getUIImageTexture = getUIImageTexture;
        _setUIImageTexture = setUIImageTexture;
        _getUIImagePreserveAspect = getUIImagePreserveAspect;
        _setUIImagePreserveAspect = setUIImagePreserveAspect;
        _getUIImageFillAmount = getUIImageFillAmount;
        _setUIImageFillAmount = setUIImageFillAmount;
        _getUIText = getUIText;
        _setUIText = setUIText;
        _getUITextColor = getUITextColor;
        _setUITextColor = setUITextColor;
        _getUITextFontSize = getUITextFontSize;
        _setUITextFontSize = setUITextFontSize;
        _getUIButtonInteractable = getUIButtonInteractable;
        _setUIButtonInteractable = setUIButtonInteractable;
        _getUIButtonHovered = getUIButtonHovered;
        _getUIButtonPressed = getUIButtonPressed;
        _getUIButtonReleased = getUIButtonReleased;
        _getUIButtonClicked = getUIButtonClicked;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterAdvancedUIApi")]
    public static int RegisterAdvancedUIApi(
        delegate* unmanaged[Cdecl]<uint, int> getCanvasScaleMode,
        delegate* unmanaged[Cdecl]<uint, int, void> setCanvasScaleMode,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getCanvasReferenceResolution,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setCanvasReferenceResolution,
        delegate* unmanaged[Cdecl]<uint, float> getRectRotation,
        delegate* unmanaged[Cdecl]<uint, float, void> setRectRotation,
        delegate* unmanaged[Cdecl]<uint, float> getRectOpacity,
        delegate* unmanaged[Cdecl]<uint, float, void> setRectOpacity,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getRectLocalScale,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setRectLocalScale,
        delegate* unmanaged[Cdecl]<uint, int> getRectLayoutMode,
        delegate* unmanaged[Cdecl]<uint, int, void> setRectLayoutMode,
        delegate* unmanaged[Cdecl]<uint, int> getUIImageType,
        delegate* unmanaged[Cdecl]<uint, int, void> setUIImageType,
        delegate* unmanaged[Cdecl]<uint, float> getUIImageThickness,
        delegate* unmanaged[Cdecl]<uint, float, void> setUIImageThickness,
        delegate* unmanaged[Cdecl]<uint, int> getUITextAlignment,
        delegate* unmanaged[Cdecl]<uint, int, void> setUITextAlignment,
        delegate* unmanaged[Cdecl]<ulong> getUIUpdateSequence)
    {
        _getCanvasScaleMode = getCanvasScaleMode;
        _setCanvasScaleMode = setCanvasScaleMode;
        _getCanvasReferenceResolution = getCanvasReferenceResolution;
        _setCanvasReferenceResolution = setCanvasReferenceResolution;
        _getRectRotation = getRectRotation;
        _setRectRotation = setRectRotation;
        _getRectOpacity = getRectOpacity;
        _setRectOpacity = setRectOpacity;
        _getRectLocalScale = getRectLocalScale;
        _setRectLocalScale = setRectLocalScale;
        _getRectLayoutMode = getRectLayoutMode;
        _setRectLayoutMode = setRectLayoutMode;
        _getUIImageType = getUIImageType;
        _setUIImageType = setUIImageType;
        _getUIImageThickness = getUIImageThickness;
        _setUIImageThickness = setUIImageThickness;
        _getUITextAlignment = getUITextAlignment;
        _setUITextAlignment = setUITextAlignment;
        _getUIUpdateSequence = getUIUpdateSequence;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterRmlUiApi")]
    public static int RegisterRmlUiApi(
        delegate* unmanaged[Cdecl]<nint, int, int> showDocument,
        delegate* unmanaged[Cdecl]<nint, int> reloadDocument,
        delegate* unmanaged[Cdecl]<nint, nint, nint, int> setText,
        delegate* unmanaged[Cdecl]<nint, nint, nint> getText,
        delegate* unmanaged[Cdecl]<nint, nint, nint, nint, int> setAttribute,
        delegate* unmanaged[Cdecl]<nint, nint, nint, nint> getAttribute,
        delegate* unmanaged[Cdecl]<nint, nint, nint, int, int> setClass,
        delegate* unmanaged[Cdecl]<nint, nint, nint, nint, int> setStyle,
        delegate* unmanaged[Cdecl]<nint, nint, nint, int> subscribeEvent,
        delegate* unmanaged[Cdecl]<nint, nint, nint, int> consumeEvent,
        delegate* unmanaged[Cdecl]<float> getSceneTimeScale,
        delegate* unmanaged[Cdecl]<float, void> setSceneTimeScale,
        delegate* unmanaged[Cdecl]<uint, nint> getRmlWidgetSource,
        delegate* unmanaged[Cdecl]<uint, nint, void> setRmlWidgetSource,
        delegate* unmanaged[Cdecl]<uint, int> getRmlWidgetVisible,
        delegate* unmanaged[Cdecl]<uint, int, void> setRmlWidgetVisible)
    {
        _rmlShowDocument = showDocument;
        _rmlReloadDocument = reloadDocument;
        _rmlSetText = setText;
        _rmlGetText = getText;
        _rmlSetAttribute = setAttribute;
        _rmlGetAttribute = getAttribute;
        _rmlSetClass = setClass;
        _rmlSetStyle = setStyle;
        _rmlSubscribeEvent = subscribeEvent;
        _rmlConsumeEvent = consumeEvent;
        _getSceneTimeScale = getSceneTimeScale;
        _setSceneTimeScale = setSceneTimeScale;
        _getRmlWidgetSource = getRmlWidgetSource;
        _setRmlWidgetSource = setRmlWidgetSource;
        _getRmlWidgetVisible = getRmlWidgetVisible;
        _setRmlWidgetVisible = setRmlWidgetVisible;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterInputApi")]
    public static int RegisterInputApi(
        delegate* unmanaged[Cdecl]<int, int> getKeyDown,
        delegate* unmanaged[Cdecl]<int, int> getKeyPressed,
        delegate* unmanaged[Cdecl]<int, int> getKeyReleased,
        delegate* unmanaged[Cdecl]<int, int> getMouseButtonDown,
        delegate* unmanaged[Cdecl]<int, int> getMouseButtonPressed,
        delegate* unmanaged[Cdecl]<int, int> getMouseButtonReleased,
        delegate* unmanaged[Cdecl]<NativeVector3> getMousePosition,
        delegate* unmanaged[Cdecl]<NativeVector3> getMouseDelta,
        delegate* unmanaged[Cdecl]<NativeVector3> getMouseScrollDelta,
        delegate* unmanaged[Cdecl]<int> getQuitRequested,
        delegate* unmanaged[Cdecl]<int> getCursorLocked,
        delegate* unmanaged[Cdecl]<int, void> setCursorLocked)
    {
        if (getKeyDown == null ||
            getKeyPressed == null ||
            getKeyReleased == null ||
            getMouseButtonDown == null ||
            getMouseButtonPressed == null ||
            getMouseButtonReleased == null ||
            getMousePosition == null ||
            getMouseDelta == null ||
            getMouseScrollDelta == null ||
            getQuitRequested == null ||
            getCursorLocked == null ||
            setCursorLocked == null)
        {
            SetError("Managed input API registration received a null function pointer.");
            return 0;
        }

        _getKeyDown = getKeyDown;
        _getKeyPressed = getKeyPressed;
        _getKeyReleased = getKeyReleased;
        _getMouseButtonDown = getMouseButtonDown;
        _getMouseButtonPressed = getMouseButtonPressed;
        _getMouseButtonReleased = getMouseButtonReleased;
        _getMousePosition = getMousePosition;
        _getMouseDelta = getMouseDelta;
        _getMouseScrollDelta = getMouseScrollDelta;
        _getQuitRequested = getQuitRequested;
        _getCursorLocked = getCursorLocked;
        _setCursorLocked = setCursorLocked;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterPhysicsApi")]
    public static int RegisterPhysicsApi(
        delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3, float, uint, NativeRaycastHit*, int> raycast,
        delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3, float, uint, nint, NativeRaycastHit*, int> raycastTagged,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, float, NativeVector3> moveKinematic,
        delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3, nint, NativeVector3, float, float, uint> spawnDecal)
    {
        if (raycast == null || raycastTagged == null || moveKinematic == null || spawnDecal == null)
        {
            SetError("Managed physics API registration received a null function pointer.");
            return 0;
        }

        _physicsRaycast = raycast;
        _physicsRaycastTagged = raycastTagged;
        _physicsMoveKinematic = moveKinematic;
        _spawnDecal = spawnDecal;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterNavigationApi")]
    public static int RegisterNavigationApi(
        delegate* unmanaged[Cdecl]<uint, NativeVector3, float, float, NativeVector3*, int> projectPoint,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, NativeVector3, float, float, NativeVector3*, int, int*, int> findPath)
    {
        if (projectPoint == null || findPath == null)
        {
            SetError("Managed navigation API registration received a null function pointer.");
            return 0;
        }
        _navigationProjectPoint = projectPoint;
        _navigationFindPath = findPath;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterDebugApi")]
    public static int RegisterDebugApi(delegate* unmanaged[Cdecl]<int, nint, void> logMessage)
    {
        if (logMessage == null)
        {
            SetError("Managed debug API registration received a null function pointer.");
            return 0;
        }

        _logMessage = logMessage;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "CreateScriptInstance")]
    public static long CreateScriptInstance(nint fullTypeNamePtr, uint entityId)
    {
        try
        {
            var fullTypeName = Marshal.PtrToStringUTF8(fullTypeNamePtr);
            if (string.IsNullOrWhiteSpace(fullTypeName))
            {
                SetError("Managed script type name was empty.");
                return 0;
            }

            lock (Gate)
            {
                if (!ScriptClasses.TryGetValue(fullTypeName, out var scriptClass))
                {
                    SetError($"Unknown managed script type '{fullTypeName}'.");
                    return 0;
                }

                if (Activator.CreateInstance(scriptClass.Type, nonPublic: true) is not ScriptBehaviour instance)
                {
                    SetError($"Failed to instantiate managed script type '{fullTypeName}'.");
                    return 0;
                }

                instance.EntityId = entityId;
                var handle = Interlocked.Increment(ref _nextInstanceHandle);
                Instances[handle] = instance;
                AddInstanceToEntityIndex(instance);
                return handle;
            }
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "DestroyScriptInstance")]
    public static void DestroyScriptInstance(long handle)
    {
        if (Instances.TryRemove(handle, out var instance))
        {
            lock (Gate)
            {
                RemoveInstanceFromEntityIndex(instance);
            }
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "UnloadScriptAssembly")]
    public static int UnloadScriptAssembly()
    {
        try
        {
            lock (Gate)
            {
                ResetLoadedAssembly();
                _lastError = string.Empty;
                return 1;
            }
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "InvokeOnCreate")]
    public static int InvokeOnCreate(long handle)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            instance.OnCreate();
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "InvokeOnUpdate")]
    public static int InvokeOnUpdate(long handle, float deltaTime)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            var uiUpdateSequence = GetUIUpdateSequence();
            UIButtonComponent.DispatchRegisteredEvents(uiUpdateSequence);
            RmlEvent.DispatchRegisteredEvents(uiUpdateSequence);
            instance.OnUpdate(deltaTime);
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "InvokeOnLateUpdate")]
    public static int InvokeOnLateUpdate(long handle, float deltaTime)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            instance.OnLateUpdate(deltaTime);
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "InvokeOnFixedUpdate")]
    public static int InvokeOnFixedUpdate(long handle, float fixedDeltaTime)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            instance.OnFixedUpdate(fixedDeltaTime);
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "InvokeOnDestroy")]
    public static int InvokeOnDestroy(long handle)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            instance.OnDestroy();
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "InvokeOnCollisionEnter")]
    public static int InvokeOnCollisionEnter(long handle, uint otherEntityId)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            instance.OnCollisionEnter(new GameObject(otherEntityId));
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "InvokeOnCollisionExit")]
    public static int InvokeOnCollisionExit(long handle, uint otherEntityId)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            instance.OnCollisionExit(new GameObject(otherEntityId));
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "InvokeOnAnimationEvent")]
    public static int InvokeOnAnimationEvent(long handle, nint namePtr, nint stringParameterPtr,
                                             float floatParameter, int intParameter)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            instance.OnAnimationEvent(new AnimationEvent(
                Marshal.PtrToStringUTF8(namePtr) ?? string.Empty,
                Marshal.PtrToStringUTF8(stringParameterPtr) ?? string.Empty,
                floatParameter,
                intParameter));
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "ApplyFieldData")]
    public static int ApplyFieldData(long handle, nint fieldDataPtr)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            var fieldData = Marshal.PtrToStringUTF8(fieldDataPtr) ?? string.Empty;
            if (!ScriptClasses.TryGetValue(instance.GetType().FullName ?? string.Empty, out var scriptClass))
            {
                SetError($"Managed script metadata missing for '{instance.GetType().FullName}'.");
                return 0;
            }

            ApplyFieldValues(instance, scriptClass, fieldData);
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "GetFieldData")]
    public static nint GetFieldData(long handle)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            if (!ScriptClasses.TryGetValue(instance.GetType().FullName ?? string.Empty, out var scriptClass))
            {
                SetError($"Managed script metadata missing for '{instance.GetType().FullName}'.");
                return 0;
            }

            _lastError = string.Empty;
            return Marshal.StringToCoTaskMemUTF8(BuildFieldData(instance, scriptClass));
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "SetEntityId")]
    public static int SetEntityId(long handle, uint entityId)
    {
        if (!Instances.TryGetValue(handle, out var instance))
        {
            SetError($"Unknown managed script instance handle '{handle}'.");
            return 0;
        }

        lock (Gate)
        {
            RemoveInstanceFromEntityIndex(instance);
            instance.EntityId = entityId;
            AddInstanceToEntityIndex(instance);
        }
        return 1;
    }

    internal static Vector3 GetEntityPosition(uint entityId)
    {
        return _getEntityPosition == null ? Vector3.Zero : _getEntityPosition(entityId).ToManaged();
    }

    internal static Vector3 GetEntityWorldPosition(uint entityId)
    {
        return _getEntityWorldPosition == null ? Vector3.Zero : _getEntityWorldPosition(entityId).ToManaged();
    }

    internal static void SetEntityWorldPosition(uint entityId, Vector3 position)
    {
        if (_setEntityWorldPosition == null)
        {
            return;
        }

        _setEntityWorldPosition(entityId, NativeVector3.FromManaged(position));
    }

    internal static void SetEntityPosition(uint entityId, Vector3 position)
    {
        if (_setEntityPosition == null)
        {
            return;
        }

        _setEntityPosition(entityId, NativeVector3.FromManaged(position));
    }

    internal static Vector3 GetEntityRotation(uint entityId)
    {
        return _getEntityRotation == null ? Vector3.Zero : _getEntityRotation(entityId).ToManaged();
    }

    internal static Vector3 GetEntityWorldRotation(uint entityId)
    {
        return _getEntityWorldRotation == null ? Vector3.Zero : _getEntityWorldRotation(entityId).ToManaged();
    }

    internal static void SetEntityRotation(uint entityId, Vector3 rotation)
    {
        if (_setEntityRotation == null)
        {
            return;
        }

        _setEntityRotation(entityId, NativeVector3.FromManaged(rotation));
    }

    internal static void SetEntityWorldRotation(uint entityId, Vector3 rotation)
    {
        if (_setEntityWorldRotation == null)
        {
            return;
        }

        _setEntityWorldRotation(entityId, NativeVector3.FromManaged(rotation));
    }

    internal static Quaternion GetEntityRotationQuaternion(uint entityId)
    {
        return _getEntityRotationQuaternion == null
            ? Quaternion.Identity
            : _getEntityRotationQuaternion(entityId).ToManaged();
    }

    internal static void SetEntityRotationQuaternion(uint entityId, Quaternion rotation)
    {
        if (_setEntityRotationQuaternion != null)
            _setEntityRotationQuaternion(entityId, NativeQuaternion.FromManaged(rotation));
    }

    internal static Vector3 GetEntityScale(uint entityId)
    {
        return _getEntityScale == null ? Vector3.One : _getEntityScale(entityId).ToManaged();
    }

    internal static Vector3 GetEntityForward(uint entityId)
    {
        return _getEntityForward == null ? -Vector3.UnitZ : _getEntityForward(entityId).ToManaged();
    }

    internal static Vector3 GetEntityRight(uint entityId)
    {
        return _getEntityRight == null ? Vector3.UnitX : _getEntityRight(entityId).ToManaged();
    }

    internal static void SetEntityScale(uint entityId, Vector3 scale)
    {
        if (_setEntityScale == null)
        {
            return;
        }

        _setEntityScale(entityId, NativeVector3.FromManaged(scale));
    }

    internal static bool GetEntityActive(uint entityId)
    {
        return _getEntityActive != null && _getEntityActive(entityId) != 0;
    }

    internal static void SetEntityActive(uint entityId, bool active)
    {
        if (_setEntityActive == null)
        {
            return;
        }

        _setEntityActive(entityId, active ? 1 : 0);
    }

    internal static string[] GetEntityTags(uint entityId)
    {
        if (_getEntityTagCount == null || _getEntityTag == null)
        {
            return [];
        }

        var tagCount = Math.Max(0, _getEntityTagCount(entityId));
        var tags = new string[tagCount];
        for (var index = 0; index < tagCount; ++index)
        {
            var tagPtr = _getEntityTag(entityId, index);
            tags[index] = tagPtr == 0 ? string.Empty : Marshal.PtrToStringUTF8(tagPtr) ?? string.Empty;
        }

        return tags;
    }

    internal static bool HasEntityTag(uint entityId, string tag)
    {
        if (string.IsNullOrEmpty(tag))
        {
            return false;
        }

        foreach (var candidate in GetEntityTags(entityId))
        {
            if (string.Equals(candidate, tag, StringComparison.Ordinal))
            {
                return true;
            }
        }

        return false;
    }

    internal static bool DestroyEntity(uint entityId)
    {
        return entityId != 0 && _destroyEntity != null && _destroyEntity(entityId) != 0;
    }

    internal static string GetEntityName(uint entityId)
    {
        return _getEntityName == null ? string.Empty : Marshal.PtrToStringUTF8(_getEntityName(entityId)) ?? string.Empty;
    }

    internal static uint FindEntityByName(string name)
    {
        if (_findEntityByName == null || string.IsNullOrWhiteSpace(name)) return 0;
        var bytes = Encoding.UTF8.GetBytes(name + '\0');
        fixed (byte* namePtr = bytes) return _findEntityByName(namePtr);
    }

    internal static uint[] FindEntitiesByTag(string tag)
    {
        if (_getEntityCountByTag == null || _getEntityByTag == null || string.IsNullOrWhiteSpace(tag)) return [];
        tag = tag.Trim();
        var bytes = Encoding.UTF8.GetBytes(tag + '\0');
        fixed (byte* tagPtr = bytes)
        {
            var count = Math.Max(0, _getEntityCountByTag(tagPtr));
            var entityIds = new uint[count];
            for (var index = 0; index < count; ++index) entityIds[index] = _getEntityByTag(tagPtr, index);
            return entityIds;
        }
    }

    internal static uint InstantiatePrefab(string prefabReference)
    {
        if (_instantiatePrefab == null || string.IsNullOrWhiteSpace(prefabReference))
        {
            return 0;
        }

        var referenceBytes = Encoding.UTF8.GetBytes(prefabReference + '\0');
        fixed (byte* referencePtr = referenceBytes)
        {
            return _instantiatePrefab(referencePtr);
        }
    }

    internal static bool PreloadPrefab(string prefabReference)
    {
        if (_preloadPrefab == null || string.IsNullOrWhiteSpace(prefabReference)) return false;
        var bytes = Encoding.UTF8.GetBytes(prefabReference.Trim() + '\0');
        fixed (byte* pointer = bytes) return _preloadPrefab(pointer) != 0;
    }

    internal static bool IsPrefabReady(string prefabReference)
    {
        if (_isPrefabReady == null || string.IsNullOrWhiteSpace(prefabReference)) return false;
        var bytes = Encoding.UTF8.GetBytes(prefabReference.Trim() + '\0');
        fixed (byte* pointer = bytes) return _isPrefabReady(pointer) != 0;
    }

    internal static bool LoadScene(string sceneAssetReference)
    {
        if (_loadScene == null || string.IsNullOrWhiteSpace(sceneAssetReference))
        {
            return false;
        }

        var referenceBytes = Encoding.UTF8.GetBytes(sceneAssetReference.Trim() + '\0');
        fixed (byte* referencePtr = referenceBytes)
        {
            return _loadScene(referencePtr) != 0;
        }
    }

    internal static void QuitApplication()
    {
        if (_quitApplication != null)
        {
            _quitApplication();
        }
    }

    internal static bool InvokeEntityMethod(uint entityId, string methodName, object?[]? args)
    {
        if (entityId == 0 || string.IsNullOrWhiteSpace(methodName))
        {
            return false;
        }

        args ??= [];
        var invoked = false;
        ScriptBehaviour[] entityInstances;
        lock (Gate)
        {
            if (!InstancesByEntity.TryGetValue(entityId, out var indexedInstances))
            {
                return false;
            }

            // Invoked script methods may create or destroy script instances.
            entityInstances = indexedInstances.ToArray();
        }

        foreach (var instance in entityInstances)
        {
            if (!TryFindInvokableMethod(instance.GetType(), methodName, args, out var method, out var convertedArgs))
            {
                continue;
            }

            try
            {
                method.Invoke(instance, convertedArgs);
                invoked = true;
            }
            catch (Exception ex)
            {
                SetError($"Failed to invoke method '{methodName}' on '{instance.GetType().FullName}': {ex.Message}");
            }
        }

        return invoked;
    }

    internal static T? GetScript<T>(uint entityId) where T : class
    {
        if (entityId == 0)
        {
            return null;
        }

        lock (Gate)
        {
            if (!InstancesByEntity.TryGetValue(entityId, out var entityInstances))
            {
                return null;
            }

            foreach (var instance in entityInstances)
            {
                if (instance is T script)
                {
                    return script;
                }
            }
        }

        return null;
    }

    private static bool TryFindInvokableMethod(Type instanceType, string methodName, object?[] args, out MethodInfo method, out object?[] convertedArgs)
    {
        var cacheKey = (instanceType, methodName, args.Length);
        if (!InvokableMethods.TryGetValue(cacheKey, out var candidates))
        {
            candidates = instanceType
                .GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
                .Where(candidate =>
                    string.Equals(candidate.Name, methodName, StringComparison.Ordinal) &&
                    candidate.ReturnType == typeof(void) &&
                    candidate.GetParameters().Length == args.Length)
                .ToArray();
            InvokableMethods[cacheKey] = candidates;
        }

        foreach (var candidate in candidates)
        {
            var parameters = candidate.GetParameters();
            var currentArgs = new object?[args.Length];
            var compatible = true;
            for (var index = 0; index < parameters.Length; ++index)
            {
                if (!TryConvertInvokeArgument(args[index], parameters[index].ParameterType, out currentArgs[index]))
                {
                    compatible = false;
                    break;
                }
            }

            if (!compatible)
            {
                continue;
            }

            method = candidate;
            convertedArgs = currentArgs;
            return true;
        }

        method = null!;
        convertedArgs = [];
        return false;
    }

    private static bool TryConvertInvokeArgument(object? value, Type targetType, out object? convertedValue)
    {
        var nullableType = Nullable.GetUnderlyingType(targetType);
        var effectiveTargetType = nullableType ?? targetType;

        if (value is null)
        {
            convertedValue = null;
            return !effectiveTargetType.IsValueType || nullableType is not null;
        }

        if (effectiveTargetType.IsInstanceOfType(value))
        {
            convertedValue = value;
            return true;
        }

        if (effectiveTargetType == typeof(GameObject) && value is uint entityId)
        {
            convertedValue = new GameObject(entityId);
            return true;
        }

        try
        {
            if (effectiveTargetType.IsEnum)
            {
                convertedValue = value is string enumName
                    ? Enum.Parse(effectiveTargetType, enumName, ignoreCase: true)
                    : Enum.ToObject(effectiveTargetType, value);
                return true;
            }

            convertedValue = Convert.ChangeType(value, effectiveTargetType, CultureInfo.InvariantCulture);
            return true;
        }
        catch
        {
            convertedValue = null;
            return false;
        }
    }

    internal static bool HasComponent(uint entityId, NativeComponentType componentType)
    {
        return _hasComponent != null && _hasComponent(entityId, (int)componentType) != 0;
    }

    internal static bool GetComponentEnabled(uint entityId, NativeComponentType componentType)
    {
        return _getComponentEnabled != null && _getComponentEnabled(entityId, (int)componentType) != 0;
    }

    internal static void SetComponentEnabled(uint entityId, NativeComponentType componentType, bool enabled)
    {
        if (_setComponentEnabled == null)
        {
            return;
        }

        _setComponentEnabled(entityId, (int)componentType, enabled ? 1 : 0);
    }

    internal static bool GetCameraMain(uint entityId)
    {
        return _getCameraMain != null && _getCameraMain(entityId) != 0;
    }

    internal static void SetCameraMain(uint entityId, bool isMain)
    {
        if (_setCameraMain == null)
        {
            return;
        }

        _setCameraMain(entityId, isMain ? 1 : 0);
    }

    internal static float GetCameraFov(uint entityId)
    {
        return _getCameraFov == null ? 0.0f : _getCameraFov(entityId);
    }

    internal static void SetCameraFov(uint entityId, float fov)
    {
        if (_setCameraFov == null)
        {
            return;
        }

        _setCameraFov(entityId, fov);
    }

    internal static float GetLightIntensity(uint entityId)
    {
        return _getLightIntensity == null ? 0.0f : _getLightIntensity(entityId);
    }

    internal static void SetLightIntensity(uint entityId, float intensity)
    {
        if (_setLightIntensity == null)
        {
            return;
        }

        _setLightIntensity(entityId, intensity);
    }

    internal static Vector3 GetLightColor(uint entityId)
    {
        return _getLightColor == null ? Vector3.Zero : _getLightColor(entityId).ToManaged();
    }

    internal static void SetLightColor(uint entityId, Vector3 color)
    {
        if (_setLightColor == null)
        {
            return;
        }

        _setLightColor(entityId, NativeVector3.FromManaged(color));
    }

    internal static bool GetMeshStatic(uint entityId)
    {
        return _getMeshStatic != null && _getMeshStatic(entityId) != 0;
    }

    internal static void SetMeshStatic(uint entityId, bool isStatic)
    {
        if (_setMeshStatic == null)
        {
            return;
        }

        _setMeshStatic(entityId, isStatic ? 1 : 0);
    }

    internal static Vector3 GetMeshColor(uint entityId)
    {
        return _getMeshColor == null ? Vector3.One : _getMeshColor(entityId).ToManaged();
    }

    internal static void SetMeshColor(uint entityId, Vector3 color)
    {
        if (_setMeshColor == null)
        {
            return;
        }

        _setMeshColor(entityId, NativeVector3.FromManaged(color));
    }

    internal static Vector3 GetMeshEmission(uint entityId)
    {
        return _getMeshEmission == null ? Vector3.Zero : _getMeshEmission(entityId).ToManaged();
    }

    internal static void SetMeshEmission(uint entityId, Vector3 emission)
    {
        if (_setMeshEmission == null)
        {
            return;
        }

        _setMeshEmission(entityId, NativeVector3.FromManaged(emission));
    }

    internal static int GetAnimationClipCount(uint entityId) => _getAnimationClipCount == null ? 0 : _getAnimationClipCount(entityId);
    internal static int GetAnimationClipIndex(uint entityId) => _getAnimationClipIndex == null ? 0 : _getAnimationClipIndex(entityId);
    internal static void SetAnimationClipIndex(uint entityId, int value) { if (_setAnimationClipIndex != null) _setAnimationClipIndex(entityId, value); }
    internal static string GetAnimationClipName(uint entityId, int clipIndex)
    {
        if (_getAnimationClipName == null)
        {
            return string.Empty;
        }

        return Marshal.PtrToStringUTF8(_getAnimationClipName(entityId, clipIndex)) ?? string.Empty;
    }
    internal static float GetAnimationClipDuration(uint entityId, int clipIndex) => _getAnimationClipDuration == null ? 0.0f : _getAnimationClipDuration(entityId, clipIndex);
    internal static bool GetAnimationPlaying(uint entityId) => _getAnimationPlaying != null && _getAnimationPlaying(entityId) != 0;
    internal static void SetAnimationPlaying(uint entityId, bool value) { if (_setAnimationPlaying != null) _setAnimationPlaying(entityId, value ? 1 : 0); }
    internal static bool GetAnimationLooping(uint entityId) => _getAnimationLooping != null && _getAnimationLooping(entityId) != 0;
    internal static void SetAnimationLooping(uint entityId, bool value) { if (_setAnimationLooping != null) _setAnimationLooping(entityId, value ? 1 : 0); }
    internal static bool GetAnimationAutoplay(uint entityId) => _getAnimationAutoplay != null && _getAnimationAutoplay(entityId) != 0;
    internal static void SetAnimationAutoplay(uint entityId, bool value) { if (_setAnimationAutoplay != null) _setAnimationAutoplay(entityId, value ? 1 : 0); }
    internal static float GetAnimationSpeed(uint entityId) => _getAnimationSpeed == null ? 0.0f : _getAnimationSpeed(entityId);
    internal static void SetAnimationSpeed(uint entityId, float value) { if (_setAnimationSpeed != null) _setAnimationSpeed(entityId, value); }
    internal static float GetAnimationTime(uint entityId) => _getAnimationTime == null ? 0.0f : _getAnimationTime(entityId);
    internal static void SetAnimationTime(uint entityId, float value) { if (_setAnimationTime != null) _setAnimationTime(entityId, value); }
    internal static void AnimationPlay(uint entityId) { if (_animationPlay != null) _animationPlay(entityId); }
    internal static void AnimationPause(uint entityId) { if (_animationPause != null) _animationPause(entityId); }
    internal static void AnimationStop(uint entityId) { if (_animationStop != null) _animationStop(entityId); }
    internal static bool GetRagdollEnabled(uint entityId) => _getRagdollEnabled != null && _getRagdollEnabled(entityId) != 0;
    internal static void SetRagdollEnabled(uint entityId, bool value) { if (_setRagdollEnabled != null) _setRagdollEnabled(entityId, value ? 1 : 0); }
    internal static float GetRagdollWeight(uint entityId) => _getRagdollWeight == null ? 0.0f : _getRagdollWeight(entityId);
    internal static void SetRagdollWeight(uint entityId, float value) { if (_setRagdollWeight != null) _setRagdollWeight(entityId, value); }
    internal static void AddRagdollImpulse(uint entityId, Vector3 impulse) { if (_addRagdollImpulse != null) _addRagdollImpulse(entityId, NativeVector3.FromManaged(impulse)); }
    internal static void ResetRagdoll(uint entityId) { if (_resetRagdoll != null) _resetRagdoll(entityId); }
    internal static float GetActiveRagdollPositionStrength(uint entityId) => _getActiveRagdollPositionStrength == null ? 0.0f : _getActiveRagdollPositionStrength(entityId);
    internal static void SetActiveRagdollPositionStrength(uint entityId, float value) { if (_setActiveRagdollPositionStrength != null) _setActiveRagdollPositionStrength(entityId, value); }
    internal static float GetActiveRagdollRotationStrength(uint entityId) => _getActiveRagdollRotationStrength == null ? 0.0f : _getActiveRagdollRotationStrength(entityId);
    internal static void SetActiveRagdollRotationStrength(uint entityId, float value) { if (_setActiveRagdollRotationStrength != null) _setActiveRagdollRotationStrength(entityId, value); }
    internal static float GetActiveRagdollDamping(uint entityId) => _getActiveRagdollDamping == null ? 0.0f : _getActiveRagdollDamping(entityId);
    internal static void SetActiveRagdollDamping(uint entityId, float value) { if (_setActiveRagdollDamping != null) _setActiveRagdollDamping(entityId, value); }
    internal static void SetAnimationBoolParameter(uint entityId, string name, bool value)
    {
        if (_setAnimationBoolParameter == null)
            return;

        var nameBytes = Encoding.UTF8.GetBytes((name ?? string.Empty) + '\0');
        fixed (byte* namePtr = nameBytes)
        {
            _setAnimationBoolParameter(entityId, namePtr, value ? 1 : 0);
        }
    }
    internal static void SetAnimationFloatParameter(uint entityId, string name, float value)
    {
        if (_setAnimationFloatParameter == null)
            return;

        var nameBytes = Encoding.UTF8.GetBytes((name ?? string.Empty) + '\0');
        fixed (byte* namePtr = nameBytes)
        {
            _setAnimationFloatParameter(entityId, namePtr, value);
        }
    }
    internal static void SetAnimationIntParameter(uint entityId, string name, int value)
    {
        if (_setAnimationIntParameter == null)
            return;

        var nameBytes = Encoding.UTF8.GetBytes((name ?? string.Empty) + '\0');
        fixed (byte* namePtr = nameBytes)
        {
            _setAnimationIntParameter(entityId, namePtr, value);
        }
    }
    internal static void SetAnimationTriggerParameter(uint entityId, string name)
    {
        if (_setAnimationTriggerParameter == null)
            return;

        var nameBytes = Encoding.UTF8.GetBytes((name ?? string.Empty) + '\0');
        fixed (byte* namePtr = nameBytes)
        {
            _setAnimationTriggerParameter(entityId, namePtr);
        }
    }
    internal static void ResetAnimationTriggerParameter(uint entityId, string name)
    {
        if (_resetAnimationTriggerParameter == null)
            return;

        var nameBytes = Encoding.UTF8.GetBytes((name ?? string.Empty) + '\0');
        fixed (byte* namePtr = nameBytes)
        {
            _resetAnimationTriggerParameter(entityId, namePtr);
        }
    }
    internal static void AnimationPlayState(uint entityId, string name)
    {
        if (_animationPlayState == null)
            return;

        var nameBytes = Encoding.UTF8.GetBytes((name ?? string.Empty) + '\0');
        fixed (byte* namePtr = nameBytes)
        {
            _animationPlayState(entityId, namePtr);
        }
    }

    internal static float GetRigidbodyMass(uint entityId) => _getRigidbodyMass == null ? 0.0f : _getRigidbodyMass(entityId);
    internal static void SetRigidbodyMass(uint entityId, float value) { if (_setRigidbodyMass != null) _setRigidbodyMass(entityId, value); }
    internal static float GetRigidbodyLinearDrag(uint entityId) => _getRigidbodyLinearDrag == null ? 0.0f : _getRigidbodyLinearDrag(entityId);
    internal static void SetRigidbodyLinearDrag(uint entityId, float value) { if (_setRigidbodyLinearDrag != null) _setRigidbodyLinearDrag(entityId, value); }
    internal static float GetRigidbodyAngularDrag(uint entityId) => _getRigidbodyAngularDrag == null ? 0.0f : _getRigidbodyAngularDrag(entityId);
    internal static void SetRigidbodyAngularDrag(uint entityId, float value) { if (_setRigidbodyAngularDrag != null) _setRigidbodyAngularDrag(entityId, value); }
    internal static float GetRigidbodyFriction(uint entityId) => _getRigidbodyFriction == null ? 0.0f : _getRigidbodyFriction(entityId);
    internal static void SetRigidbodyFriction(uint entityId, float value) { if (_setRigidbodyFriction != null) _setRigidbodyFriction(entityId, value); }
    internal static bool GetRigidbodyUseGravity(uint entityId) => _getRigidbodyUseGravity != null && _getRigidbodyUseGravity(entityId) != 0;
    internal static void SetRigidbodyUseGravity(uint entityId, bool value) { if (_setRigidbodyUseGravity != null) _setRigidbodyUseGravity(entityId, value ? 1 : 0); }
    internal static bool GetRigidbodyKinematic(uint entityId) => _getRigidbodyKinematic != null && _getRigidbodyKinematic(entityId) != 0;
    internal static void SetRigidbodyKinematic(uint entityId, bool value) { if (_setRigidbodyKinematic != null) _setRigidbodyKinematic(entityId, value ? 1 : 0); }
    internal static bool GetRigidbodyFreezeRotation(uint entityId) => _getRigidbodyFreezeRotation != null && _getRigidbodyFreezeRotation(entityId) != 0;
    internal static void SetRigidbodyFreezeRotation(uint entityId, bool value) { if (_setRigidbodyFreezeRotation != null) _setRigidbodyFreezeRotation(entityId, value ? 1 : 0); }
    internal static Vector3 GetRigidbodyVelocity(uint entityId) => _getRigidbodyVelocity == null ? Vector3.Zero : _getRigidbodyVelocity(entityId).ToManaged();
    internal static void SetRigidbodyVelocity(uint entityId, Vector3 value) { if (_setRigidbodyVelocity != null) _setRigidbodyVelocity(entityId, NativeVector3.FromManaged(value)); }
    internal static Vector3 GetRigidbodyAngularVelocity(uint entityId) => _getRigidbodyAngularVelocity == null ? Vector3.Zero : _getRigidbodyAngularVelocity(entityId).ToManaged();
    internal static void SetRigidbodyAngularVelocity(uint entityId, Vector3 value) { if (_setRigidbodyAngularVelocity != null) _setRigidbodyAngularVelocity(entityId, NativeVector3.FromManaged(value)); }
    internal static void AddRigidbodyForce(uint entityId, Vector3 value) { if (_addRigidbodyForce != null) _addRigidbodyForce(entityId, NativeVector3.FromManaged(value)); }
    internal static void AddRigidbodyImpulse(uint entityId, Vector3 value) { if (_addRigidbodyImpulse != null) _addRigidbodyImpulse(entityId, NativeVector3.FromManaged(value)); }
    internal static void AddRigidbodyForceAtPosition(uint entityId, Vector3 value, Vector3 worldPosition) { if (_addRigidbodyForceAtPosition != null) _addRigidbodyForceAtPosition(entityId, NativeVector3.FromManaged(value), NativeVector3.FromManaged(worldPosition)); }
    internal static void AddRigidbodyImpulseAtPosition(uint entityId, Vector3 value, Vector3 worldPosition) { if (_addRigidbodyImpulseAtPosition != null) _addRigidbodyImpulseAtPosition(entityId, NativeVector3.FromManaged(value), NativeVector3.FromManaged(worldPosition)); }

    internal static int GetColliderShape(uint entityId) => _getColliderShape == null ? 0 : _getColliderShape(entityId);
    internal static void SetColliderShape(uint entityId, int value) { if (_setColliderShape != null) _setColliderShape(entityId, value); }
    internal static Vector3 GetColliderCenter(uint entityId) => _getColliderCenter == null ? Vector3.Zero : _getColliderCenter(entityId).ToManaged();
    internal static void SetColliderCenter(uint entityId, Vector3 value) { if (_setColliderCenter != null) _setColliderCenter(entityId, NativeVector3.FromManaged(value)); }
    internal static Vector3 GetColliderSize(uint entityId) => _getColliderSize == null ? Vector3.One : _getColliderSize(entityId).ToManaged();
    internal static void SetColliderSize(uint entityId, Vector3 value) { if (_setColliderSize != null) _setColliderSize(entityId, NativeVector3.FromManaged(value)); }
    internal static float GetColliderRadius(uint entityId) => _getColliderRadius == null ? 0.0f : _getColliderRadius(entityId);
    internal static void SetColliderRadius(uint entityId, float value) { if (_setColliderRadius != null) _setColliderRadius(entityId, value); }
    internal static float GetColliderHeight(uint entityId) => _getColliderHeight == null ? 0.0f : _getColliderHeight(entityId);
    internal static void SetColliderHeight(uint entityId, float value) { if (_setColliderHeight != null) _setColliderHeight(entityId, value); }
    internal static bool GetColliderTrigger(uint entityId) => _getColliderTrigger != null && _getColliderTrigger(entityId) != 0;
    internal static void SetColliderTrigger(uint entityId, bool value) { if (_setColliderTrigger != null) _setColliderTrigger(entityId, value ? 1 : 0); }
    internal static bool GetColliderBlocksAudio(uint entityId) => _getColliderBlocksAudio != null && _getColliderBlocksAudio(entityId) != 0;
    internal static void SetColliderBlocksAudio(uint entityId, bool value) { if (_setColliderBlocksAudio != null) _setColliderBlocksAudio(entityId, value ? 1 : 0); }

    internal static bool GetParticleSystemPlaying(uint entityId) => _getParticleSystemPlaying != null && _getParticleSystemPlaying(entityId) != 0;
    internal static int GetParticleSystemParticleCount(uint entityId) => _getParticleSystemParticleCount == null ? 0 : _getParticleSystemParticleCount(entityId);
    internal static void ParticleSystemPlay(uint entityId) { if (_particleSystemPlay != null) _particleSystemPlay(entityId); }
    internal static void ParticleSystemPause(uint entityId) { if (_particleSystemPause != null) _particleSystemPause(entityId); }
    internal static void ParticleSystemStop(uint entityId, bool clear) { if (_particleSystemStop != null) _particleSystemStop(entityId, clear ? 1 : 0); }
    internal static void ParticleSystemClear(uint entityId) { if (_particleSystemClear != null) _particleSystemClear(entityId); }
    internal static void ParticleSystemEmit(uint entityId, int count) { if (_particleSystemEmit != null) _particleSystemEmit(entityId, count); }
    internal static void ParticleSystemEmitAt(uint entityId, Vector3 worldPosition, int count)
    {
        if (_particleSystemEmitAt != null)
        {
            _particleSystemEmitAt(entityId, NativeVector3.FromManaged(worldPosition), count);
        }
    }
    internal static string GetParticleSystemAssetReference(uint entityId)
    {
        if (_getParticleSystemAssetReference == null)
        {
            return string.Empty;
        }

        return Marshal.PtrToStringUTF8(_getParticleSystemAssetReference(entityId)) ?? string.Empty;
    }
    internal static void SetParticleSystemAssetReference(uint entityId, string value)
    {
        if (_setParticleSystemAssetReference == null)
        {
            return;
        }

        var valueBytes = Encoding.UTF8.GetBytes((value ?? string.Empty) + '\0');
        fixed (byte* valuePtr = valueBytes)
        {
            _setParticleSystemAssetReference(entityId, (nint)valuePtr);
        }
    }
    internal static bool GetParticleSystemLooping(uint entityId) => _getParticleSystemLooping != null && _getParticleSystemLooping(entityId) != 0;
    internal static void SetParticleSystemLooping(uint entityId, bool value) { if (_setParticleSystemLooping != null) _setParticleSystemLooping(entityId, value ? 1 : 0); }
    internal static bool GetParticleSystemPlayOnAwake(uint entityId) => _getParticleSystemPlayOnAwake != null && _getParticleSystemPlayOnAwake(entityId) != 0;
    internal static void SetParticleSystemPlayOnAwake(uint entityId, bool value) { if (_setParticleSystemPlayOnAwake != null) _setParticleSystemPlayOnAwake(entityId, value ? 1 : 0); }
    internal static float GetParticleSystemDuration(uint entityId) => _getParticleSystemDuration == null ? 0.0f : _getParticleSystemDuration(entityId);
    internal static void SetParticleSystemDuration(uint entityId, float value) { if (_setParticleSystemDuration != null) _setParticleSystemDuration(entityId, value); }
    internal static float GetParticleSystemStartLifetime(uint entityId) => _getParticleSystemStartLifetime == null ? 0.0f : _getParticleSystemStartLifetime(entityId);
    internal static void SetParticleSystemStartLifetime(uint entityId, float value) { if (_setParticleSystemStartLifetime != null) _setParticleSystemStartLifetime(entityId, value); }
    internal static float GetParticleSystemStartSpeed(uint entityId) => _getParticleSystemStartSpeed == null ? 0.0f : _getParticleSystemStartSpeed(entityId);
    internal static void SetParticleSystemStartSpeed(uint entityId, float value) { if (_setParticleSystemStartSpeed != null) _setParticleSystemStartSpeed(entityId, value); }
    internal static float GetParticleSystemStartSize(uint entityId) => _getParticleSystemStartSize == null ? 0.0f : _getParticleSystemStartSize(entityId);
    internal static void SetParticleSystemStartSize(uint entityId, float value) { if (_setParticleSystemStartSize != null) _setParticleSystemStartSize(entityId, value); }
    internal static float GetParticleSystemGravityModifier(uint entityId) => _getParticleSystemGravityModifier == null ? 0.0f : _getParticleSystemGravityModifier(entityId);
    internal static void SetParticleSystemGravityModifier(uint entityId, float value) { if (_setParticleSystemGravityModifier != null) _setParticleSystemGravityModifier(entityId, value); }
    internal static float GetParticleSystemEmissionRate(uint entityId) => _getParticleSystemEmissionRate == null ? 0.0f : _getParticleSystemEmissionRate(entityId);
    internal static void SetParticleSystemEmissionRate(uint entityId, float value) { if (_setParticleSystemEmissionRate != null) _setParticleSystemEmissionRate(entityId, value); }
    internal static Vector3 GetParticleSystemStartColor(uint entityId) => _getParticleSystemStartColor == null ? Vector3.One : _getParticleSystemStartColor(entityId).ToManaged();
    internal static void SetParticleSystemStartColor(uint entityId, Vector3 value) { if (_setParticleSystemStartColor != null) _setParticleSystemStartColor(entityId, NativeVector3.FromManaged(value)); }
    internal static Vector3 GetParticleSystemShapeSize(uint entityId) => _getParticleSystemShapeSize == null ? Vector3.One : _getParticleSystemShapeSize(entityId).ToManaged();
    internal static void SetParticleSystemShapeSize(uint entityId, Vector3 value) { if (_setParticleSystemShapeSize != null) _setParticleSystemShapeSize(entityId, NativeVector3.FromManaged(value)); }
    internal static int GetParticleSystemSimulationSpace(uint entityId) => _getParticleSystemSimulationSpace == null ? 0 : _getParticleSystemSimulationSpace(entityId);
    internal static void SetParticleSystemSimulationSpace(uint entityId, int value) { if (_setParticleSystemSimulationSpace != null) _setParticleSystemSimulationSpace(entityId, value); }
    internal static int GetParticleSystemShape(uint entityId) => _getParticleSystemShape == null ? 0 : _getParticleSystemShape(entityId);
    internal static void SetParticleSystemShape(uint entityId, int value) { if (_setParticleSystemShape != null) _setParticleSystemShape(entityId, value); }

    internal static bool GetSoundEmitterPlaying(uint entityId) => _getSoundEmitterPlaying != null && _getSoundEmitterPlaying(entityId) != 0;
    internal static void SoundEmitterPlay(uint entityId) { if (_soundEmitterPlay != null) _soundEmitterPlay(entityId); }
    internal static void SoundEmitterPlayOneShot(uint entityId) { if (_soundEmitterPlayOneShot != null) _soundEmitterPlayOneShot(entityId); }
    internal static void SoundEmitterPlayOneShot(uint entityId, float volumeScale, float pitchScale) { if (_soundEmitterPlayOneShotScaled != null) _soundEmitterPlayOneShotScaled(entityId, volumeScale, pitchScale); }
    internal static void SoundEmitterPause(uint entityId) { if (_soundEmitterPause != null) _soundEmitterPause(entityId); }
    internal static void SoundEmitterStop(uint entityId) { if (_soundEmitterStop != null) _soundEmitterStop(entityId); }
    internal static string GetSoundEmitterClipReference(uint entityId)
    {
        if (_getSoundEmitterClipReference == null)
        {
            return string.Empty;
        }

        return Marshal.PtrToStringUTF8(_getSoundEmitterClipReference(entityId)) ?? string.Empty;
    }
    internal static void SetSoundEmitterClipReference(uint entityId, string value)
    {
        if (_setSoundEmitterClipReference == null)
        {
            return;
        }

        var valueBytes = Encoding.UTF8.GetBytes((value ?? string.Empty) + '\0');
        fixed (byte* valuePtr = valueBytes)
        {
            _setSoundEmitterClipReference(entityId, (nint)valuePtr);
        }
    }
    internal static bool GetSoundEmitterLooping(uint entityId) => _getSoundEmitterLooping != null && _getSoundEmitterLooping(entityId) != 0;
    internal static void SetSoundEmitterLooping(uint entityId, bool value) { if (_setSoundEmitterLooping != null) _setSoundEmitterLooping(entityId, value ? 1 : 0); }
    internal static bool GetSoundEmitterSpatialized(uint entityId) => _getSoundEmitterSpatialized != null && _getSoundEmitterSpatialized(entityId) != 0;
    internal static void SetSoundEmitterSpatialized(uint entityId, bool value) { if (_setSoundEmitterSpatialized != null) _setSoundEmitterSpatialized(entityId, value ? 1 : 0); }
    internal static bool GetSoundEmitterPlayOnAwake(uint entityId) => _getSoundEmitterPlayOnAwake != null && _getSoundEmitterPlayOnAwake(entityId) != 0;
    internal static void SetSoundEmitterPlayOnAwake(uint entityId, bool value) { if (_setSoundEmitterPlayOnAwake != null) _setSoundEmitterPlayOnAwake(entityId, value ? 1 : 0); }
    internal static float GetSoundEmitterVolume(uint entityId) => _getSoundEmitterVolume == null ? 1.0f : _getSoundEmitterVolume(entityId);
    internal static void SetSoundEmitterVolume(uint entityId, float value) { if (_setSoundEmitterVolume != null) _setSoundEmitterVolume(entityId, value); }
    internal static float GetSoundEmitterPitch(uint entityId) => _getSoundEmitterPitch == null ? 1.0f : _getSoundEmitterPitch(entityId);
    internal static void SetSoundEmitterPitch(uint entityId, float value) { if (_setSoundEmitterPitch != null) _setSoundEmitterPitch(entityId, value); }

    internal static float GetCanvasScaleFactor(uint entityId) => _getCanvasScaleFactor == null ? 1.0f : _getCanvasScaleFactor(entityId);
    internal static void SetCanvasScaleFactor(uint entityId, float value) { if (_setCanvasScaleFactor != null) _setCanvasScaleFactor(entityId, value); }
    internal static int GetCanvasSortingOrder(uint entityId) => _getCanvasSortingOrder == null ? 0 : _getCanvasSortingOrder(entityId);
    internal static void SetCanvasSortingOrder(uint entityId, int value) { if (_setCanvasSortingOrder != null) _setCanvasSortingOrder(entityId, value); }

    internal static Vector2 GetRectAnchoredPosition(uint entityId)
    {
        var value = _getRectAnchoredPosition == null ? new NativeVector3() : _getRectAnchoredPosition(entityId);
        return new Vector2(value.X, value.Y);
    }

    internal static void SetRectAnchoredPosition(uint entityId, Vector2 value)
    {
        if (_setRectAnchoredPosition != null) _setRectAnchoredPosition(entityId, new NativeVector3(value.X, value.Y, 0.0f));
    }

    internal static Vector2 GetRectSizeDelta(uint entityId)
    {
        var value = _getRectSizeDelta == null ? new NativeVector3() : _getRectSizeDelta(entityId);
        return new Vector2(value.X, value.Y);
    }

    internal static void SetRectSizeDelta(uint entityId, Vector2 value)
    {
        if (_setRectSizeDelta != null) _setRectSizeDelta(entityId, new NativeVector3(value.X, value.Y, 0.0f));
    }

    internal static int GetRectAnchorPreset(uint entityId) => _getRectAnchorPreset == null ? 4 : _getRectAnchorPreset(entityId);
    internal static void SetRectAnchorPreset(uint entityId, int value) { if (_setRectAnchorPreset != null) _setRectAnchorPreset(entityId, value); }
    internal static int GetCanvasScaleMode(uint entityId) => _getCanvasScaleMode == null ? 0 : _getCanvasScaleMode(entityId);
    internal static void SetCanvasScaleMode(uint entityId, int value) { if (_setCanvasScaleMode != null) _setCanvasScaleMode(entityId, value); }
    internal static Vector2 GetCanvasReferenceResolution(uint entityId)
    {
        var value = _getCanvasReferenceResolution == null ? new NativeVector3(1920.0f, 1080.0f, 0.0f) : _getCanvasReferenceResolution(entityId);
        return new Vector2(value.X, value.Y);
    }
    internal static void SetCanvasReferenceResolution(uint entityId, Vector2 value)
    {
        if (_setCanvasReferenceResolution != null) _setCanvasReferenceResolution(entityId, new NativeVector3(value.X, value.Y, 0.0f));
    }
    internal static float GetRectRotation(uint entityId) => _getRectRotation == null ? 0.0f : _getRectRotation(entityId);
    internal static void SetRectRotation(uint entityId, float value) { if (_setRectRotation != null) _setRectRotation(entityId, value); }
    internal static float GetRectOpacity(uint entityId) => _getRectOpacity == null ? 1.0f : _getRectOpacity(entityId);
    internal static void SetRectOpacity(uint entityId, float value) { if (_setRectOpacity != null) _setRectOpacity(entityId, value); }
    internal static Vector2 GetRectLocalScale(uint entityId)
    {
        var value = _getRectLocalScale == null ? new NativeVector3(1.0f, 1.0f, 0.0f) : _getRectLocalScale(entityId);
        return new Vector2(value.X, value.Y);
    }
    internal static void SetRectLocalScale(uint entityId, Vector2 value)
    {
        if (_setRectLocalScale != null) _setRectLocalScale(entityId, new NativeVector3(value.X, value.Y, 0.0f));
    }
    internal static int GetRectLayoutMode(uint entityId) => _getRectLayoutMode == null ? 0 : _getRectLayoutMode(entityId);
    internal static void SetRectLayoutMode(uint entityId, int value) { if (_setRectLayoutMode != null) _setRectLayoutMode(entityId, value); }
    internal static int GetUIImageType(uint entityId) => _getUIImageType == null ? 0 : _getUIImageType(entityId);
    internal static void SetUIImageType(uint entityId, int value) { if (_setUIImageType != null) _setUIImageType(entityId, value); }
    internal static float GetUIImageThickness(uint entityId) => _getUIImageThickness == null ? 2.0f : _getUIImageThickness(entityId);
    internal static void SetUIImageThickness(uint entityId, float value) { if (_setUIImageThickness != null) _setUIImageThickness(entityId, value); }
    internal static int GetUITextAlignment(uint entityId) => _getUITextAlignment == null ? 4 : _getUITextAlignment(entityId);
    internal static void SetUITextAlignment(uint entityId, int value) { if (_setUITextAlignment != null) _setUITextAlignment(entityId, value); }
    internal static ulong GetUIUpdateSequence() => _getUIUpdateSequence == null ? 0UL : _getUIUpdateSequence();

    private static byte[] Utf8(string? value) => Encoding.UTF8.GetBytes((value ?? string.Empty) + '\0');

    internal static bool RmlShowDocument(string document, bool visible)
    {
        if (_rmlShowDocument == null) return false;
        var a = Utf8(document);
        fixed (byte* p = a) return _rmlShowDocument((nint)p, visible ? 1 : 0) != 0;
    }

    internal static string GetActiveScenePath()
    {
        if (_getActiveScenePath == null)
        {
            return string.Empty;
        }

        return Marshal.PtrToStringUTF8(_getActiveScenePath()) ?? string.Empty;
    }
    internal static string GetRmlWidgetSource(uint entityId) =>
        _getRmlWidgetSource == null ? string.Empty :
        Marshal.PtrToStringUTF8(_getRmlWidgetSource(entityId)) ?? string.Empty;
    internal static void SetRmlWidgetSource(uint entityId, string value)
    {
        if (_setRmlWidgetSource == null) return;
        var bytes = Utf8(value);
        fixed (byte* pointer = bytes) _setRmlWidgetSource(entityId, (nint)pointer);
    }
    internal static bool GetRmlWidgetVisible(uint entityId) =>
        _getRmlWidgetVisible != null && _getRmlWidgetVisible(entityId) != 0;
    internal static void SetRmlWidgetVisible(uint entityId, bool value)
    {
        if (_setRmlWidgetVisible != null) _setRmlWidgetVisible(entityId, value ? 1 : 0);
    }
    internal static bool RmlReloadDocument(string document)
    {
        if (_rmlReloadDocument == null) return false;
        var a = Utf8(document);
        fixed (byte* p = a) return _rmlReloadDocument((nint)p) != 0;
    }
    internal static bool RmlSetText(string document, string id, string value)
    {
        if (_rmlSetText == null) return false;
        var a = Utf8(document); var b = Utf8(id); var c = Utf8(value);
        fixed (byte* pa = a) fixed (byte* pb = b) fixed (byte* pc = c)
            return _rmlSetText((nint)pa, (nint)pb, (nint)pc) != 0;
    }
    internal static string RmlGetText(string document, string id)
    {
        if (_rmlGetText == null) return string.Empty;
        var a = Utf8(document); var b = Utf8(id);
        fixed (byte* pa = a) fixed (byte* pb = b)
            return Marshal.PtrToStringUTF8(_rmlGetText((nint)pa, (nint)pb)) ?? string.Empty;
    }
    internal static bool RmlSetAttribute(string document, string id, string name, string value)
    {
        if (_rmlSetAttribute == null) return false;
        var a = Utf8(document); var b = Utf8(id); var c = Utf8(name); var d = Utf8(value);
        fixed (byte* pa = a) fixed (byte* pb = b) fixed (byte* pc = c) fixed (byte* pd = d)
            return _rmlSetAttribute((nint)pa, (nint)pb, (nint)pc, (nint)pd) != 0;
    }
    internal static string RmlGetAttribute(string document, string id, string name)
    {
        if (_rmlGetAttribute == null) return string.Empty;
        var a = Utf8(document); var b = Utf8(id); var c = Utf8(name);
        fixed (byte* pa = a) fixed (byte* pb = b) fixed (byte* pc = c)
            return Marshal.PtrToStringUTF8(_rmlGetAttribute((nint)pa, (nint)pb, (nint)pc)) ?? string.Empty;
    }
    internal static bool RmlSetClass(string document, string id, string name, bool enabled)
    {
        if (_rmlSetClass == null) return false;
        var a = Utf8(document); var b = Utf8(id); var c = Utf8(name);
        fixed (byte* pa = a) fixed (byte* pb = b) fixed (byte* pc = c)
            return _rmlSetClass((nint)pa, (nint)pb, (nint)pc, enabled ? 1 : 0) != 0;
    }
    internal static bool RmlSetStyle(string document, string id, string name, string value)
    {
        if (_rmlSetStyle == null) return false;
        var a = Utf8(document); var b = Utf8(id); var c = Utf8(name); var d = Utf8(value);
        fixed (byte* pa = a) fixed (byte* pb = b) fixed (byte* pc = c) fixed (byte* pd = d)
            return _rmlSetStyle((nint)pa, (nint)pb, (nint)pc, (nint)pd) != 0;
    }
    internal static bool RmlSubscribeEvent(string document, string id, string eventName)
    {
        if (_rmlSubscribeEvent == null) return false;
        var a = Utf8(document); var b = Utf8(id); var c = Utf8(eventName);
        fixed (byte* pa = a) fixed (byte* pb = b) fixed (byte* pc = c)
            return _rmlSubscribeEvent((nint)pa, (nint)pb, (nint)pc) != 0;
    }
    internal static bool RmlConsumeEvent(string document, string id, string eventName)
    {
        if (_rmlConsumeEvent == null) return false;
        var a = Utf8(document); var b = Utf8(id); var c = Utf8(eventName);
        fixed (byte* pa = a) fixed (byte* pb = b) fixed (byte* pc = c)
            return _rmlConsumeEvent((nint)pa, (nint)pb, (nint)pc) != 0;
    }
    internal static float GetSceneTimeScale() => _getSceneTimeScale == null ? 1.0f : _getSceneTimeScale();
    internal static void SetSceneTimeScale(float value) { if (_setSceneTimeScale != null) _setSceneTimeScale(value); }

    internal static Vector3 GetUIImageColor(uint entityId) => _getUIImageColor == null ? Vector3.One : _getUIImageColor(entityId).ToManaged();
    internal static void SetUIImageColor(uint entityId, Vector3 value) { if (_setUIImageColor != null) _setUIImageColor(entityId, NativeVector3.FromManaged(value)); }
    internal static float GetUIImageAlpha(uint entityId) => _getUIImageAlpha == null ? 1.0f : _getUIImageAlpha(entityId);
    internal static void SetUIImageAlpha(uint entityId, float value) { if (_setUIImageAlpha != null) _setUIImageAlpha(entityId, value); }
    internal static string GetUIImageTexture(uint entityId) => _getUIImageTexture == null ? string.Empty : Marshal.PtrToStringUTF8(_getUIImageTexture(entityId)) ?? string.Empty;
    internal static void SetUIImageTexture(uint entityId, string value)
    {
        if (_setUIImageTexture == null) return;
        var bytes = Encoding.UTF8.GetBytes((value ?? string.Empty) + '\0');
        fixed (byte* valuePtr = bytes) _setUIImageTexture(entityId, (nint)valuePtr);
    }
    internal static bool GetUIImagePreserveAspect(uint entityId) => _getUIImagePreserveAspect != null && _getUIImagePreserveAspect(entityId) != 0;
    internal static void SetUIImagePreserveAspect(uint entityId, bool value) { if (_setUIImagePreserveAspect != null) _setUIImagePreserveAspect(entityId, value ? 1 : 0); }
    internal static float GetUIImageFillAmount(uint entityId) => _getUIImageFillAmount == null ? 1.0f : _getUIImageFillAmount(entityId);
    internal static void SetUIImageFillAmount(uint entityId, float value) { if (_setUIImageFillAmount != null) _setUIImageFillAmount(entityId, value); }

    internal static string GetUIText(uint entityId)
    {
        return _getUIText == null ? string.Empty : Marshal.PtrToStringUTF8(_getUIText(entityId)) ?? string.Empty;
    }

    internal static void SetUIText(uint entityId, string value)
    {
        if (_setUIText == null)
        {
            return;
        }

        var textBytes = Encoding.UTF8.GetBytes((value ?? string.Empty) + '\0');
        fixed (byte* textPtr = textBytes)
        {
            _setUIText(entityId, (nint)textPtr);
        }
    }

    internal static Vector3 GetUITextColor(uint entityId) => _getUITextColor == null ? Vector3.One : _getUITextColor(entityId).ToManaged();
    internal static void SetUITextColor(uint entityId, Vector3 value) { if (_setUITextColor != null) _setUITextColor(entityId, NativeVector3.FromManaged(value)); }
    internal static float GetUITextFontSize(uint entityId) => _getUITextFontSize == null ? 0.0f : _getUITextFontSize(entityId);
    internal static void SetUITextFontSize(uint entityId, float value) { if (_setUITextFontSize != null) _setUITextFontSize(entityId, value); }

    internal static bool GetUIButtonInteractable(uint entityId) => _getUIButtonInteractable != null && _getUIButtonInteractable(entityId) != 0;
    internal static void SetUIButtonInteractable(uint entityId, bool value) { if (_setUIButtonInteractable != null) _setUIButtonInteractable(entityId, value ? 1 : 0); }
    internal static bool GetUIButtonHovered(uint entityId) => _getUIButtonHovered != null && _getUIButtonHovered(entityId) != 0;
    internal static bool GetUIButtonPressed(uint entityId) => _getUIButtonPressed != null && _getUIButtonPressed(entityId) != 0;
    internal static bool GetUIButtonReleased(uint entityId) => _getUIButtonReleased != null && _getUIButtonReleased(entityId) != 0;
    internal static bool GetUIButtonClicked(uint entityId) => _getUIButtonClicked != null && _getUIButtonClicked(entityId) != 0;

    internal static bool GetKeyDown(int keyCode)
    {
        return _getKeyDown != null && _getKeyDown(keyCode) != 0;
    }

    internal static bool GetKeyPressed(int keyCode)
    {
        return _getKeyPressed != null && _getKeyPressed(keyCode) != 0;
    }

    internal static bool GetKeyReleased(int keyCode)
    {
        return _getKeyReleased != null && _getKeyReleased(keyCode) != 0;
    }

    internal static bool GetMouseButtonDown(int button)
    {
        return _getMouseButtonDown != null && _getMouseButtonDown(button) != 0;
    }

    internal static bool GetMouseButtonPressed(int button)
    {
        return _getMouseButtonPressed != null && _getMouseButtonPressed(button) != 0;
    }

    internal static bool GetMouseButtonReleased(int button)
    {
        return _getMouseButtonReleased != null && _getMouseButtonReleased(button) != 0;
    }

    internal static Vector2 GetMousePosition()
    {
        if (_getMousePosition == null)
        {
            return Vector2.Zero;
        }

        var position = _getMousePosition();
        return new Vector2(position.X, position.Y);
    }

    internal static Vector2 GetMouseDelta()
    {
        if (_getMouseDelta == null)
        {
            return Vector2.Zero;
        }

        var delta = _getMouseDelta();
        return new Vector2(delta.X, delta.Y);
    }

    internal static Vector2 GetMouseScrollDelta()
    {
        if (_getMouseScrollDelta == null)
        {
            return Vector2.Zero;
        }

        var delta = _getMouseScrollDelta();
        return new Vector2(delta.X, delta.Y);
    }

    internal static bool GetQuitRequested()
    {
        return _getQuitRequested != null && _getQuitRequested() != 0;
    }

    internal static bool GetCursorLocked()
    {
        return _getCursorLocked != null && _getCursorLocked() != 0;
    }

    internal static void SetCursorLocked(bool locked)
    {
        if (_setCursorLocked == null)
        {
            return;
        }

        _setCursorLocked(locked ? 1 : 0);
    }

    internal static bool PhysicsRaycast(Vector3 origin, Vector3 direction, float maxDistance, uint ignoredEntityId, out NativeRaycastHit hit)
    {
        hit = default;
        if (_physicsRaycast == null)
        {
            return false;
        }

        fixed (NativeRaycastHit* hitPtr = &hit)
        {
            return _physicsRaycast(
                NativeVector3.FromManaged(origin),
                NativeVector3.FromManaged(direction),
                maxDistance,
                ignoredEntityId,
                hitPtr) != 0;
        }
    }

    internal static bool NavigationProjectPoint(uint navigationMeshEntityId, Vector3 point,
        float agentRadius, float agentHeight, out Vector3 projected)
    {
        projected = default;
        if (_navigationProjectPoint == null)
            return false;
        NativeVector3 nativeProjected;
        if (_navigationProjectPoint(navigationMeshEntityId, NativeVector3.FromManaged(point), agentRadius,
                agentHeight, &nativeProjected) == 0)
            return false;
        projected = nativeProjected.ToManaged();
        return true;
    }

    internal static Vector3[] NavigationFindPath(uint navigationMeshEntityId, Vector3 start, Vector3 end,
        float agentRadius, float agentHeight, out bool complete)
    {
        complete = false;
        if (_navigationFindPath == null)
            return [];
        int nativeComplete = 0;
        var count = _navigationFindPath(navigationMeshEntityId, NativeVector3.FromManaged(start),
            NativeVector3.FromManaged(end), agentRadius, agentHeight, null, 0, &nativeComplete);
        if (count <= 0)
            return [];
        var nativePoints = new NativeVector3[count];
        fixed (NativeVector3* points = nativePoints)
        {
            count = _navigationFindPath(navigationMeshEntityId, NativeVector3.FromManaged(start),
                NativeVector3.FromManaged(end), agentRadius, agentHeight, points, count, &nativeComplete);
        }
        count = Math.Min(Math.Max(0, count), nativePoints.Length);
        var result = new Vector3[Math.Max(0, count)];
        for (var index = 0; index < result.Length; ++index)
            result[index] = nativePoints[index].ToManaged();
        complete = nativeComplete != 0;
        return result;
    }

    internal static bool PhysicsRaycastTagged(Vector3 origin, Vector3 direction, float maxDistance, uint ignoredEntityId, string tag, out NativeRaycastHit hit)
    {
        hit = default;
        if (_physicsRaycastTagged == null)
        {
            return false;
        }

        var tagPtr = Marshal.StringToCoTaskMemUTF8(tag ?? string.Empty);
        try
        {
            fixed (NativeRaycastHit* hitPtr = &hit)
            {
                return _physicsRaycastTagged(
                    NativeVector3.FromManaged(origin),
                    NativeVector3.FromManaged(direction),
                    maxDistance,
                    ignoredEntityId,
                    tagPtr,
                    hitPtr) != 0;
            }
        }
        finally
        {
            Marshal.FreeCoTaskMem(tagPtr);
        }
    }

    internal static Vector3 PhysicsMoveKinematic(uint entityId, Vector3 displacement, float skinWidth)
    {
        if (_physicsMoveKinematic == null)
        {
            return Vector3.Zero;
        }

        return _physicsMoveKinematic(entityId, NativeVector3.FromManaged(displacement), skinWidth).ToManaged();
    }

    internal static uint SpawnDecal(Vector3 point, Vector3 normal, string materialAssetReference,
        Vector2 size, float depth, float lifetime, float fadeDuration)
    {
        if (_spawnDecal == null)
        {
            return 0;
        }

        var materialReferencePtr = Marshal.StringToCoTaskMemUTF8(materialAssetReference ?? string.Empty);
        try
        {
            return _spawnDecal(
                NativeVector3.FromManaged(point),
                NativeVector3.FromManaged(normal),
                materialReferencePtr,
                new NativeVector3 { X = size.X, Y = size.Y, Z = depth },
                lifetime,
                fadeDuration);
        }
        finally
        {
            Marshal.FreeCoTaskMem(materialReferencePtr);
        }
    }

    internal static void LogMessage(int severity, string? message)
    {
        if (_logMessage == null)
        {
            return;
        }

        var textPtr = Marshal.StringToCoTaskMemUTF8(message ?? string.Empty);
        try
        {
            _logMessage(severity, textPtr);
        }
        finally
        {
            Marshal.FreeCoTaskMem(textPtr);
        }
    }

    private static void ResetLoadedAssembly()
    {
        Instances.Clear();
        InstancesByEntity.Clear();
        ScriptClasses.Clear();
        InvokableMethods.Clear();

        if (_loadContext is null)
        {
            return;
        }

        _loadedAssembly = null;
        _loadContext.Unload();
        _loadContext = null;

        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
    }

    private static void AddInstanceToEntityIndex(ScriptBehaviour instance)
    {
        if (!InstancesByEntity.TryGetValue(instance.EntityId, out var instances))
        {
            instances = [];
            InstancesByEntity.Add(instance.EntityId, instances);
        }

        instances.Add(instance);
    }

    private static void RemoveInstanceFromEntityIndex(ScriptBehaviour instance)
    {
        if (!InstancesByEntity.TryGetValue(instance.EntityId, out var instances))
        {
            return;
        }

        instances.Remove(instance);
        if (instances.Count == 0)
        {
            InstancesByEntity.Remove(instance.EntityId);
        }
    }

    private static IEnumerable<ScriptClassMetadata> DiscoverScriptClasses(Assembly assembly)
    {
        foreach (var type in assembly.GetTypes())
        {
            var isBehaviour = typeof(ScriptBehaviour).IsAssignableFrom(type);
            var isScriptableObject = typeof(ScriptableObject).IsAssignableFrom(type);
            if (type.IsAbstract || (!isBehaviour && !isScriptableObject))
            {
                continue;
            }

            var defaultInstance = Activator.CreateInstance(type, nonPublic: true);
            var fields = new List<ScriptFieldMetadata>();

            foreach (var field in type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
            {
                if (field.IsStatic || field.GetCustomAttribute<SerializedFieldAttribute>() is null)
                {
                    continue;
                }

                var fieldType = MapFieldType(field.FieldType);
                if (field.FieldType == typeof(string) && field.GetCustomAttribute<MaterialAssetAttribute>() is not null)
                {
                    fieldType = 25;
                }
                if (fieldType is null)
                {
                    continue;
                }

                fields.Add(new ScriptFieldMetadata(field.Name, fieldType.Value, GetReferenceTypeName(field.FieldType, fieldType.Value), field.GetValue(defaultInstance), field));
            }

            foreach (var property in type.GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
            {
                if (property.GetCustomAttribute<SerializedFieldAttribute>() is null || property.GetMethod is null || property.SetMethod is null)
                {
                    continue;
                }

                var fieldType = MapFieldType(property.PropertyType);
                if (property.PropertyType == typeof(string) && property.GetCustomAttribute<MaterialAssetAttribute>() is not null)
                {
                    fieldType = 25;
                }
                if (fieldType is null)
                {
                    continue;
                }

                fields.Add(new ScriptFieldMetadata(property.Name, fieldType.Value, GetReferenceTypeName(property.PropertyType, fieldType.Value), property.GetValue(defaultInstance), property));
            }

            yield return new ScriptClassMetadata(
                assembly.GetName().Name ?? string.Empty,
                type.Namespace ?? string.Empty,
                type.Name,
                type,
                isScriptableObject,
                GetAssignableTypeNames(type),
                fields);
        }
    }

    private static int? MapFieldType(Type type)
    {
        if (type == typeof(bool))
        {
            return 1;
        }

        if (type == typeof(int))
        {
            return 2;
        }

        if (type == typeof(float))
        {
            return 3;
        }

        if (type == typeof(double))
        {
            return 4;
        }

        if (type == typeof(string))
        {
            return 5;
        }

        if (type == typeof(Vector2))
        {
            return 6;
        }

        if (type == typeof(Vector3))
        {
            return 7;
        }

        if (type == typeof(uint))
        {
            return 8;
        }

        if (type == typeof(GameObject))
        {
            return 9;
        }

        if (type == typeof(MeshComponent))
        {
            return 10;
        }

        if (type == typeof(CameraComponent))
        {
            return 11;
        }

        if (type == typeof(LightComponent))
        {
            return 12;
        }

        if (type == typeof(RigidbodyComponent))
        {
            return 13;
        }

        if (type == typeof(ColliderComponent))
        {
            return 14;
        }

        if (type == typeof(AnimationComponent))
        {
            return 15;
        }

        if (type == typeof(CanvasComponent))
        {
            return 16;
        }

        if (type == typeof(RectTransformComponent))
        {
            return 17;
        }

        if (type == typeof(UIImageComponent))
        {
            return 18;
        }

        if (type == typeof(UITextComponent))
        {
            return 19;
        }

        if (type == typeof(UIButtonComponent))
        {
            return 20;
        }

        if (type == typeof(ParticleSystemComponent))
        {
            return 21;
        }

        if (type == typeof(SoundEmitterComponent))
        {
            return 22;
        }

        if (type == typeof(Prefab))
        {
            return 23;
        }

        if (typeof(ScriptableObject).IsAssignableFrom(type))
        {
            return 24;
        }

        return null;
    }

    private static string GetReferenceTypeName(Type memberType, int fieldType)
    {
        return fieldType == 24 ? memberType.FullName ?? memberType.Name : string.Empty;
    }

    private static IReadOnlyList<string> GetAssignableTypeNames(Type type)
    {
        var names = new List<string>();
        for (var current = type; current is not null; current = current.BaseType)
        {
            if (!string.IsNullOrEmpty(current.FullName)) names.Add(current.FullName);
        }
        return names;
    }

    private static string BuildMetadataPayload()
    {
        var builder = new StringBuilder();

        foreach (var scriptClass in ScriptClasses.Values)
        {
            builder.Append(scriptClass.IsScriptableObject ? "OBJECT\t" : "CLASS\t")
                .Append(Escape(scriptClass.AssemblyName)).Append('\t')
                .Append(Escape(scriptClass.NamespaceName)).Append('\t')
                .Append(Escape(scriptClass.ClassName)).Append('\t')
                .Append(Escape(string.Join(';', scriptClass.AssignableTypeNames))).Append('\n');

            foreach (var field in scriptClass.Fields)
            {
                builder.Append("FIELD\t")
                    .Append(Escape(field.Name)).Append('\t')
                    .Append(field.Type.ToString(CultureInfo.InvariantCulture)).Append('\t')
                    .Append('1').Append('\t')
                    .Append(Escape(SerializeValue(field.Type, field.DefaultValue))).Append('\t')
                    .Append(Escape(field.ReferenceTypeName)).Append('\t')
                    .Append(field.DefaultValue is null ? '1' : '0').Append('\n');
            }

            builder.Append("END\n");
        }

        return builder.ToString();
    }

    private static void ApplyFieldValues(object instance, ScriptClassMetadata scriptClass, string fieldData)
    {
        foreach (var line in fieldData.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            var tokens = SplitEscaped(line, '\t');
            if (tokens.Count < 4 || !string.Equals(tokens[0], "FIELD", StringComparison.Ordinal))
            {
                continue;
            }

            var fieldName = tokens[1];
            var fieldType = int.Parse(tokens[2], CultureInfo.InvariantCulture);
            var member = scriptClass.Fields.FirstOrDefault(candidate => candidate.Name == fieldName);
            if (member is null)
            {
                continue;
            }

            var memberType = member.Member switch
            {
                FieldInfo fieldInfo => fieldInfo.FieldType,
                PropertyInfo propertyInfo => propertyInfo.PropertyType,
                _ => typeof(object),
            };
            var value = ParseValue(fieldType, tokens[3], memberType);

            if (member.Member is FieldInfo field)
            {
                field.SetValue(instance, value);
            }
            else if (member.Member is PropertyInfo property)
            {
                property.SetValue(instance, value);
            }
        }
    }

    private static string BuildFieldData(ScriptBehaviour instance, ScriptClassMetadata scriptClass)
    {
        var builder = new StringBuilder();

        foreach (var field in scriptClass.Fields)
        {
            var value = field.Member switch
            {
                FieldInfo fieldInfo => fieldInfo.GetValue(instance),
                PropertyInfo propertyInfo => propertyInfo.GetValue(instance),
                _ => null,
            };

            builder.Append("FIELD\t")
                .Append(Escape(field.Name)).Append('\t')
                .Append(field.Type.ToString(CultureInfo.InvariantCulture)).Append('\t')
                .Append(Escape(SerializeValue(field.Type, value))).Append('\n');
        }

        return builder.ToString();
    }

    private static object? ParseValue(int fieldType, string value, Type memberType)
    {
        return fieldType switch
        {
            1 => string.Equals(value, "true", StringComparison.Ordinal),
            2 => int.Parse(value, CultureInfo.InvariantCulture),
            3 => float.Parse(value, CultureInfo.InvariantCulture),
            4 => double.Parse(value, CultureInfo.InvariantCulture),
            5 => value,
            6 => ParseVector2(value),
            7 => ParseVector3(value),
            8 => uint.Parse(value, CultureInfo.InvariantCulture),
            9 => CreateReferenceValue(memberType, value),
            10 => CreateReferenceValue(memberType, value),
            11 => CreateReferenceValue(memberType, value),
            12 => CreateReferenceValue(memberType, value),
            13 => CreateReferenceValue(memberType, value),
            14 => CreateReferenceValue(memberType, value),
            15 => CreateReferenceValue(memberType, value),
            16 => CreateReferenceValue(memberType, value),
            17 => CreateReferenceValue(memberType, value),
            18 => CreateReferenceValue(memberType, value),
            19 => CreateReferenceValue(memberType, value),
            20 => CreateReferenceValue(memberType, value),
            21 => CreateReferenceValue(memberType, value),
            22 => CreateReferenceValue(memberType, value),
            23 => string.IsNullOrWhiteSpace(value) ? null : new Prefab(value),
            24 => LoadScriptableObject(value, memberType),
            25 => value,
            _ => null,
        };
    }

    private static string SerializeValue(int fieldType, object? value)
    {
        return fieldType switch
        {
            1 => (bool?)value == true ? "true" : "false",
            2 => Convert.ToInt32(value, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture),
            3 => Convert.ToSingle(value, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture),
            4 => Convert.ToDouble(value, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture),
            5 => value as string ?? string.Empty,
            6 => SerializeVector2(value),
            7 => SerializeVector3(value),
            8 => Convert.ToUInt32(value, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture),
            9 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            10 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            11 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            12 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            13 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            14 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            15 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            16 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            17 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            18 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            19 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            20 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            21 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            22 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            23 => (value as Prefab)?.AssetReference ?? string.Empty,
            24 => (value as ScriptableObject)?.AssetReference ?? string.Empty,
            25 => value as string ?? string.Empty,
            _ => string.Empty,
        };
    }

    private static ScriptableObject? LoadScriptableObject(string assetReference, Type expectedType)
    {
        if (string.IsNullOrWhiteSpace(assetReference) || _loadScriptableObjectAsset == null)
        {
            return null;
        }

        var referenceBytes = Encoding.UTF8.GetBytes(assetReference + '\0');
        string assetData;
        fixed (byte* referencePtr = referenceBytes)
        {
            var dataPtr = _loadScriptableObjectAsset(referencePtr);
            assetData = Marshal.PtrToStringUTF8(dataPtr) ?? string.Empty;
        }

        // Native asset loading may preserve Windows CRLF line endings (for
        // example when reading packaged data in binary mode). A trailing '\r'
        // otherwise becomes part of the managed type name and makes a valid
        // asset fail type lookup, which then reads back as a blank reference.
        assetData = assetData.Replace("\r\n", "\n", StringComparison.Ordinal)
                             .Replace('\r', '\n');

        var lines = assetData.Split('\n', StringSplitOptions.RemoveEmptyEntries);
        if (lines.Length == 0)
        {
            return null;
        }

        var header = SplitEscaped(lines[0], '\t');
        if (header.Count < 2 || header[0] != "SCRIPTABLE" || !ScriptClasses.TryGetValue(header[1], out var scriptClass) || !scriptClass.IsScriptableObject)
        {
            return null;
        }
        if (!expectedType.IsAssignableFrom(scriptClass.Type))
        {
            return null;
        }

        if (Activator.CreateInstance(scriptClass.Type, nonPublic: true) is not ScriptableObject instance)
        {
            return null;
        }

        instance.AssetReference = assetReference;
        ApplyFieldValues(instance, scriptClass, string.Join('\n', lines.Skip(1)));
        return instance;
    }

    private static object? CreateReferenceValue(Type memberType, string value)
    {
        var entityId = uint.Parse(value, CultureInfo.InvariantCulture);
        if (entityId == 0)
        {
            return null;
        }

        if (memberType == typeof(GameObject))
        {
            return new GameObject(entityId);
        }

        if (memberType == typeof(MeshComponent))
        {
            return new MeshComponent(entityId);
        }

        if (memberType == typeof(CameraComponent))
        {
            return new CameraComponent(entityId);
        }

        if (memberType == typeof(LightComponent))
        {
            return new LightComponent(entityId);
        }

        if (memberType == typeof(RigidbodyComponent))
        {
            return new RigidbodyComponent(entityId);
        }

        if (memberType == typeof(ColliderComponent))
        {
            return new ColliderComponent(entityId);
        }

        if (memberType == typeof(AnimationComponent))
        {
            return new AnimationComponent(entityId);
        }

        if (memberType == typeof(CanvasComponent))
        {
            return new CanvasComponent(entityId);
        }

        if (memberType == typeof(RectTransformComponent))
        {
            return new RectTransformComponent(entityId);
        }

        if (memberType == typeof(UIImageComponent))
        {
            return new UIImageComponent(entityId);
        }

        if (memberType == typeof(UITextComponent))
        {
            return new UITextComponent(entityId);
        }

        if (memberType == typeof(UIButtonComponent))
        {
            return new UIButtonComponent(entityId);
        }

        if (memberType == typeof(ParticleSystemComponent))
        {
            return new ParticleSystemComponent(entityId);
        }

        if (memberType == typeof(SoundEmitterComponent))
        {
            return new SoundEmitterComponent(entityId);
        }

        return entityId;
    }

    private static uint ExtractEntityId(object? value)
    {
        return value switch
        {
            null => 0u,
            uint entityId => entityId,
            GameObject gameObject => gameObject.EntityId,
            MeshComponent meshComponent => meshComponent.EntityId,
            CameraComponent cameraComponent => cameraComponent.EntityId,
            LightComponent lightComponent => lightComponent.EntityId,
            RigidbodyComponent rigidbodyComponent => rigidbodyComponent.EntityId,
            ColliderComponent colliderComponent => colliderComponent.EntityId,
            AnimationComponent animationComponent => animationComponent.EntityId,
            CanvasComponent canvasComponent => canvasComponent.EntityId,
            RectTransformComponent rectTransformComponent => rectTransformComponent.EntityId,
            UIImageComponent imageComponent => imageComponent.EntityId,
            UITextComponent textComponent => textComponent.EntityId,
            UIButtonComponent buttonComponent => buttonComponent.EntityId,
            ParticleSystemComponent particleSystemComponent => particleSystemComponent.EntityId,
            SoundEmitterComponent soundEmitterComponent => soundEmitterComponent.EntityId,
            _ => 0u,
        };
    }

    private static string SerializeVector2(object? value)
    {
        var vector = value is Vector2 typedValue ? typedValue : Vector2.Zero;
        return string.Create(CultureInfo.InvariantCulture, $"{vector.X},{vector.Y}");
    }

    private static string SerializeVector3(object? value)
    {
        var vector = value is Vector3 typedValue ? typedValue : Vector3.Zero;
        return string.Create(CultureInfo.InvariantCulture, $"{vector.X},{vector.Y},{vector.Z}");
    }

    private static Vector2 ParseVector2(string value)
    {
        var parts = SplitEscaped(value, ',');
        return parts.Count == 2
            ? new Vector2(float.Parse(parts[0], CultureInfo.InvariantCulture), float.Parse(parts[1], CultureInfo.InvariantCulture))
            : Vector2.Zero;
    }

    private static Vector3 ParseVector3(string value)
    {
        var parts = SplitEscaped(value, ',');
        return parts.Count == 3
            ? new Vector3(float.Parse(parts[0], CultureInfo.InvariantCulture), float.Parse(parts[1], CultureInfo.InvariantCulture), float.Parse(parts[2], CultureInfo.InvariantCulture))
            : Vector3.Zero;
    }

    private static List<string> SplitEscaped(string text, char delimiter)
    {
        var tokens = new List<string>();
        var builder = new StringBuilder();
        var escaping = false;

        foreach (var character in text)
        {
            if (escaping)
            {
                builder.Append(character switch
                {
                    'n' => '\n',
                    't' => '\t',
                    _ => character,
                });

                escaping = false;
                continue;
            }

            if (character == '\\')
            {
                escaping = true;
                continue;
            }

            if (character == delimiter)
            {
                tokens.Add(builder.ToString());
                builder.Clear();
                continue;
            }

            builder.Append(character);
        }

        tokens.Add(builder.ToString());
        return tokens;
    }

    private static string Escape(string value)
    {
        return value
            .Replace("\\", "\\\\", StringComparison.Ordinal)
            .Replace("\t", "\\t", StringComparison.Ordinal)
            .Replace("\n", "\\n", StringComparison.Ordinal);
    }

    private static void SetError(string message)
    {
        lock (Gate)
        {
            _lastError = message;
        }
    }
}
