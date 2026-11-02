#pragma once
#include <d3d12.h>
#include <string>
#include <cstdint>

// Owned, pointer-free equality key. Never hash struct memory (padding/pointers).
// Full bytecode equality also protects callers that do not supply a resource version.
inline std::string Dx12PipelineKey(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d,
    const std::string& identity, std::uint64_t version, const void* root, size_t rootSize) {
    std::string key;
    auto bytes=[&](const void* p,size_t n){key.append(reinterpret_cast<const char*>(&n),sizeof(n));if(n)key.append(static_cast<const char*>(p),n);};
    auto value=[&](auto v){key.append(reinterpret_cast<const char*>(&v),sizeof(v));};
    bytes(identity.data(),identity.size());value(version);bytes(root,rootSize);
    for(auto shader:{d.VS,d.PS,d.DS,d.HS,d.GS})bytes(shader.pShaderBytecode,shader.BytecodeLength);
    value(d.StreamOutput.NumEntries);for(UINT i=0;i<d.StreamOutput.NumEntries;++i){const auto& e=d.StreamOutput.pSODeclaration[i];value(e.Stream);bytes(e.SemanticName,std::char_traits<char>::length(e.SemanticName));value(e.SemanticIndex);value(e.StartComponent);value(e.ComponentCount);value(e.OutputSlot);}
    value(d.StreamOutput.NumStrides);for(UINT i=0;i<d.StreamOutput.NumStrides;++i)value(d.StreamOutput.pBufferStrides[i]);value(d.StreamOutput.RasterizedStream);
    value(d.BlendState.AlphaToCoverageEnable);value(d.BlendState.IndependentBlendEnable);
    for(UINT i=0;i<d.NumRenderTargets;++i){const auto& b=d.BlendState.RenderTarget[d.BlendState.IndependentBlendEnable?i:0];value(b.BlendEnable);value(b.LogicOpEnable);if(b.BlendEnable){value(b.SrcBlend);value(b.DestBlend);value(b.BlendOp);value(b.SrcBlendAlpha);value(b.DestBlendAlpha);value(b.BlendOpAlpha);}if(b.LogicOpEnable)value(b.LogicOp);value(b.RenderTargetWriteMask);}
    value(d.SampleMask);const auto& r=d.RasterizerState;
    value(r.FillMode);value(r.CullMode);value(r.FrontCounterClockwise);value(r.DepthBias);value(r.DepthBiasClamp);value(r.SlopeScaledDepthBias);value(r.DepthClipEnable);value(r.MultisampleEnable);value(r.AntialiasedLineEnable);value(r.ForcedSampleCount);value(r.ConservativeRaster);
    const auto& z=d.DepthStencilState;value(z.DepthEnable);if(z.DepthEnable){value(z.DepthWriteMask);value(z.DepthFunc);}value(z.StencilEnable);
    if(z.StencilEnable){value(z.StencilReadMask);value(z.StencilWriteMask);for(auto face:{z.FrontFace,z.BackFace}){value(face.StencilFailOp);value(face.StencilDepthFailOp);value(face.StencilPassOp);value(face.StencilFunc);}}
    value(d.InputLayout.NumElements);for(UINT i=0;i<d.InputLayout.NumElements;++i){const auto& e=d.InputLayout.pInputElementDescs[i];std::string semantic=e.SemanticName;for(char& c:semantic)if(c>='a'&&c<='z')c-=32;bytes(semantic.data(),semantic.size());value(e.SemanticIndex);value(e.Format);value(e.InputSlot);value(e.AlignedByteOffset);value(e.InputSlotClass);value(e.InstanceDataStepRate);}
    value(d.IBStripCutValue);value(d.PrimitiveTopologyType);value(d.NumRenderTargets);for(UINT i=0;i<d.NumRenderTargets;++i)value(d.RTVFormats[i]);value(d.DSVFormat);value(d.SampleDesc.Count);value(d.SampleDesc.Quality);value(d.NodeMask);value(d.Flags);
    // CachedPSO is a driver compilation hint, not graphics state.
    return key;
}
