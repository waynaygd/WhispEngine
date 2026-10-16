#include <map>
#include <cstring>
#pragma once
#include "../../IRenderAdapter.h"

#ifdef _WIN32
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

struct GLFWwindow;
class Dx12RenderAdapter final : public IRenderAdapter
{
public:
	bool Initialize(IWindow* window) override;
	bool InitializeEditorUi(IWindow* window) override;
	void BeginEditorUiFrame() override;
	void RenderEditorUiFrame() override;
	bool BeginViewportRender(int width, int height, const float* clearColor) override;
	void EndViewportRender() override;
	std::uint64_t GetViewportTextureId() const override;
    std::uint64_t GetTexturePreviewId(RenderTextureHandle h)const override {auto it=m_UploadedTextures.find(h.value);return it==m_UploadedTextures.end()?0:it->second.gpuSrvHandle.ptr;}
	void BeginFrame() override;
	void Clear(float r, float g, float b, float a) override;
	void DrawTestTriangle() override;
	void DrawTestLine() override;
	void DrawTestQuad() override;
	void DrawTestCube() override;
	void EndFrame() override;
	void Present() override;
	void ShutdownEditorUi() override;
	void Shutdown() override;

	void SetTestTransform(const float* mvp16) override;
	void SetTestColor(float r, float g, float b, float a) override;
	RenderMeshHandle UploadMesh(const MeshData& meshData) override;
	void DestroyMesh(RenderMeshHandle handle) override;
	RenderTextureHandle CreateTexture2D(const TextureData& textureData) override;
	void DestroyTexture(RenderTextureHandle handle) override;
	RenderShaderHandle CreateShaderProgram(const ShaderResource& shaderResource) override;
	void DestroyShader(RenderShaderHandle handle) override;
	void BindShader(RenderShaderHandle handle) override;
	void BindTexture(std::uint32_t slot, RenderTextureHandle handle) override;
	void DrawMesh(RenderMeshHandle handle) override;
    void SetFog(const FogFrame& frame)override{m_Fog=frame;}
    FogDiagnostics GetFogDiagnostics()const override{return m_FogDiagnostics;}
    void SetAntiAliasing(AntiAliasingMode mode) override {m_AntiAliasing=AntiAliasingMode(std::clamp(int(mode),0,2));}
    AntiAliasingDiagnostics GetAntiAliasingDiagnostics()const override {AntiAliasingDiagnostics d;d.mode=m_AntiAliasing;for(const auto& t:m_HdrTargets)if(t.color)d.intermediateBytes+=t.allocationBytes;return d;}
    void SetEnvironmentSettings(const EnvironmentSettings& s) override {m_EnvironmentSettings=SanitizeEnvironment(s);}
    void SetSceneLights(std::span<const GpuSceneLight>,LightRenderingMode,const SceneLightDiagnostics& = {}) override;
    SceneLightDiagnostics GetSceneLightDiagnostics() const override {return m_SceneLightDiagnostics;}
    std::vector<std::uint32_t> ReadBackLightTiles();
    bool UploadEnvironment(const EnvironmentResource&,std::uint64_t) override;
    void ReleaseEnvironment() override;
    EnvironmentDiagnostics GetEnvironmentDiagnostics() const override {return m_EnvironmentDiagnostics;}
    std::vector<float> ReadBackHdrPixels();
    std::vector<float> ReadBackEnvironment(unsigned resource,unsigned subresource=0);
    void SetSurfaceParameters(const std::array<float,4>& p) override { m_Surface=p; }
    void SetTextureParameters(const std::array<float,4>& p) override { m_TextureParameters=p; }
    void SetBaseColor(const std::array<float,4>& p) override { m_BaseColor=p; }
    void SetCameraPosition(const std::array<float,4>& p) override { if(std::memcmp(m_Camera.data(),p.data(),sizeof(p))!=0){m_Camera=p;m_FrameConstantsDirty=true;} }
    void SetLighting(const LightingConstants& c) override { if(std::memcmp(&m_Lighting,&c,sizeof(c))!=0){m_Lighting=c;m_FrameConstantsDirty=true;} }
    void SetNormalMatrix(const std::array<float,12>& n) override { m_Normal=n; }
    bool SupportsInstancing(RenderShaderHandle handle) const override;
    bool DrawMeshInstanced(RenderMeshHandle handle, std::span<const RenderInstanceData> instances) override;
    RenderSubmissionStatistics GetSubmissionStatistics() const override;
    // Diagnostics only, after EndFrame and before Present; waits for readback.
    std::vector<std::uint8_t> ReadBackFramePixels();
    std::vector<std::uint8_t> ReadBackViewportPixels();


