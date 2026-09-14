// Ocean-only screen-space effects consume opaque scene buffers, never ocean output.
bool oceanProject(float3 world, out float2 uv, out float depth)
{
    float4 clip = mul(pass.projection,mul(pass.view,float4(world,1)));
    uv = 0; depth = 0;
    if (clip.w <= .00001) return false;
    float3 ndc = clip.xyz/clip.w;
    depth = pass.zeroToOneDepth != 0 ? ndc.z : ndc.z*.5+.5;
    if (pass.flipY != 0) ndc.y = -ndc.y;
    uv = ndc.xy*.5+.5;
    return all(uv>0) && all(uv<1) && depth>=0 && depth<=1;
}
float oceanWallContact(float3 world)
{
    if (contact.x <= .001 || max(contact.y,max(contact.z,contact.w)) <= 0) return 0;
    float2 uv; float projectedDepth;
    if (!oceanProject(world,uv,projectedDepth)) return 0;
    float viewDistance = max(-mul(pass.view,float4(world,1)).z,.1);
    float2 radius = abs(float2(pass.projection[0][0],pass.projection[1][1]))*contact.x/(2.0*viewDistance);
    float3 up = normalize(mul(float3(0,1,0),(float3x3)pass.worldToLocal));
    float nearest = contact.x;
    for (int ring=1;ring<=2;++ring)
        for (int tap=0;tap<8;++tap)
        {
            float angle = float(tap)*.785398163;
            float2 at = uv+float2(cos(angle),sin(angle))*radius*(float(ring)*.5);
            if (any(at<=0) || any(at>=1)) continue;
            float depth = depthTexture.SampleLevel(at,0).r;
            if (depth<=.000001) continue;
            float3 normal = decodeNormal(normalTexture.SampleLevel(at,0).rgb);
            if (abs(dot(normal,up))>.7) continue; // Floors do not count as nearby walls.
            float3 receiver = reconstructWorldPosition(at,depth,pass.inverseViewProjection,pass.flipY,pass.zeroToOneDepth);
            nearest = min(nearest,length(receiver-world));
        }
    return 1.0-smoothstep(0.0,contact.x,nearest);
}
float4 oceanSceneReflection(float3 world, float3 normal, float3 direction, float roughness)
{
    if (reflections.x<=0) return 0;
    float3 origin = world+normal*.04;
    float previous = 0;
    const int steps = clamp(int(reflections.w),8,64);
    bool hadFront = false;
    for (int step=1;step<=steps;++step)
    {
        float fraction = float(step)/float(steps);
        float travel = .05+reflections.y*fraction*fraction;
        float3 at = origin+direction*travel;
        float2 uv;float rayDepth;
        if (!oceanProject(at,uv,rayDepth)) break;
        float depth = depthTexture.SampleLevel(uv,0).r;
        bool behind = depth>.000001 && depth>=rayDepth;
        if (behind && hadFront)
        {
            float low = previous, high = travel;
            for (int refinement=0;refinement<6;++refinement)
            {
                float middle = (low+high)*.5;
                float2 middleUv;float middleDepth;
                if (!oceanProject(origin+direction*middle,middleUv,middleDepth)) break;
                float sampled = depthTexture.SampleLevel(middleUv,0).r;
                if (sampled>.000001 && sampled>=middleDepth) high=middle; else low=middle;
            }
            at=origin+direction*high;
            if (!oceanProject(at,uv,rayDepth)) return 0;
            depth=depthTexture.SampleLevel(uv,0).r;
            float3 hit = reconstructWorldPosition(uv,depth,pass.inverseViewProjection,pass.flipY,pass.zeroToOneDepth);
            float delta = mul(pass.view,float4(hit-at,0)).z;
            float3 hitNormal = decodeNormal(normalTexture.SampleLevel(uv,0).rgb);
            float3 local = mul(pass.worldToLocal,float4(hit,1)).xyz;
            // Reject depth discontinuities, backfaces and submerged floors.
            if (depth<=.000001 || delta<0 || delta>reflections.z || dot(direction,hitNormal)>=-.01 || local.y<0) return 0;
            float confidence = edgeFade(uv,.06)*(1.0-smoothstep(.75,1.0,high/reflections.y));
            confidence *= reflections.x*(1.0-roughness*roughness);
            return float4(sceneTexture.SampleLevel(uv,0).rgb,confidence);
        }
        if (!behind) hadFront=true;
        previous=travel;
    }
    return 0; // The caller retains physical-sky lighting when a ray leaves the screen or misses.
}
