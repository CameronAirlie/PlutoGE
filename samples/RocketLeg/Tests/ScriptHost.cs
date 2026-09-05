// Minimal ScriptCore adapter backed by actual Bullet physics for this sample's
// headless driving tests. The production controller is compiled unchanged.
using System.Numerics;
using System.Runtime.InteropServices;
namespace PlutoGE.ScriptCore;
public static class Native {
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern void SetRayMask(int mask);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern void SetBall(Vector3 p);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern Vector3 BallPointVelocity(Vector3 p);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern void Initialize(float[] vertices, int count);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern void Step(float dt);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern void Clear();
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern Vector3 Position();
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern void SetPosition(Vector3 p);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern Vector3 Right();
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern Vector3 Forward();
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern void SetRotation(float x,float y,float z,float w);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern Vector3 Velocity();
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern Vector3 AngularVelocity();
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern void SetVelocity(Vector3 v);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern void SetAngularVelocity(Vector3 v);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern Vector3 PointVelocity(Vector3 p);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern void Force(Vector3 f);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern void ForceAt(Vector3 f, Vector3 p);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern void Impulse(Vector3 f);
[DllImport("RocketLegBullet", CallingConvention=CallingConvention.Cdecl)] public static extern int Ray(Vector3 o,Vector3 d,float l,out Vector3 p,out Vector3 n,out float distance);
}
[AttributeUsage(AttributeTargets.Field)] public class SerializedFieldAttribute : Attribute {}
[AttributeUsage(AttributeTargets.Field)] public class InputMappingAssetAttribute : Attribute {}
public class ScriptBehaviour { public GameObject GameObject {get;} = new(); public virtual void OnCreate(){} public virtual void OnUpdate(float dt){} public virtual void OnFixedUpdate(float dt){} }
public class GameObject {
 public static GameObject? Find(string name)=>null;
 public Vector3 Position;
 public Quaternion RotationQuaternion;
 public bool HasTag(string tag)=>Ball && tag=="ball";
 public bool Ball;
 public string Name="Car"; public Vector3 Scale=new(1.2f,.4f,2);
 public bool Static;
 public Vector3 WorldPosition {get=>Native.Position();set=>Native.SetPosition(value);}
 public Vector3 WorldRotation {get=>Vector3.Zero;set {var q=Quaternion.CreateFromYawPitchRoll(value.Y*MathF.PI/180,value.X*MathF.PI/180,value.Z*MathF.PI/180);Native.SetRotation(q.X,q.Y,q.Z,q.W);}}
 public Vector3 Forward=>Native.Forward(); public Vector3 Right=>Native.Right();
 public Dictionary<Type,object> Components=new();
 public T? GetComponent<T>() where T:class => Ball && typeof(T)==typeof(RigidbodyComponent) ? new RigidbodyComponent {IsBall=true} as T : Static ? null : Components.GetValueOrDefault(typeof(T)) as T;
}
public enum ColliderShape {Box}
public class ColliderComponent {public ColliderShape Shape; public Vector3 Center,Size;}
public class RigidbodyComponent {
 public bool IsBall;
 public float Mass, LinearDrag, AngularDrag, Friction; public bool UseGravity, IsKinematic, FreezeRotation;
 // Match ScriptCore's fixed-callback snapshots: impulses mutate Bullet at once,
 // but Velocity remains the pre-callback value until the host synchronizes it.
 public Vector3 Velocity {get;set;} public Vector3 AngularVelocity {get;set;}
 public void SyncIn(){Velocity=Native.Velocity();AngularVelocity=Native.AngularVelocity();}
 public void SyncOut(){Native.SetVelocity(Velocity);Native.SetAngularVelocity(AngularVelocity);}
 public Vector3 GetVelocityAtPoint(Vector3 p)=>IsBall?Native.BallPointVelocity(p):Native.PointVelocity(p);
 public void AddForce(Vector3 f)=>Native.Force(f);
 public void AddImpulse(Vector3 f)=>Native.Impulse(f);
 public void AddForceAtPosition(Vector3 f,Vector3 p)=>Native.ForceAt(f,p);
}
public class InputActionMap {
 public static bool Boost,Jump,Powerslide; public static float Throttle,Steer,Pitch;
 public static InputActionMap Load(string p)=>new();
 public float GetAxis(string a)=>a.EndsWith("Forward")?MathF.Max(Throttle,0):a.EndsWith("Backward")?MathF.Max(-Throttle,0):a.EndsWith("Steer")?Steer:a.EndsWith("Pitch")?Pitch:0;
 public bool IsDown(string a)=>(Boost&&a.EndsWith("Boost"))||(Jump&&a.EndsWith("Jump"))||(Powerslide&&a.EndsWith("Powerslide"));
 public bool WasPressed(string a)=>false;
}
public static class Debug {public static void LogError(string s)=>throw new Exception(s);}
public struct RaycastHit {public Vector3 Normal,Point; public float Distance; public GameObject Entity;}
public static class Physics {
 public static bool Raycast(Vector3 o,Vector3 d,float l,GameObject ignore,out RaycastHit hit){
  hit=new(){Entity=new(){Static=true}};
  var result=Native.Ray(o,d,l,out hit.Point,out hit.Normal,out hit.Distance);
  hit.Entity.Ball=result==2;return result!=0;
 }
}