    void SetMeshRange(const RenderMeshRange& range) override {m_MeshRange=range;}
    void SetMaterialState(const MaterialRenderState& state) override {m_MaterialState=state;}
    void SetModelMatrix(const std::array<float,16>& m) override { m_Model=m; }
    bool BeginShadowPass(const ShadowFrame&) override;
    void DrawShadowMesh(RenderMeshHandle,const RenderInstanceData&) override;
    bool DrawShadowMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData>) override;
    void EndShadowPass() override;
    void SetEditorVisuals(const EditorVisualFrame& f) override {m_EditorVisualFrame=f;m_EditorVisualDiagnostics={};}
    bool UploadEditorSky(const EnvironmentResource&,std::uint64_t) override;
    EditorVisualDiagnostics GetEditorVisualDiagnostics()const override{return m_EditorVisualDiagnostics;}
    void BeginMainPass() override;
    void EndMainPass() override;
    std::uint64_t GetShadowPreviewTextureId() const override;
    std::array<unsigned,2> ProbeMsaaSupport(unsigned samples)const {std::array<unsigned,2> result{};int index=0;for(auto format:{DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_D32_FLOAT}){D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS data{format,samples,D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,0};if(m_Device&&SUCCEEDED(m_Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&data,sizeof(data))))result[index]=data.NumQualityLevels;++index;}return result;}
    RenderGpuTimings GetGpuTimings() const override { return m_GpuTimings; }
    std::vector<float> ReadBackShadowDepth();
    unsigned GetValidationErrorCount() const;
    struct MaterialBindingRecord {unsigned frameSlot=0;std::array<std::uint64_t,5> textures{};std::uint64_t descriptor=0;unsigned availableMaps=0;};
    void SetMaterialBindingTrace(bool enabled){m_TraceMaterialBindings=enabled;}
    const auto& GetMaterialBindingTrace()const{return m_MaterialBindingTrace;}

    void ReleaseShadowResources();
    bool SaveFrameImage(const std::string&) override;

