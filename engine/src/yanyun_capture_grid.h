// Reduce only the CPU transfer: same detector sample grid and source precision.
// All resources and descriptors are retained by the existing NR frame lease.
#pragma once
namespace yanyundual {
struct CapturePipeline {
 ComPtr<ID3D12Device> device;ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> state;
 void Prepare(ID3D12Device* dev){
  if(device){if(!identity033::Equal(device.Get(),dev))throw std::runtime_error("capture device changed");return;}
  D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=1;
  ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ranges[1].NumDescriptors=1;
  D3D12_ROOT_PARAMETER p[3]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[0].Constants.Num32BitValues=4;
  for(unsigned i=0;i<2;++i){p[i+1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[i+1].DescriptorTable={1,&ranges[i]};}
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=p;
  ComPtr<ID3DBlob> blob,error;ComPtr<ID3D12RootSignature> nextRoot;ComPtr<ID3D12PipelineState> nextState;
  Require(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"capture root serialization");
  Require(dev->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&nextRoot)),"capture root creation");
  blob.Reset();error.Reset();Require(D3DCompile(CaptureShader,sizeof(CaptureShader)-1,"033_capture_grid",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&error),"capture shader compile");
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=nextRoot.Get();pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
  Require(dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&nextState)),"capture pipeline creation");
  root=std::move(nextRoot);state=std::move(nextState);device=dev;
 }
};
inline CapturePipeline capturePipeline;
}
