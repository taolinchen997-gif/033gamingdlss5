// CPU-only ID3D12Device stub: no real device, GPU, DLL loading or driver calls.
// Method signatures follow Windows SDK 10.0.26100.0 d3d12.h (MSVC ABI).
#pragma once
#include <d3d12.h>
namespace identitytest033 {
struct DeviceView:ID3D12Device {
    IUnknown* owner;unsigned unexpectedCalls=0,privateCalls=0;
    explicit DeviceView(IUnknown* p):owner(p){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{return owner->QueryInterface(id,out);}
    ULONG STDMETHODCALLTYPE AddRef()override{return owner->AddRef();}
    ULONG STDMETHODCALLTYPE Release()override{return owner->Release();}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*)override{++privateCalls;return DXGI_ERROR_NOT_FOUND;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*)override{++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*)override{++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR)override{++unexpectedCalls;return E_NOTIMPL;}
    UINT STDMETHODCALLTYPE GetNodeCount() override {++unexpectedCalls;return {};}
    HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID riid, void **ppCommandQueue) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **ppCommandAllocator) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *pCommandAllocator, ID3D12PipelineState *pInitialState, REFIID riid, void **ppCommandList) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc, REFIID riid, void **ppvHeap) override {++unexpectedCalls;return E_NOTIMPL;}
    UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) override {++unexpectedCalls;return {};}
    HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT nodeMask, const void *pBlobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void **ppvRootSignature) override {++unexpectedCalls;return E_NOTIMPL;}
    void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {++unexpectedCalls;}
    void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource *pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {++unexpectedCalls;}
    void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource *pResource, ID3D12Resource *pCounterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {++unexpectedCalls;}
    void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource *pResource, const D3D12_RENDER_TARGET_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {++unexpectedCalls;}
    void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource *pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {++unexpectedCalls;}
    void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {++unexpectedCalls;}
    void STDMETHODCALLTYPE CopyDescriptors(UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE *pDestDescriptorRangeStarts, const UINT *pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE *pSrcDescriptorRangeStarts, const UINT *pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override {++unexpectedCalls;}
    void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override {++unexpectedCalls;}
    D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC *pResourceDescs) override {++unexpectedCalls;return {};}
    D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(UINT nodeMask, D3D12_HEAP_TYPE heapType) override {++unexpectedCalls;return {};}
    HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riidResource, void **ppvResource) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC *pDesc, REFIID riid, void **ppvHeap) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riid, void **ppvResource) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riid, void **ppvResource) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateSharedHandle(ID3D12DeviceChild *pObject, const SECURITY_ATTRIBUTES *pAttributes, DWORD Access, LPCWSTR Name, HANDLE *pHandle) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OpenSharedHandle(HANDLE NTHandle, REFIID riid, void **ppvObj) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR Name, DWORD Access, HANDLE *pNTHandle) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE MakeResident(UINT NumObjects, ID3D12Pageable *const *ppObjects) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Evict(UINT NumObjects, ID3D12Pageable *const *ppObjects) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateFence(UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override {++unexpectedCalls;return E_NOTIMPL;}
    void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC *pResourceDesc, UINT FirstSubresource, UINT NumSubresources, UINT64 BaseOffset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts, UINT *pNumRows, UINT64 *pRowSizeInBytes, UINT64 *pTotalBytes) override {++unexpectedCalls;}
    HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC *pDesc, REFIID riid, void **ppvHeap) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL Enable) override {++unexpectedCalls;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC *pDesc, ID3D12RootSignature *pRootSignature, REFIID riid, void **ppvCommandSignature) override {++unexpectedCalls;return E_NOTIMPL;}
    void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource *pTiledResource, UINT *pNumTilesForEntireResource, D3D12_PACKED_MIP_INFO *pPackedMipDesc, D3D12_TILE_SHAPE *pStandardTileShapeForNonPackedMips, UINT *pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D12_SUBRESOURCE_TILING *pSubresourceTilingsForNonPackedMips) override {++unexpectedCalls;}
    LUID STDMETHODCALLTYPE GetAdapterLuid() override {++unexpectedCalls;return {};}
};
}