private:
    GLFWwindow* m_EditorUiWindow=nullptr;
    float m_EditorUiScale=1;
    EditorVisualFrame m_EditorVisualFrame;
    EditorVisualDiagnostics m_EditorVisualDiagnostics;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_EditorVisualRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_EditorVisualPso[2][2];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_EditorSky;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_EditorSkySrv;
    std::string m_EditorSkyKey,m_EditorVisualError;
    std::uint64_t m_EditorSkyVersion=0;
    bool m_EditorVisualAttempted=false;
    bool EnsureEditorVisualPipeline();
    void DrawEditorVisual(bool grid);
    void ReleaseEditorVisuals();
    struct LightFrameResources {
        Microsoft::WRL::ComPtr<ID3D12Resource> lights,counts,indices,stats,readback;
        std::uint64_t lightCapacity=0,tileCapacity=0;
        bool pending=false,optimized=false;
        UINT count=0,tilesX=1,tilesY=1,width=1,height=1;
    };
    std::vector<LightFrameResources> m_LightFrames[2];
    std::vector<GpuSceneLight> m_PendingLights;
    SceneLightDiagnostics m_SceneLightDiagnostics;
    LightRenderingMode m_LightMode=LightRenderingMode::Tiled;
    UINT m_LightPassCursor=0,m_ActiveLightPass=UINT_MAX;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_LightCullRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_LightCullPso;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_LightTimeHeap;
    bool m_LightCullAttempted=false;
    std::string m_LightCullError;
    static constexpr UINT LightTileCapacity=256,MaxLightPasses=32,MaxSceneLights=16384;
    void PrepareSceneLights();
    void BindSceneLights();
    void CollectSceneLights(UINT);
    void ReleaseSceneLights();
    bool EnsureLightCulling();
    RenderMeshRange m_MeshRange;
    MaterialRenderState m_MaterialState;
    std::array<float,4> m_Surface{0,.5f,0,0},m_BaseColor{1,1,1,1},m_Camera{0,0,-2.25f,0};
    UINT MaterialIndexCount(UINT total) const {return m_MeshRange.firstIndex<=total?(m_MeshRange.indexCount?((m_MeshRange.indexCount<=total-m_MeshRange.firstIndex)?m_MeshRange.indexCount:0):total-m_MeshRange.firstIndex):0;}
	bool CreateDevice(HWND hwnd);
	bool CreateCommandObjects();
	bool CreateSwapchain(HWND hwnd);
	bool CreateRtvHeapAndTargets();
	bool CreateDepthResources();
	bool ResizeBackBufferResources(UINT width, UINT height);
	void ResizeBackBufferIfNeeded();
	bool CreateSyncObjects();
	bool CreatePipelineAndAssets();
	bool EnsureViewportResources(UINT width, UINT height);
	void ReleaseViewportResources();

	void WaitForGpu();
	void MoveToNextFrame();
	static void AllocateImGuiDescriptor(
		struct ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);
	static void FreeImGuiDescriptor(
		struct ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

	static constexpr UINT FrameCount = 2;
    static constexpr UINT MaxDrawsPerFrame = 2048;


    bool EnsureShadowPipeline(const ShadowSettings&);
    bool EnsureShadowTarget(UINT);
    void BindShadows();
    bool UploadInstances(std::span<const RenderInstanceData>,D3D12_VERTEX_BUFFER_VIEW&);
    void InitializeGpuTiming();
    void CollectGpuTiming(UINT);
    void ShadowTimestamp(UINT);
    struct ShadowTarget {
        Microsoft::WRL::ComPtr<ID3D12Resource> depth;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};
        UINT descriptor=UINT_MAX,resolution=0;
    };
    ShadowTarget m_ShadowTargets[FrameCount];
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_ShadowRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_ShadowPso,m_ShadowInstancePso,m_ShadowDoublePso,m_ShadowDoubleInstancePso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_ShadowMirrorPso,m_ShadowMirrorInstancePso;
    Microsoft::WRL::ComPtr<ID3DBlob> m_ShadowVs,m_ShadowInstanceVs;
    int m_ShadowDepthBias=-1;
    float m_ShadowSlopeBias=-1;
    std::string m_LastShadowError;
    bool m_ShadowResourceWarning=false;
    ShadowConstants m_ShadowConstants{};
    UINT m_NullShadowDescriptor=UINT_MAX;
    D3D12_GPU_DESCRIPTOR_HANDLE m_NullShadowSrv{};
    bool m_ShadowActive=false,m_PreparedScene=false;
    UINT m_LastShadowSlot=0;
    std::array<float,16> m_Model{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    struct CachedInstances { const RenderInstanceData* data; std::size_t count; D3D12_VERTEX_BUFFER_VIEW view; };
    std::vector<CachedInstances> m_CachedInstances;
    static constexpr UINT TimingQueries=98;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_TimeHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_TimeReadback[FrameCount];
    UINT64 m_TimeFrequency=0;
    UINT m_TimePassCount=0,m_TimeCurrentPass=UINT_MAX;
    UINT m_TimePending[FrameCount]{};
    bool m_TimeOverflow=false,m_TimeComplete[FrameCount]{};
    RenderGpuTimings m_GpuTimings{};

	struct UploadedMesh
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
		D3D12_VERTEX_BUFFER_VIEW vertexView{};
		Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
		D3D12_INDEX_BUFFER_VIEW indexView{};
		UINT indexCount = 0;
	};

	struct UploadedTexture
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> texture;
		UINT width = 0;
		UINT height = 0;
        UINT64 allocationBytes=0;
		UINT descriptorIndex = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuSrvHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpuSrvHandle{};
	};

	struct UploadedShader
	{
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
        bool srgbBaseColor=false;
        std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,6> hdrStates;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> instancedPipelineState;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> doublePipelineState,doubleInstancedPipelineState;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> mirrorPipelineState,mirrorInstancedPipelineState;
	};

	struct ViewportTarget
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> color;
		Microsoft::WRL::ComPtr<ID3D12Resource> depth;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
		D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
		D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
		D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
		UINT width = 0;
		UINT height = 0;
        UINT64 allocationBytes=0;
		UINT srvDescriptorIndex = UINT_MAX;
		bool rendering = false;
	};

	Microsoft::WRL::ComPtr<IDXGIFactory6> m_Factory;
	Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_Queue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_Allocator[FrameCount];
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CmdList;

	Microsoft::WRL::ComPtr<IDXGISwapChain3> m_Swapchain;
	HWND m_Hwnd = nullptr;
	UINT m_FrameIndex = 0;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
	UINT m_RtvSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_Rt[FrameCount];
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
	UINT m_DsvSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_Depth;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
	UINT m_SrvDescriptorSize = 0;

	Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
	UINT64 m_FenceValue = 0;
	HANDLE m_FenceEvent = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_Pso;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_LinePso;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_VB;
	D3D12_VERTEX_BUFFER_VIEW m_VbView{};
	Microsoft::WRL::ComPtr<ID3D12Resource> m_LineVB;
	D3D12_VERTEX_BUFFER_VIEW m_LineVbView{};
	Microsoft::WRL::ComPtr<ID3D12Resource> m_QuadVB;
	D3D12_VERTEX_BUFFER_VIEW m_QuadVbView{};
	Microsoft::WRL::ComPtr<ID3D12Resource> m_CubeVB;
	D3D12_VERTEX_BUFFER_VIEW m_CubeVbView{};

	float m_ClearColor[4] = { 0.08f, 0.08f, 0.12f, 1.0f };

	UINT m_Width = 1280;
	UINT m_Height = 720;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_Cb[FrameCount][MaxDrawsPerFrame];
	uint8_t* m_CbMapped[FrameCount][MaxDrawsPerFrame] = {};
	D3D12_GPU_VIRTUAL_ADDRESS m_CbGpu[FrameCount][MaxDrawsPerFrame] = {};
	UINT m_DrawCbIndex = 0;
    struct InstancePage {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint8_t* mapped=nullptr;
        std::size_t capacity=0,used=0;
    };
    void BindFrameConstants();
    std::vector<InstancePage> m_FrameConstantPages[FrameCount];
    std::size_t m_FrameConstantPageIndex=0;
    D3D12_GPU_VIRTUAL_ADDRESS m_FrameConstantAddress=0;
    std::array<std::uint8_t,768> m_LastFrameConstants{};
    bool m_FrameConstantsDirty=true;
    std::vector<InstancePage> m_InstancePages[FrameCount];
    std::size_t m_InstancePageIndex=0;
    UINT64 m_InstanceFrameFence[FrameCount]{};
    RenderSubmissionStatistics m_SubmissionStatistics;
    bool m_FrameRecording=false;
    struct EnvironmentGpu {
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,4> textures;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptors;
    };
    EnvironmentGpu m_EnvironmentGpu;
    EnvironmentSettings m_EnvironmentSettings;
    EnvironmentDiagnostics m_EnvironmentDiagnostics;
    D3D12_GPU_DESCRIPTOR_HANDLE m_EnvironmentTable{};
    bool BindEnvironment();
    Microsoft::WRL::ComPtr<ID3D12Resource> m_CachedBrdfGpu;
    void InitializeEnvironmentFallback();
    struct HdrTarget {
        Microsoft::WRL::ComPtr<ID3D12Resource> color;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptors,rtvs;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        UINT width=0,height=0;
        UINT64 allocationBytes=0;
    };
    HdrTarget m_HdrTargets[FrameCount];
    struct FogPassResources {HdrTarget target,volume;Microsoft::WRL::ComPtr<ID3D12Resource> constants,readback;bool pending=false;};
    std::vector<FogPassResources> m_FogPasses[FrameCount];
    FogFrame m_Fog;
    FogDiagnostics m_FogDiagnostics{true};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_FogRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_FogPso,m_FogVolumePso,m_FogCompositePso;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_FogTime;
    UINT m_FogPassCursor=0;
    double m_FogGpuMs=0;
    void EnsureFogTarget(HdrTarget&,UINT,UINT);
    bool EnsureFogPipeline();
    void ApplyFog();
    void CollectFogTiming(UINT);
    void ReleaseFog();
    bool m_HdrActive=false,m_HdrNative=false;
    AntiAliasingMode m_AntiAliasing=AntiAliasingMode::Off;
    float m_SceneClear[4]={.08f,.08f,.12f,1};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_OutputRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_OutputPso;
    void BeginHdrScene();
    void EndHdrScene();
    void ReleaseHdrTargets();
    std::vector<float> ReadBackFloatTexture(ID3D12Resource*,unsigned,unsigned);

    std::vector<Microsoft::WRL::ComPtr<IUnknown>> m_RetiredResources[FrameCount];
    std::vector<UINT> m_RetiredTextureDescriptors[FrameCount];
	static constexpr UINT MaxTextures = 128;
    static constexpr UINT MaterialTablesPerFrame=8192;
    std::map<std::array<std::uint64_t,5>,D3D12_GPU_DESCRIPTOR_HANDLE> m_MaterialTables;
    UINT m_MaterialTableCount=0;
    std::array<std::uint64_t,5> m_LastMaterialTableKey{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_LastMaterialTable{};
    unsigned m_MaterialAvailableMaps=0;
    std::array<RenderTextureHandle,3> m_BoundMaps{};
    std::array<float,4> m_TextureParameters{1,1,0,1};
    bool BindMaterialTextures(bool srgb);
    bool m_TraceMaterialBindings=false;
    std::vector<MaterialBindingRecord> m_MaterialBindingTrace;

	std::unordered_map<std::uint64_t, UploadedMesh> m_UploadedMeshes;
	std::uint64_t m_NextMeshHandle = 1;
	std::unordered_map<std::uint64_t, UploadedTexture> m_UploadedTextures;
	std::vector<UINT> m_FreeTextureDescriptorIndices;
	std::vector<UINT> m_FreeImGuiDescriptorIndices;
	std::uint64_t m_NextTextureHandle = 1;
	std::unordered_map<std::uint64_t, UploadedShader> m_UploadedShaders;
	std::uint64_t m_NextShaderHandle = 1;
	ViewportTarget m_ViewportTarget;
	RenderShaderHandle m_BoundShaderHandle{};
	RenderTextureHandle m_BoundTextureHandle{};
	bool m_EditorUiInitialized = false;

	float m_PendingMVP[16] = {
		1,0,0,0,
		0,1,0,0,
		0,0,1,0,
		0,0,0,1
	};
	LightingConstants m_Lighting{};
    std::array<float,12> m_Normal{1,0,0,0,0,1,0,0,0,0,1,0};
	float m_PendingColor[4] = { 1,1,1,1 };
};
#endif
