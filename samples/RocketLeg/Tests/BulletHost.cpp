#include <btBulletDynamicsCommon.h>
#include "PlutoGE/scene/components/ColliderComponent.h"
#include <memory>
#include <vector>

// Thin headless host for the real sample controller; collisions and suspension
// rays use the same Bullet library as PlutoGE. No renderer or window is needed.
static btDefaultCollisionConfiguration config;
static btCollisionDispatcher dispatcher(&config);
static btDbvtBroadphase broadphase;
static btSequentialImpulseConstraintSolver solver;
static btDiscreteDynamicsWorld world(&dispatcher, &broadphase, &solver, &config);
static std::vector<std::unique_ptr<btCollisionShape>> shapes;
static std::vector<std::unique_ptr<btRigidBody>> bodies;
static btRigidBody* car = nullptr;
static btRigidBody* ball = nullptr;
static int rayMask = 15;
static btTriangleMesh triangles;
struct Vec { float x,y,z; };
static btVector3 v(Vec p) { return {p.x,p.y,p.z}; }
static Vec v(const btVector3& p) { return {p.x(),p.y(),p.z()}; }
#define API extern "C" __declspec(dllexport)
static btRigidBody* add(std::unique_ptr<btCollisionShape> shape, float mass, btVector3 pos) {
    btVector3 inertia(0,0,0); if(mass) shape->calculateLocalInertia(mass,inertia);
    auto body = std::make_unique<btRigidBody>(btRigidBody::btRigidBodyConstructionInfo(mass,nullptr,shape.get(),inertia));
    body->setWorldTransform(btTransform(btQuaternion(0,0,0,1),pos));
    body->setFriction(mass ? .05f : .5f);
    world.addRigidBody(body.get());auto result=body.get();
    shapes.push_back(std::move(shape));bodies.push_back(std::move(body));return result;
}
API void Initialize(float* vertices, int count) {
    world.setGravity({0,-9.81f,0});
    for(int i=0;i<count;i+=9) triangles.addTriangle({vertices[i],vertices[i+1],vertices[i+2]}, {vertices[i+3],vertices[i+4],vertices[i+5]}, {vertices[i+6],vertices[i+7],vertices[i+8]});
    add(std::make_unique<btBvhTriangleMeshShape>(&triangles,true),0,{0,0,0});
    add(std::make_unique<btBoxShape>(btVector3(35,.25f,21)),0,{0,-.25f,0});
    auto box=std::make_unique<btBoxShape>(btVector3(.552f,.15f,.88f));
    auto compound=std::make_unique<btCompoundShape>();
    compound->addChildShape(btTransform(btQuaternion(0,0,0,1),{0,.04f,0}),box.get());
    shapes.push_back(std::move(box));
    car=add(std::move(compound),850,{0,.75f,16});
    PlutoGE::scene::ColliderComponent collider({.center={0,.1f,0},.size={.92f,.75f,.88f}});
    car->setCcdSweptSphereRadius(collider.GetCcdSweptSphereRadius({1.2f,.4f,2}));
    car->setCcdMotionThreshold(.0001f);
    car->setDamping(.04f,0);
    ball=add(std::make_unique<btSphereShape>(1.1f),18,{0,-100,0});
    ball->setGravity({0,0,0});
}
API void SetRayMask(int mask) { rayMask=mask; }
API void SetBall(Vec p) { ball->setWorldTransform(btTransform(btQuaternion(0,0,0,1),v(p)));ball->setLinearVelocity({0,0,0});ball->setAngularVelocity({0,0,0});world.updateSingleAabb(ball); }
API Vec BallPointVelocity(Vec p) { return v(ball->getVelocityInLocalPoint(v(p)-ball->getCenterOfMassPosition())); }
API void Step(float dt) { world.stepSimulation(dt,0); }
API void SetPosition(Vec p) { auto t=car->getWorldTransform();t.setOrigin(v(p));car->setWorldTransform(t);car->setInterpolationWorldTransform(t);world.updateSingleAabb(car);car->activate(true); }
API Vec Position() { return v(car->getWorldTransform().getOrigin()); }
API Vec Right() { return v(car->getWorldTransform().getBasis().getColumn(0)); }
API Vec Forward() { return v(-car->getWorldTransform().getBasis().getColumn(2)); }
API void SetRotation(float x,float y,float z,float w) { auto t=car->getWorldTransform();t.setRotation({x,y,z,w});car->setWorldTransform(t);car->setInterpolationWorldTransform(t);world.updateSingleAabb(car); }
API Vec Velocity() { return v(car->getLinearVelocity()); }
API Vec AngularVelocity() { return v(car->getAngularVelocity()); }
API void SetVelocity(Vec p) { car->setLinearVelocity(v(p));car->activate(true); }
API void SetAngularVelocity(Vec p) { car->setAngularVelocity(v(p)); }
API Vec PointVelocity(Vec p) { return v(car->getVelocityInLocalPoint(v(p)-car->getCenterOfMassPosition())); }
API void Force(Vec f) { car->applyCentralForce(v(f));car->activate(true); }
API void ForceAt(Vec f,Vec p) { car->applyForce(v(f),v(p)-car->getCenterOfMassPosition());car->activate(true); }
API void Impulse(Vec f) { car->applyCentralImpulse(v(f));car->activate(true); }
API void Clear() { car->clearForces(); }
struct IgnoreCar : btCollisionWorld::ClosestRayResultCallback {
    IgnoreCar(btVector3 from,btVector3 to):ClosestRayResultCallback(from,to){}
    bool needsCollision(btBroadphaseProxy* proxy) const override {
        return proxy->m_clientObject!=car && ClosestRayResultCallback::needsCollision(proxy);
    }
};
API int Ray(Vec origin,Vec direction,float length,Vec* point,Vec* normal,float* distance) {
    auto local=car->getWorldTransform().inverse()*v(origin);
    int wheel=(local.z()<0 ? 0:2)+(local.x()>0 ? 1:0);
    if(!(rayMask & (1<<wheel)))return 0;
    auto from=v(origin),to=from+v(direction)*length;IgnoreCar hit(from,to);world.rayTest(from,to,hit);
    if(!hit.hasHit())return 0;
    *point=v(hit.m_hitPointWorld);*normal=v(hit.m_hitNormalWorld);*distance=length*hit.m_closestHitFraction;return hit.m_collisionObject==ball ? 2 : 1;
}
