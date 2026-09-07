namespace PlutoGE.ScriptCore.Examples;

/// <summary>Attach to a Camera + Camera Rig and assign a character. Arrow keys orbit the camera.</summary>
public sealed class CharacterCameraRig : ScriptBehaviour
{
    [SerializedField] private GameObject? target = null;
    private float yaw, pitch = 15;
    public override void OnCreate()
    {
        if (!CameraRig.Configure(GameObject, target, new() { Mode = CameraRigMode.Orbit, Distance = 4 }))
            Debug.LogWarning("CharacterCameraRig requires a Camera Rig component.");
    }
    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime <= 0) return;
        yaw += ((Input.IsKeyDown(KeyCode.Right) ? 1 : 0) - (Input.IsKeyDown(KeyCode.Left) ? 1 : 0)) * 90 * deltaTime;
        pitch = Math.Clamp(pitch + ((Input.IsKeyDown(KeyCode.Up) ? 1 : 0) - (Input.IsKeyDown(KeyCode.Down) ? 1 : 0)) * 60 * deltaTime, -70, 80);
        CameraRig.Orbit(GameObject, yaw, pitch, 4);
    }
}
