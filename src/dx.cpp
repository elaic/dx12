#include "dx.h"

#include "config.h"

#pragma warning(push)
#pragma warning(disable : 4324)
#include "d3dx12.h"
#pragma warning(pop)

#include "DirectXMath.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>


using Microsoft::WRL::ComPtr;

constexpr int framebufferCount_ = 3;

ComPtr<ID3D12Device> device_;
ComPtr<IDXGISwapChain3> swapChain_;
ComPtr<ID3D12CommandQueue> commandQueue_;
ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
ComPtr<ID3D12Resource> renderTargets_[framebufferCount_];
ComPtr<ID3D12CommandAllocator> commandAllocators_[framebufferCount_];
ComPtr<ID3D12GraphicsCommandList> commandList_;
ComPtr<ID3D12Fence> fences_[framebufferCount_];
uint64_t fenceValues_[framebufferCount_];
HANDLE fenceEvent_;

ComPtr<ID3D12PipelineState> pipelineState_;
ComPtr<ID3D12RootSignature> rootSignature_;
D3D12_VIEWPORT viewport_;
D3D12_RECT scissors_;
ComPtr<ID3D12Resource> vertexBuffer_;
D3D12_VERTEX_BUFFER_VIEW vertexBufferView_;

int frameIdx_;
unsigned int rtvHandleSize_;

const std::string projectRoot_(PROJECT_SRC_DIR);
const std::wstring wprojectRoot_(projectRoot_.begin(), projectRoot_.end());

static void updatePipeline();
static void waitForPreviousFrame(bool isShutdown = false);

OnErrorCallback errorCallback_;

struct Vertex
{
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT4 color;
};

bool initd3d(HWND window, int width, int height, bool fullscreen, OnErrorCallback errorCallback)
{
    HRESULT result;

    errorCallback_ = errorCallback;

    ComPtr<IDXGIFactory4> dxgiFactory;
    result = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
    if (FAILED(result)) {
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter;
    int adapterIdx = 0;
    bool adapterFound = false;

    while (dxgiFactory->EnumAdapters1(adapterIdx, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapterIdx++;
            continue;
        }

        result = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), NULL);
        if (SUCCEEDED(result)) {
            adapterFound = true;
            break;
        }

        adapterIdx++;
    }

    if (!adapterFound)
        return false;

    result = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device_.GetAddressOf()));
    if (FAILED(result))
        return false;

    D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
    result = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue_.GetAddressOf()));
    if (FAILED(result))
        return false;

    DXGI_MODE_DESC backBufferDesc = {};
    backBufferDesc.Width = width;
    backBufferDesc.Height = height;
    backBufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    DXGI_SAMPLE_DESC sampleDesc = {};
    sampleDesc.Count = 1;

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = framebufferCount_;
    swapChainDesc.BufferDesc = backBufferDesc;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.OutputWindow = window;
    swapChainDesc.SampleDesc = sampleDesc;
    swapChainDesc.Windowed = !fullscreen;

    ComPtr<IDXGISwapChain> tmpSwapChain;
    dxgiFactory->CreateSwapChain(commandQueue_.Get(), &swapChainDesc, tmpSwapChain.GetAddressOf());

    result = tmpSwapChain.As(&swapChain_);
    if (FAILED(result))
        return false;

    frameIdx_ = swapChain_->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = framebufferCount_;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    result = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvDescriptorHeap_.GetAddressOf()));
    if (FAILED(result))
        return false;

    rtvHandleSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle { rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart() };

    for (int i = 0; i < framebufferCount_; ++i) {
        result = swapChain_->GetBuffer(i, IID_PPV_ARGS(renderTargets_[i].GetAddressOf()));
        if (FAILED(result))
            return false;

        device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr, rtvHandle);

        rtvHandle.Offset(1, rtvHandleSize_);
    }

    for (int i = 0; i < framebufferCount_; ++i) {
        result = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocators_[i].GetAddressOf()));
        if (FAILED(result))
            return false;
    }

    result = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators_[0].Get(), nullptr, IID_PPV_ARGS(commandList_.GetAddressOf()));
    if (FAILED(result))
        return false;

    for (int i = 0; i < framebufferCount_; ++i) {
        result = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fences_[i].GetAddressOf()));
        if (FAILED(result))
            return false;
        fenceValues_[i] = 0;
    }

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_)
        return false;

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> rootSig;
    ComPtr<ID3DBlob> errorBlob;
    result = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSig, errorBlob.GetAddressOf());
    if (FAILED(result)) {
        if (errorBlob) {
            printf("%s\n", (char*)errorBlob->GetBufferPointer());
        }
        return false;
    }

    result = device_->CreateRootSignature(0, rootSig->GetBufferPointer(), rootSig->GetBufferSize(), IID_PPV_ARGS(rootSignature_.GetAddressOf()));
    if (FAILED(result))
        return false;

    ComPtr<ID3DBlob> vertexShader;
    result = D3DCompileFromFile(
        (wprojectRoot_ + std::wstring(L"/shaders/vertex.hlsl")).c_str(),
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
        0,
        &vertexShader,
        errorBlob.ReleaseAndGetAddressOf());
    if (FAILED(result)) {
        if (errorBlob)
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        return false;
    }

    D3D12_SHADER_BYTECODE vertexBytecode;
    vertexBytecode.pShaderBytecode = vertexShader->GetBufferPointer();
    vertexBytecode.BytecodeLength = vertexShader->GetBufferSize();

    ComPtr<ID3DBlob> pixelShader;
    result = D3DCompileFromFile(
        (wprojectRoot_ + std::wstring(L"/shaders/pixel.hlsl")).c_str(),
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
        0,
        pixelShader.GetAddressOf(),
        errorBlob.ReleaseAndGetAddressOf());
    if (FAILED(result)) {
        if (errorBlob)
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        return false;
    }

    D3D12_SHADER_BYTECODE pixelBytecode;
    pixelBytecode.pShaderBytecode = pixelShader->GetBufferPointer();
    pixelBytecode.BytecodeLength = pixelShader->GetBufferSize();

    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_INPUT_LAYOUT_DESC layoutDesc = {};
    layoutDesc.NumElements = sizeof(inputLayout) / sizeof(inputLayout[0]);
    layoutDesc.pInputElementDescs = inputLayout;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = layoutDesc;
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.VS = vertexBytecode;
    psoDesc.PS = pixelBytecode;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc = sampleDesc;
    psoDesc.SampleMask = 0xffffffff;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.NumRenderTargets = 1;

    result = device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState_.GetAddressOf()));
    if (FAILED(result))
        return false;

    Vertex vertices[] = {
        { { 0.0f, 0.5f, 0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 0.5f, -0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.5f, -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
    };

    UINT vertBufSize = sizeof(vertices);

    const auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto vertBudderDesc = CD3DX12_RESOURCE_DESC::Buffer(vertBufSize);

    result = device_->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &vertBudderDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(vertexBuffer_.GetAddressOf()));
    if (FAILED(result))
        return false;
    vertexBuffer_->SetName(L"VertexBufferResource");

    const auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    ComPtr<ID3D12Resource> vertBufferUploadRes;
    result = device_->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &vertBudderDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(vertBufferUploadRes.GetAddressOf()));
    if (FAILED(result))
        return false;
    vertBufferUploadRes->SetName(L"VertexBufferUploadResource");

    D3D12_SUBRESOURCE_DATA vertexData = {};
    vertexData.pData = reinterpret_cast<BYTE*>(vertices);
    vertexData.RowPitch = vertBufSize;
    vertexData.SlicePitch = vertBufSize;

    const auto transition = CD3DX12_RESOURCE_BARRIER::Transition(vertexBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    UpdateSubresources(commandList_.Get(), vertexBuffer_.Get(), vertBufferUploadRes.Get(), 0, 0, 1, &vertexData);
    commandList_->ResourceBarrier(1, &transition);
    commandList_->Close();
    ID3D12CommandList* cmdLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, cmdLists);

    result = commandQueue_->Signal(fences_[frameIdx_].Get(), fenceValues_[frameIdx_]);
    if (FAILED(result))
        return false;
    waitForPreviousFrame();
    // TODO: super ugly hack!!! Rework wait for previous frame!
    fenceValues_[frameIdx_]--;

    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = vertBufSize;
    vertexBufferView_.StrideInBytes = sizeof(Vertex);

    viewport_.TopLeftX = 0;
    viewport_.TopLeftY = 0;
    viewport_.Width = (float)width;
    viewport_.Height = (float)height;
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissors_.top = 0;
    scissors_.left = 0;
    scissors_.bottom = height;
    scissors_.right = width;

    return true;
}

