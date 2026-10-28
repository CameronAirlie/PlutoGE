using System.Numerics;
using System.Text.Json;
using RocketLeg.Scripts;
using PlutoGE.ScriptCore;

try
{
static void Check(bool ok,string message) {if(!ok)throw new Exception(message);}
// Read actual GLB stadium triangles, bypassing only the renderer/import cache.
var vertices = new List<float>();
foreach(var name in new[]{"ArenaBanks","ArenaShell"}) {
 var bytes=File.ReadAllBytes(Path.Combine(args[0],name+".glb"));
 int jsonLength=BitConverter.ToInt32(bytes,12), binaryOffset=28+jsonLength;
 using var doc=JsonDocument.Parse(bytes.AsMemory(20,jsonLength)); var root=doc.RootElement;
 var views=root.GetProperty("bufferViews");
 int positions=binaryOffset+views[0].GetProperty("byteOffset").GetInt32();
 int indices=binaryOffset+views[2].GetProperty("byteOffset").GetInt32();
 int count=root.GetProperty("accessors")[2].GetProperty("count").GetInt32();
 for(int i=0;i<count;i++) {int vertex=BitConverter.ToInt32(bytes,indices+i*4);for(int j=0;j<3;j++)vertices.Add(BitConverter.ToSingle(bytes,positions+vertex*12+j*4));}
}
Native.Initialize(vertices.ToArray(),vertices.Count,ArenaLayout.FloorHalfLength,ArenaLayout.FloorHalfWidth,ArenaLayout.CeilingHeight);
const float WallStart=ArenaLayout.BankRadius+1;
ArcadeCarController Car(Vector3 position) {
 Native.SetRayMask(15);Native.SetBall(new(0,-100,0));
 Native.Clear(); Native.SetPosition(position); Native.SetRotation(0,1,0,0); Native.SetVelocity(Vector3.Zero);Native.SetAngularVelocity(Vector3.Zero);
 InputActionMap.Boost=false;InputActionMap.Jump=false;InputActionMap.Throttle=0;InputActionMap.Steer=0;InputActionMap.Pitch=0;InputActionMap.Powerslide=false;
 var c=new ArcadeCarController();c.GameObject.Components[typeof(RigidbodyComponent)]=new RigidbodyComponent();c.GameObject.Components[typeof(ColliderComponent)]=new ColliderComponent();c.OnCreate();return c;
}
void Tick(ArcadeCarController c,float dt) {var b=c.GameObject.GetComponent<RigidbodyComponent>()!;b.SyncIn();c.OnUpdate(dt);c.OnFixedUpdate(dt);b.SyncOut();Native.Step(dt);}
// Inside the stadium shell (plan-view rounded wall, roof, or a goal), with a small margin.
bool InsideArena(Vector3 p) {
 const float cornerRadius=10;
 if(p.Y>ArenaLayout.CeilingHeight+.5f||p.Y<-1)return false;
 if(MathF.Abs(p.Z)<ArenaLayout.GoalHalfWidth&&MathF.Abs(p.X)<ArenaLayout.GoalLine+ArenaLayout.GoalDepth+.5f&&p.Y<ArenaLayout.GoalHeight+.5f)return true;
 float dx=MathF.Max(MathF.Abs(p.X)-(ArenaLayout.FloorHalfLength-cornerRadius),0),dz=MathF.Max(MathF.Abs(p.Z)-(ArenaLayout.FloorHalfWidth-cornerRadius),0);
 return MathF.Sqrt(dx*dx+dz*dz)<cornerRadius+ArenaLayout.BankRadius+.5f;
}
// Shell containment: points on the play side are left alone, points past it come back.
Check(!ArenaLayout.TryContain(new(0,.34f,0),.4f,out _,out _) && !ArenaLayout.TryContain(new(0,15,ArenaLayout.FloorHalfWidth+ArenaLayout.BankRadius-.4f),.4f,out _,out _) &&
      !ArenaLayout.TryContain(new(ArenaLayout.GoalLine+3,2,0),.4f,out _,out _),"containment ignores legal positions and goals");
Check(ArenaLayout.TryContain(new(0,15,ArenaLayout.FloorHalfWidth+ArenaLayout.BankRadius+.3f),.4f,out var backInside,out var wallInward) &&
      MathF.Abs(backInside.Z-(ArenaLayout.FloorHalfWidth+ArenaLayout.BankRadius-.4f))<.01f && wallInward.Z< -.99f,"containment returns a car behind the side wall");
Check(ArenaLayout.TryContain(new(0,ArenaLayout.CeilingHeight+.2f,0),.4f,out var underRoof,out var roofInward) &&
      MathF.Abs(underRoof.Y-(ArenaLayout.CeilingHeight-.4f))<.01f && roofInward.Y< -.99f,"containment returns a car above the roof");
foreach(float dt in new[]{1/60f,1/120f})
foreach(bool boost in new[]{false,true}) {
 var c=Car(new(0,.75f,12));
 for(int i=0;i<2/dt;i++)Tick(c,dt);
 Check(c.Grounded && Native.Position().Y>.27f && Native.Position().Y<.45f,"stable suspension ride height");
 InputActionMap.Throttle=1; InputActionMap.Boost=boost;
 float maxY=0;bool wall=false,ceiling=false;
 for(int i=0;i<6/dt;i++) {
  var before=Native.Position();var beforeVelocity=Native.Velocity();
  Tick(c,dt); var pos=Native.Position();maxY=MathF.Max(maxY,pos.Y);
  if(!InsideArena(pos)) Check(false,$"car left the stadium at t={i*dt:0.00}s: {before} -> {pos}, velocity {beforeVelocity}, grounded={c.Grounded}");
  wall |= c.Grounded && pos.Y>WallStart && MathF.Abs(c.SurfaceNormal.Y)<.2f;
  ceiling |= c.Grounded && c.SurfaceNormal.Y<-.9f;
 }
 Console.WriteLine($"dt={dt}, boost={boost}: max height={maxY}, wall={wall}, ceiling={ceiling}, position={Native.Position()}");
 Check(wall,"drive from floor through concave bank onto vertical wall");
 // Roof contact deliberately releases the suspension now.
 Check(maxY>ArenaLayout.WallTop,"reach upper bank");
 Check(maxY<ArenaLayout.CeilingHeight,"stay inside the roof at top speed");
}

// Approach the rounded plan-view corner obliquely, not just a straight bank.
var corner=Car(new(34,.75f,15));
var cornerRotation=Quaternion.CreateFromAxisAngle(Vector3.UnitY,MathF.PI*1.25f);
Native.SetRotation(cornerRotation.X,cornerRotation.Y,cornerRotation.Z,cornerRotation.W);
InputActionMap.Throttle=1;
bool cornerWall=false;
for(int i=0;i<480;i++) {Tick(corner,1/60f);cornerWall |= corner.Grounded && Native.Position().Y>ArenaLayout.BankRadius && MathF.Abs(corner.SurfaceNormal.Y)<.3f;}
Check(cornerWall,"oblique rounded-corner traversal");

// Jump away from a vertical wall; suspension must not reattach immediately.
var jumper=Car(new(0,.75f,16));InputActionMap.Throttle=1;
for(int i=0;i<360 && !(jumper.Grounded && Native.Position().Y>WallStart && MathF.Abs(jumper.SurfaceNormal.Y)<.1f);i++) Tick(jumper,1/60f);
Check(jumper.Grounded && Native.Position().Y>WallStart,"wall jump setup");
var wallNormal=jumper.SurfaceNormal;InputActionMap.Jump=true;
Tick(jumper,1/60f);
Check(!jumper.Grounded && Vector3.Dot(Native.Velocity(),wallNormal)>5,"wall jump impulse away from surface");
for(int i=0;i<8;i++){Tick(jumper,1/60f);Check(!jumper.Grounded,"jump detachment lockout");}

var airborne=Car(new(0,10,0));
for(int i=0;i<12;i++)Tick(airborne,1/60f);
Check(!airborne.Grounded && Native.Velocity().Y < -1,"airborne gravity without surface magnetism");
for(int i=0;i<48;i++)Tick(airborne,1/60f);
Check(MathF.Abs(Native.Velocity().Y+Gravity.Stadium)<.6f,"cars fall at stadium gravity");

// Tyre grip removes side slip; steering reverses when driving backwards.
foreach(float drive in new[]{1f,-1f}) {
 var steering=Car(new(0,.75f,0));
 for(int i=0;i<90;i++)Tick(steering,1/60f);
 Native.SetVelocity(Native.Forward()*drive*8);InputActionMap.Throttle=drive;InputActionMap.Steer=.5f;
 for(int i=0;i<30;i++)Tick(steering,1/60f);
 Check(steering.Grounded && Native.AngularVelocity().Y*drive<-.1f,"forward/reverse surface steering");
}
var grip=Car(new(0,.75f,0));for(int i=0;i<90;i++)Tick(grip,1/60f);
Native.SetVelocity(new(5,0,0));for(int i=0;i<30;i++)Tick(grip,1/60f);
Check(MathF.Abs(Native.Velocity().X)<.3f,"lateral tyre grip");

// A stationary upside-down car must release the ceiling under gravity.
var roof=Car(new(0,ArenaLayout.CeilingHeight-.34f,0));Native.SetRotation(0,0,1,0);
for(int i=0;i<45;i++)Tick(roof,1/60f);
Check(!roof.Grounded && Native.Position().Y<ArenaLayout.CeilingHeight-2 && Native.Velocity().Y<-5,"ceiling falls rather than sticks");

void PressJump(ArcadeCarController c,float pitch=0,float steer=0,float dt=1/120f) {
 InputActionMap.Jump=false;Tick(c,dt);
 InputActionMap.Pitch=pitch;InputActionMap.Steer=steer;
 InputActionMap.Jump=true;Tick(c,dt);
 InputActionMap.Jump=false;InputActionMap.Pitch=0;InputActionMap.Steer=0;
}
ArcadeCarController JumpFromFloor() {
 var c=Car(new(0,.34f,0));for(int i=0;i<30;i++)Tick(c,1/120f);
 Check(c.ResetWheelContacts>=3,"three-wheel grounded setup");
 PressJump(c);Check(c.FlipAvailable && !c.Grounded,"first jump retains flip");
 return c;
}
var doubleJump=JumpFromFloor();float firstSpeed=Native.Velocity().Y;
PressJump(doubleJump);Check(!doubleJump.FlipAvailable && !doubleJump.IsFlipping && Native.Velocity().Y>firstSpeed+6,"neutral second jump");
var beforeThird=Native.Velocity().Y;PressJump(doubleJump);Check(Native.Velocity().Y<beforeThird,"third jump blocked");

foreach(float flipDt in new[]{1/60f,1/120f})
foreach(var input in new[]{new Vector2(0,-1),new Vector2(0,1),new Vector2(-1,0),new Vector2(1,0),new Vector2(1,-1)}) {
 var flip=JumpFromFloor();Native.SetPosition(new(0,10,0));
 var up=Vector3.Normalize(Vector3.Cross(Native.Right(),Native.Forward()));
 var direction=Vector3.Normalize(Native.Forward()*-input.Y+Native.Right()*input.X);
 var axis=Vector3.Normalize(Vector3.Cross(up,direction));
 var initialVelocity=Native.Velocity();PressJump(flip,input.Y,input.X,flipDt);
 Check(flip.IsFlipping && !flip.FlipAvailable && Vector3.Dot(Native.Velocity()-initialVelocity,direction)>8,"directional dodge impulse");
 float rotation=0;
 for(int i=0;i<(int)MathF.Round(1/flipDt);i++){Tick(flip,flipDt);rotation+=Vector3.Dot(Native.AngularVelocity(),axis)*flipDt;}
 Console.WriteLine($"Flip {input}, dt={flipDt}: radians={rotation}");
 Check(!flip.IsFlipping && rotation>5.6f && rotation<7.0f,"directional flip turns about once");
}

var timely=JumpFromFloor();Native.SetPosition(new(0,25,0));
for(int i=0;i<170;i++)Tick(timely,1/120f);
Check(timely.FlipAvailable,"flip retained inside 1.5 second window");
PressJump(timely,-1);Check(timely.IsFlipping,"late valid flip succeeds");
var held=JumpFromFloor();InputActionMap.Jump=true;
for(int i=0;i<30;i++)Tick(held,1/120f);
Check(held.FlipAvailable && !held.IsFlipping,"holding jump cannot consume second jump");

var expired=Car(new(0,25,0));for(int i=0;i<181;i++)Tick(expired,1/120f);
Check(!expired.FlipAvailable,"flip expires after 1.5 seconds");
float expiredSpeed=Native.Velocity().Y;PressJump(expired,-1);Check(!expired.IsFlipping && Native.Velocity().Y<expiredSpeed,"expired dodge denied");

// Mask individual wheel rays to verify the requested 3-of-4 threshold.
Native.SetPosition(new(0,.34f,0));Native.SetRotation(0,1,0,0);Native.SetVelocity(Vector3.Zero);Native.SetAngularVelocity(Vector3.Zero);
Native.SetRayMask(3);Tick(expired,1/120f);Check(expired.ResetWheelContacts==2 && !expired.FlipAvailable,"two floor wheels cannot reset flip");
Native.SetRayMask(7);Tick(expired,1/120f);Check(expired.ResetWheelContacts==3 && expired.FlipAvailable,"three floor wheels reset flip");

var ballReset=Car(new(0,25,0));for(int i=0;i<181;i++)Tick(ballReset,1/120f);
Native.SetPosition(new(0,10,0));Native.SetRotation(0,1,0,0);Native.SetVelocity(Vector3.Zero);Native.SetAngularVelocity(Vector3.Zero);
Native.SetBall(new(0,8.785f,0));Native.SetRayMask(3);
Tick(ballReset,1/120f);Check(ballReset.ResetWheelContacts==2 && !ballReset.FlipAvailable,"two ball wheels cannot reset flip");
Native.SetRayMask(7);Tick(ballReset,1/120f);Check(ballReset.ResetWheelContacts==3 && ballReset.FlipAvailable && !ballReset.Grounded,"three ball wheels restore flip without ball traction");
PressJump(ballReset,-1);Check(ballReset.IsFlipping,"ball reset grants usable dodge");

// Powerslide retains more side slip; rear-only contacts cannot steer the car.
float SideSlip(bool sliding) {
 var c=Car(new(0,.34f,0));for(int i=0;i<30;i++)Tick(c,1/120f);
 InputActionMap.Powerslide=sliding;Native.SetVelocity(new(5,0,0));
 for(int i=0;i<30;i++)Tick(c,1/120f);
 return MathF.Abs(Native.Velocity().X);
}
Check(SideSlip(true)>SideSlip(false)*2,"powerslide reduces tyre grip");
var rearSteer=Car(new(0,.34f,0));Native.SetRayMask(12);Native.SetVelocity(new(0,0,8));InputActionMap.Steer=1;
Tick(rearSteer,1/120f);Check(MathF.Abs(Native.AngularVelocity().Y)<.001f,"no artificial steering without front tyre contacts");
Native.SetRayMask(15);

// Boost remains finite and freezes/resets independently of suspension.
var tank=Car(new(0,10,0)); InputActionMap.Boost=true;
for(int i=0;i<240;i++)Tick(tank,1/60f);
Check(tank.BoostAmount<.001f,"finite boost");Tick(tank,1/60f);Tick(tank,1/60f);Check(!tank.IsBoosting,"empty boost");
Check(tank.CollectBoost(35) && MathF.Abs(tank.BoostAmount-35)<.001f,"pickup");
tank.SetFrozen(true);tank.OnFixedUpdate(1);Check(tank.BoostAmount==35 && !tank.IsBoosting,"frozen boost");
tank.ResetCar();Check(tank.BoostAmount==100,"reset boost");

// A held jump keeps pushing briefly, so it peaks higher than a tap.
float JumpPeak(int heldTicks) {
 var c=Car(new(0,.34f,0));for(int i=0;i<30;i++)Tick(c,1/120f);
 InputActionMap.Jump=true;for(int i=0;i<heldTicks;i++)Tick(c,1/120f);
 InputActionMap.Jump=false;float peak=Native.Position().Y;
 for(int i=0;i<150;i++){Tick(c,1/120f);peak=MathF.Max(peak,Native.Position().Y);}
 return peak;
}
float tapPeak=JumpPeak(1),heldPeak=JumpPeak(36);
Console.WriteLine($"Jump peak: tap={tapPeak}, held={heldPeak}");
Check(heldPeak>tapPeak+0.4f,"held jump rises higher than a tap");

// Boosting along the floor reaches supersonic; coasting drops out of it.
var sonic=Car(new(-50,.34f,0));var east=Quaternion.CreateFromAxisAngle(Vector3.UnitY,-MathF.PI*0.5f);
Native.SetRotation(east.X,east.Y,east.Z,east.W);for(int i=0;i<30;i++)Tick(sonic,1/60f);
InputActionMap.Throttle=1;InputActionMap.Boost=true;
for(int i=0;i<150 && !sonic.IsSupersonic;i++)Tick(sonic,1/60f);
Check(sonic.IsSupersonic,"boost reaches supersonic");
InputActionMap.Throttle=0;InputActionMap.Boost=false;
for(int i=0;i<120;i++)Tick(sonic,1/60f);
Check(!sonic.IsSupersonic,"coasting leaves supersonic");

// Demolition parks the car, survives a match unfreeze, and respawns with full boost.
var wreck=Car(new(0,.34f,0));wreck.RespawnPosition=new(-30,.75f,4);
InputActionMap.Boost=true;for(int i=0;i<30;i++)Tick(wreck,1/60f);InputActionMap.Boost=false;
bool demolishedEvent=false,respawnedEvent=false;
wreck.Demolished+=_=>demolishedEvent=true;wreck.Respawned+=()=>respawnedEvent=true;
wreck.Demolish(new ArcadeCarController());
Check(wreck.IsDemolished && wreck.IsFrozen && demolishedEvent && Native.Position().Y>100,"demolition parks the car");
wreck.SetFrozen(false);Check(wreck.IsFrozen,"unfreezing keeps a wreck parked");
for(int i=0;i<170;i++)wreck.OnUpdate(1/60f);
Check(wreck.IsDemolished && wreck.RespawnTimeRemaining>0,"respawn waits for its delay");
for(int i=0;i<20;i++)wreck.OnUpdate(1/60f);
Check(!wreck.IsDemolished && respawnedEvent && !wreck.IsFrozen && wreck.BoostAmount==100 &&
      Vector3.Distance(Native.Position(),wreck.RespawnPosition)<.01f,"respawn at team position with full boost");
// Full steer at top speed carves a circle instead of spinning out.
var carve=Car(new(-50,.34f,0));Native.SetRotation(east.X,east.Y,east.Z,east.W);for(int i=0;i<30;i++)Tick(carve,1/60f);
InputActionMap.Throttle=1;InputActionMap.Boost=true;for(int i=0;i<90;i++)Tick(carve,1/60f);
InputActionMap.Steer=1;float worstSlip=0,worstYaw=0;
for(int i=0;i<120;i++){
 Tick(carve,1/60f);
 var side=Vector3.Dot(Native.Velocity(),Native.Right());worstSlip=MathF.Max(worstSlip,MathF.Abs(side));
 worstYaw=MathF.Max(worstYaw,MathF.Abs(Native.AngularVelocity().Y));
}
Console.WriteLine($"Carve at speed: worst side slip={worstSlip}, worst yaw rate={worstYaw}");
Check(carve.Grounded && worstSlip<4 && worstYaw<3,"full steer at speed does not spin out");
InputActionMap.Boost=false;InputActionMap.Steer=0;InputActionMap.Throttle=0;

// A dodge cancels the jump's rise, then a pulled-back stick cancels its pitch spin.
var stall=JumpFromFloor();for(int i=0;i<10;i++)Tick(stall,1/120f);
Check(Native.Velocity().Y>2,"rising before the dodge");
PressJump(stall,-1);Check(stall.IsFlipping && MathF.Abs(Native.Velocity().Y)<1,"dodge cancels vertical velocity");
var cancel=JumpFromFloor();Native.SetPosition(new(0,10,0));PressJump(cancel,-1);
var pitchAxis=-Native.Right();float pitched=0;
for(int i=0;i<12;i++){Tick(cancel,1/120f);pitched+=Vector3.Dot(Native.AngularVelocity(),pitchAxis)/120;}
InputActionMap.Pitch=1;
var spinBeforeCancel=Vector3.Dot(Native.AngularVelocity(),pitchAxis);
for(int i=0;i<4;i++){Tick(cancel,1/120f);pitched+=Vector3.Dot(Native.AngularVelocity(),pitchAxis)/120;}
var spinEarly=Vector3.Dot(Native.AngularVelocity(),pitchAxis);
for(int i=0;i<104;i++){Tick(cancel,1/120f);pitched+=Vector3.Dot(Native.AngularVelocity(),pitchAxis)/120;}
InputActionMap.Pitch=0;
Console.WriteLine($"Flip cancel: spin {spinBeforeCancel} -> {spinEarly} after 33 ms, pitched {pitched} rad in total");
Check(spinEarly>1 && spinEarly<spinBeforeCancel-.5f,"flip cancel pushes against the spin rather than stopping it dead");
Check(pitched<2.5f,"pulling back cancels a forward flip");

// Lying on the roof, a jump hops and rolls the car back onto its wheels.
var turtle=Car(new(0,.6f,0));Native.SetRotation(0,0,1,0);for(int i=0;i<30;i++)Tick(turtle,1/60f);
Check(Vector3.Cross(Native.Right(),Native.Forward()).Y<-.9f && !turtle.Grounded,"turtle setup: resting on the roof");
PressJump(turtle,0,0,1/60f);for(int i=0;i<50;i++)Tick(turtle,1/60f);
Check(turtle.Grounded && Vector3.Cross(Native.Right(),Native.Forward()).Y>.95f,"jump rights a car stuck on its roof");

// Left alone on its roof or side, the car rights itself after a moment.
foreach(var (rotation,label) in new[]{(new Quaternion(0,0,1,0),"roof"),(Quaternion.CreateFromAxisAngle(Vector3.UnitZ,MathF.PI/2),"side")}) {
 var still=Car(new(0,.8f,0));Native.SetRotation(rotation.X,rotation.Y,rotation.Z,rotation.W);
 for(int i=0;i<60;i++)Tick(still,1/60f);
 Check(!still.Grounded,$"{label} setup: lying off the wheels");
 for(int i=0;i<150;i++)Tick(still,1/60f);
 Check(still.Grounded && Vector3.Cross(Native.Right(),Native.Forward()).Y>.95f,$"a car left on its {label} rights itself");
}

// Turning circle: radius = speed / yaw rate once the turn has settled.
float TurnRadius(float speed) {
 var c=Car(new(-40,.34f,-20));Native.SetRotation(east.X,east.Y,east.Z,east.W);for(int i=0;i<30;i++)Tick(c,1/60f);
 InputActionMap.Throttle=1;Native.SetVelocity(Native.Forward()*speed);
 for(int i=0;i<10;i++)Tick(c,1/60f);
 InputActionMap.Throttle=speed>20?1:0;InputActionMap.Steer=1;
 for(int i=0;i<45;i++)Tick(c,1/60f);
 var radius=Native.Velocity().Length()/MathF.Max(MathF.Abs(Native.AngularVelocity().Y),1e-3f);
 InputActionMap.Steer=0;InputActionMap.Throttle=0;
 return radius;
}
float slowRadius=TurnRadius(8),fastRadius=TurnRadius(23);
Console.WriteLine($"Turning radius: {slowRadius} m at 8 m/s, {fastRadius} m at 23 m/s");
Check(slowRadius<4.5f && fastRadius<9.5f,"tight turning circle at low and top drive speed");

// Air control: a held input spins up, and releasing it stops the car where it points.
var air=Car(new(0,25,0));InputActionMap.Pitch=1;for(int i=0;i<30;i++)Tick(air,1/60f);
var spinning=Native.AngularVelocity().Length();InputActionMap.Pitch=0;
for(int i=0;i<40;i++)Tick(air,1/60f);
Console.WriteLine($"Air control: held={spinning}, released={Native.AngularVelocity().Length()}");
Check(spinning>3 && Native.AngularVelocity().Length()<.6f,"air rotation responds and settles on release");
Console.WriteLine("PASS: Bullet ride height, 60/120 Hz boosted and unboosted floor-bank-wall traversal and ceiling release, rounded corner, wall jump, airborne gravity, front tyre steering, powerslide, 1.5s flips, 3-wheel floor/ball resets, finite boost, variable jump height, supersonic, demolition respawn, carving without spin-out, dodge stall and pushing flip cancel, air control, stadium gravity, turtle righting, auto-righting, turning radius");


return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine(exception);
    return 1;
}
