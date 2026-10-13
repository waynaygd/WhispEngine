struct SceneLight { float4 positionRange; float4 directionType; float4 colorIntensity; float4 coneAttenuation; float4 screenBounds; };
StructuredBuffer<SceneLight> Lights:register(t0);
RWStructuredBuffer<uint2> Counts:register(u0);
RWStructuredBuffer<uint> Indices:register(u1);
RWStructuredBuffer<uint> Stats:register(u2);
cbuffer CullCB:register(b0){uint LightCount, TilesX, TilesY, Capacity; uint Width, Height, Reserved0, Reserved1;};
groupshared uint Masks[2];
groupshared uint RunningCount;
// One group per tile, testing 64 lights in parallel. Bit-prefix compaction preserves
// ascending light order without per-pixel atomics or a nondeterministic append list.
[numthreads(64,1,1)]
void CSMain(uint3 group:SV_GroupID,uint3 thread:SV_GroupThreadID)
{
    uint lane=thread.x,tile=group.y*TilesX+group.x;
    uint2 xy=group.xy;
    float2 lo=float2(xy*16)/float2(Width,Height);
    float2 hi=float2(min((xy+1)*16,uint2(Width,Height)))/float2(Width,Height);
    if(lane==0)RunningCount=0;
    for(uint first=0;first<LightCount;first+=64){
        if(lane<2)Masks[lane]=0;
        GroupMemoryBarrierWithGroupSync();
        uint light=first+lane;bool match=false;
        if(light<LightCount){float4 b=Lights[light].screenBounds;match=b.x<=hi.x&&b.y<=hi.y&&b.z>=lo.x&&b.w>=lo.y;}
        uint unused;
        if(match)InterlockedOr(Masks[lane/32],1u<<(lane%32),unused);
        GroupMemoryBarrierWithGroupSync();
        uint preceding=(lane%32)==0?0:((1u<<(lane%32))-1u);
        uint prefix=countbits(Masks[lane/32]&preceding)+(lane>=32?countbits(Masks[0]):0);
        uint index=RunningCount+prefix;
        if(match&&index<Capacity)Indices[tile*Capacity+index]=light;
        GroupMemoryBarrierWithGroupSync();
        if(lane==0)RunningCount+=countbits(Masks[0])+countbits(Masks[1]);
        GroupMemoryBarrierWithGroupSync();
    }
    if(lane==0){
        uint count=RunningCount,unused;
        Counts[tile]=uint2(min(count,Capacity),count>Capacity?1:0);
        InterlockedAdd(Stats[0],min(count,Capacity),unused);
        InterlockedAdd(Stats[1],count,unused);
        InterlockedMax(Stats[2],count,unused);
        if(count>Capacity)InterlockedAdd(Stats[3],1,unused);
    }
}