void update()
{

}

void render()
{
    HRESULT result;

    updatePipeline();

    ID3D12CommandList* commandLists[] = { commandList_.Get() };

    commandQueue_->ExecuteCommandLists(1, commandLists);

    result = commandQueue_->Signal(fences_[frameIdx_].Get(), fenceValues_[frameIdx_]);
    if (FAILED(result))
        errorCallback_();

    result = swapChain_->Present(0, 0);
    if (FAILED(result))
        errorCallback_();
}

void cleanupd3d()
{
    for (int i = 0; i < framebufferCount_; ++i) {
        frameIdx_ = i;
        waitForPreviousFrame(true);
    }

    BOOL fullscreen = false;
    HRESULT result = swapChain_->GetFullscreenState(&fullscreen, NULL);
    if (FAILED(result))
        errorCallback_();

    if (fullscreen)
        swapChain_->SetFullscreenState(false, NULL);

    CloseHandle(fenceEvent_);
}

void updatePipeline()
{
    HRESULT result;

    waitForPreviousFrame();

    result = commandAllocators_[frameIdx_]->Reset();
    if (FAILED(result))
        errorCallback_();

    result = commandList_->Reset(commandAllocators_[frameIdx_].Get(), pipelineState_.Get());
    if (FAILED(result))
        errorCallback_();

    const auto presentToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets_[frameIdx_].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto renderTargetToPresent = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets_[frameIdx_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    commandList_->ResourceBarrier(1, &presentToRenderTarget);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle{ rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), frameIdx_, rtvHandleSize_ };

    commandList_->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    commandList_->SetGraphicsRootSignature(rootSignature_.Get());
    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissors_);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList_->DrawInstanced(3, 1, 0, 0);

    commandList_->ResourceBarrier(1, &renderTargetToPresent);

    result = commandList_->Close();
    if (FAILED(result))
        errorCallback_();
}

void waitForPreviousFrame(bool isShutdown)
{
    HRESULT result;

    if (!isShutdown)
        frameIdx_ = swapChain_->GetCurrentBackBufferIndex();

    if (fences_[frameIdx_]->GetCompletedValue() < fenceValues_[frameIdx_]) {
        result = fences_[frameIdx_]->SetEventOnCompletion(fenceValues_[frameIdx_], fenceEvent_);
        if (FAILED(result))
            errorCallback_();

        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    fenceValues_[frameIdx_]++;
}
