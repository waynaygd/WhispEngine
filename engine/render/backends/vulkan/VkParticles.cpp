#include "VkRenderAdapter.h"
#if defined(ENABLE_VULKAN)
#include <cstring>
#include <stdexcept>
void VkRenderAdapter::ParticleFrame(){
 if(m_ParticleQueries&&m_ParticleQueryCount[m_Frame]){std::uint64_t ticks[16]{};auto count=m_ParticleQueryCount[m_Frame];if(vkGetQueryPoolResults(m_Device,m_ParticleQueries,m_Frame*16,count,sizeof(ticks),ticks,sizeof(std::uint64_t),VK_QUERY_RESULT_64_BIT)==VK_SUCCESS){double elapsed=0;for(unsigned i=0;i<count;i+=2)elapsed+=double(ticks[i+1]-ticks[i])*m_TimestampPeriod/1e6;m_ParticleStats.gpuMs=elapsed;m_ParticleStats.timingValid=true;}}
 m_ParticleQueryCount[m_Frame]=0;m_ParticleUsed=0;m_ParticleStats.drawCalls=m_ParticleStats.bufferGrowths=m_ParticleStats.uploadBytes=0;
}
void VkRenderAdapter::DrawParticles(const ParticleDrawList& draw){
 if(m_Skip||!m_Recording||draw.vertices.empty()||draw.vertices.size()>60000)return;
 auto command=m_CmdBuffers[m_ImageIndex];auto& query=m_ParticleQueryCount[m_Frame];bool timing=m_ParticleQueries&&query<16;if(timing)vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,m_ParticleQueries,m_Frame*16+query);
 auto bytes=draw.vertices.size()*sizeof(ParticleVertex);auto& buffer=m_ParticleBuffers[m_Frame];auto required=m_ParticleUsed+bytes;if(buffer.capacity<required){if(buffer.buffer){Mesh retired;retired.vertices=buffer;m_RetiredMeshes[m_Frame].push_back(std::move(retired));}m_ParticleUsed=0;auto capacity=VkDeviceSize(65536);while(capacity<required)capacity*=2;buffer=MakeBuffer(capacity,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);++m_ParticleStats.bufferGrowths;}
 void* memory;if(vkMapMemory(m_Device,buffer.memory,0,buffer.capacity,0,&memory)!=VK_SUCCESS)throw std::runtime_error("Vulkan particle vertex mapping failed");std::memcpy(static_cast<char*>(memory)+m_ParticleUsed,draw.vertices.data(),bytes);vkUnmapMemory(m_Device,buffer.memory);VkDeviceSize offset=m_ParticleUsed;m_ParticleUsed+=bytes;m_ParticleStats.uploadBytes+=bytes;
 vkCmdBindVertexBuffers(command,0,1,&buffer.buffer,&offset);VkRect2D clip{{0,0},m_SwapExtent};vkCmdSetScissor(command,0,1,&clip);
 for(auto& batch:draw.commands){auto texture=m_Textures.find(batch.texture.value);if(texture==m_Textures.end()||batch.first>draw.vertices.size()||batch.count>draw.vertices.size()-batch.first)continue;vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,m_ParticlePso[batch.additive?1:0]);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,m_SceneLayout,0,1,&texture->second.set,0,nullptr);vkCmdDraw(command,batch.count,1,batch.first,0);++m_ParticleStats.drawCalls;++m_Statistics.drawCalls;}
 if(timing){vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,m_ParticleQueries,m_Frame*16+query+1);query+=2;}
}
#endif
