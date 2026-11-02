using System.Numerics;

namespace PlutoGE.ScriptCore;

/// <summary>
/// A volume of volumetric clouds. Its shadows on the ground and the overcast light of dense cover follow these
/// settings every frame (docs/CLOUD_SHADOWS.md), so a weather script can drive them directly, for example by
/// raising <see cref="Coverage"/> toward 1 and <see cref="Density"/> to close the sky into overcast.
/// </summary>
[NativeComponent("VolumetricCloudComponent")]
public sealed class VolumetricCloudComponent : RegisteredComponent
{
    internal VolumetricCloudComponent(uint entityId) : base(entityId)
    {
    }

    /// <summary>Unscaled size of the volume, in world units.</summary>
    public Vector3 Size { get => GetVector3("Size"); set => SetVector3("Size", value); }

    /// <summary>Fraction of the volume that is cloud, 0 to 1.</summary>
    public float Coverage { get => GetFloat("Coverage"); set => SetFloat("Coverage", value); }
    public float Density { get => GetFloat("Density"); set => SetFloat("Density", value); }
    /// <summary>Extinction per world unit at full density.</summary>
    public float Extinction { get => GetFloat("Extinction"); set => SetFloat("Extinction", value); }
    public float ScatteringAlbedo { get => GetFloat("Scattering Albedo"); set => SetFloat("Scattering Albedo", value); }
    /// <summary>Forward scattering, -0.9 to 0.9.</summary>
    public float Anisotropy { get => GetFloat("Anisotropy"); set => SetFloat("Anisotropy", value); }
    public float AmbientLight { get => GetFloat("Ambient Light"); set => SetFloat("Ambient Light", value); }
    public Vector3 CloudColor { get => GetVector3("Cloud Color"); set => SetVector3("Cloud Color", value); }

    public float BaseNoiseScale { get => GetFloat("Base Noise Scale"); set => SetFloat("Base Noise Scale", value); }
    public float DetailNoiseScale { get => GetFloat("Detail Noise Scale"); set => SetFloat("Detail Noise Scale", value); }
    public float DetailErosion { get => GetFloat("Detail Erosion"); set => SetFloat("Detail Erosion", value); }

    public Vector3 WindDirection { get => GetVector3("Wind Direction"); set => SetVector3("Wind Direction", value); }
    /// <summary>World units per second.</summary>
    public float WindSpeed { get => GetFloat("Wind Speed"); set => SetFloat("Wind Speed", value); }

    /// <summary>Trace resolution scale, 0.25 to 1.</summary>
    public float RenderScale { get => GetFloat("Render Scale"); set => SetFloat("Render Scale", value); }
    public int PrimarySteps { get => GetInt("Primary Steps"); set => SetInt("Primary Steps", value); }
    public int LightSteps { get => GetInt("Light Steps"); set => SetInt("Light Steps", value); }

    /// <summary>Whether the volume shades the directional light through the cloud shadow map.</summary>
    public bool CastShadows { get => GetBool("Cast Shadows"); set => SetBool("Cast Shadows", value); }
    /// <summary>How far from the camera cloud shadows reach, in world units.</summary>
    public float ShadowDistance { get => GetFloat("Shadow Distance"); set => SetFloat("Shadow Distance", value); }
    /// <summary>Scale on the volume's extinction for its shadows only.</summary>
    public float ShadowDensity { get => GetFloat("Shadow Density"); set => SetFloat("Shadow Density", value); }
}
