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

struct Vertex
{
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT4 color;

    static const D3D12_INPUT_ELEMENT_DESC inputLayout[];
    static const size_t inputLayoutSize;
};

const D3D12_INPUT_ELEMENT_DESC Vertex::inputLayout[] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

const size_t Vertex::inputLayoutSize = sizeof(Vertex::inputLayout) / sizeof(Vertex::inputLayout[0]);

struct ConstantBuffer
{
    DirectX::XMFLOAT4X4 wvpMatrix;

    static const size_t GPUAlignedSize;
};

const size_t ConstantBuffer::GPUAlignedSize = (sizeof(ConstantBuffer) + 255) & ~255;

using Microsoft::WRL::ComPtr;

constexpr int framebufferCount_ = 3;

// general device/present variables
ComPtr<IDXGIFactory4> dxgiFactory_;
ComPtr<ID3D12Device> device_;
ComPtr<IDXGISwapChain3> swapChain_;
D3D12_VIEWPORT viewport_;
D3D12_RECT scissors_;
ComPtr<ID3D12Debug> debugController_;

// command resources
ComPtr<ID3D12CommandQueue> commandQueue_;
ComPtr<ID3D12CommandAllocator> commandAllocators_[framebufferCount_];
ComPtr<ID3D12GraphicsCommandList> commandList_;
ComPtr<ID3D12Fence> fences_[framebufferCount_];
uint64_t fenceValues_[framebufferCount_];
HANDLE fenceEvent_;

// graphics pipeline state config
ComPtr<ID3D12PipelineState> pipelineState_;
ComPtr<ID3D12RootSignature> rootSignature_;

// vertex/index buffer resources
ComPtr<ID3D12Resource> vertexBuffer_;
ComPtr<ID3D12Resource> indexBuffer_;
D3D12_VERTEX_BUFFER_VIEW vertexBufferView_;
D3D12_INDEX_BUFFER_VIEW indexBufferView_;

// Render target variables
ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
ComPtr<ID3D12Resource> renderTargets_[framebufferCount_];
uint32_t rtvHandleSize_;

// Depth stencil variables
ComPtr<ID3D12Resource> depthStencilBuffer_[framebufferCount_];
ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;
uint32_t dsHandleSize_;

// Constant buffer variables;
ComPtr<ID3D12Resource> constBuffers_[framebufferCount_];
ConstantBuffer cbuf_;

// matrices
DirectX::XMFLOAT4X4 cameraProjMat;
DirectX::XMFLOAT4X4 cameraViewMat;

DirectX::XMFLOAT4 cameraPos;
DirectX::XMFLOAT4 cameraUp;
DirectX::XMFLOAT4 cameraTarget;

DirectX::XMFLOAT4X4 cube1WorldMat;
DirectX::XMFLOAT4X4 cube1RotMat;
DirectX::XMFLOAT4 cube1Pos;

DirectX::XMFLOAT4X4 cube2WorldMat;
DirectX::XMFLOAT4X4 cube2RotMat;
DirectX::XMFLOAT4 cube2PosOffset;

int numCubeIndices_;
int frameIdx_;

OnErrorCallback errorCallback_;

// useful variables for resource path
static const std::string projectRoot_(PROJECT_SRC_DIR);
static const std::wstring wprojectRoot_(projectRoot_.begin(), projectRoot_.end());

// static (private) functions
static void updatePipeline();
static void waitForPreviousFrame(bool isShutdown = false);

static bool createDxgiFactory();
static bool createDevice();
static bool createSwapChain(HWND window, int width, int height, bool fullscreen);
static bool createCommandQueue();
static bool createRTVDescHeap();
static bool createDSVDescHeap(int width, int height);
static bool createCommandResources();
static bool createRootSignature();
static bool createMainDescHeap();
static bool compileShader(const std::wstring& name, const char* shaderType, ID3DBlob** outShaderBytecode);
static bool createPSO(ID3DBlob* vertexShader, ID3DBlob* pixelShader);
static bool setupGeometry();

bool initd3d(HWND window, int width, int height, bool fullscreen, OnErrorCallback errorCallback)
{
    using namespace DirectX;

    errorCallback_ = errorCallback;

    if (!createDxgiFactory())
        return false;

    if (!createDevice())
        return false;

    if (!createCommandQueue())
        return false;

    if (!createSwapChain(window, width, height, fullscreen))
        return false;

    if (!createRTVDescHeap())
        return false;

    if (!createDSVDescHeap(width, height))
        return false;

    if (!createMainDescHeap())
        return false;

    if (!createCommandResources())
        return false;

    if (!createRootSignature())
        return false;

    ComPtr<ID3DBlob> vertexShader;
    if (!compileShader(std::wstring(L"vertex.hlsl"), "vs_5_0", vertexShader.GetAddressOf()))
        return false;

    ComPtr<ID3DBlob> pixelShader;
    if (!compileShader(std::wstring(L"pixel.hlsl"), "ps_5_0", pixelShader.GetAddressOf()))
        return false;

    if (!createPSO(vertexShader.Get(), pixelShader.Get()))
        return false;

    if (!setupGeometry())
        return false;

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

    // build projection and view matrix
    XMMATRIX tmpMat = XMMatrixPerspectiveFovLH(45.0f*(3.14f / 180.0f), (float)width / (float)height, 0.1f, 1000.0f);
    XMStoreFloat4x4(&cameraProjMat, tmpMat);

    // set starting camera state
    cameraPos = XMFLOAT4(0.0f, 2.0f, -4.0f, 0.0f);
    cameraTarget = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
    cameraUp = XMFLOAT4(0.0f, 1.0f, 0.0f, 0.0f);

    // build view matrix
    XMVECTOR cPos = XMLoadFloat4(&cameraPos);
    XMVECTOR cTarg = XMLoadFloat4(&cameraTarget);
    XMVECTOR cUp = XMLoadFloat4(&cameraUp);
    tmpMat = XMMatrixLookAtLH(cPos, cTarg, cUp);
    XMStoreFloat4x4(&cameraViewMat, tmpMat);

    // set starting cubes position
    // first cube
    cube1Pos = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f); // set cube 1's position
    XMVECTOR posVec = XMLoadFloat4(&cube1Pos); // create xmvector for cube1's position

    tmpMat = XMMatrixTranslationFromVector(posVec); // create translation matrix from cube1's position vector
    XMStoreFloat4x4(&cube1RotMat, XMMatrixIdentity()); // initialize cube1's rotation matrix to identity matrix
    XMStoreFloat4x4(&cube1WorldMat, tmpMat); // store cube1's world matrix

                                             // second cube
    cube2PosOffset = XMFLOAT4(1.5f, 0.0f, 0.0f, 0.0f);
    posVec = XMLoadFloat4(&cube2PosOffset) + XMLoadFloat4(&cube1Pos); // create xmvector for cube2's position
                                                                                // we are rotating around cube1 here, so add cube2's position to cube1

    tmpMat = XMMatrixTranslationFromVector(posVec); // create translation matrix from cube2's position offset vector
    XMStoreFloat4x4(&cube2RotMat, XMMatrixIdentity()); // initialize cube2's rotation matrix to identity matrix
    XMStoreFloat4x4(&cube2WorldMat, tmpMat); // store cube2's world matrix

    return true;
}

void update()
{
    using DirectX::XMMATRIX;
    using DirectX::XMMatrixRotationX;
    using DirectX::XMMatrixRotationY;
    using DirectX::XMMatrixRotationZ;
    using DirectX::XMLoadFloat4x4;
    using DirectX::XMLoadFloat4;
    using DirectX::XMMatrixTranslationFromVector;
    using DirectX::XMMatrixScaling;

    // map and copy constant buffer
    CD3DX12_RANGE readRange{ 0, 0 };
    CD3DX12_RANGE writeRange{ 0, 2 * sizeof(ConstantBuffer::GPUAlignedSize) };
    uint8_t* cbufGpuAddr;

    constBuffers_[frameIdx_]->Map(0, &readRange, reinterpret_cast<void**>(&cbufGpuAddr));

    // create rotation matrices
    XMMATRIX rotXMat = XMMatrixRotationX(0.0001f);
    XMMATRIX rotYMat = XMMatrixRotationY(0.0002f);
    XMMATRIX rotZMat = XMMatrixRotationZ(0.0003f);

    // add rotation to cube1's rotation matrix and store it
    XMMATRIX rotMat = XMLoadFloat4x4(&cube1RotMat) * rotXMat * rotYMat * rotZMat;
    XMStoreFloat4x4(&cube1RotMat, rotMat);

    // create translation matrix for cube 1 from cube 1's position vector
    XMMATRIX translationMat = XMMatrixTranslationFromVector(XMLoadFloat4(&cube1Pos));

    // create cube1's world matrix by first rotating the cube, then positioning the rotated cube
    XMMATRIX worldMat = rotMat * translationMat;

    // store cube1's world matrix
    XMStoreFloat4x4(&cube1WorldMat, worldMat);

    // update constant buffer for cube1
    // create the wvp matrix and store in constant buffer
    XMMATRIX viewMat = XMLoadFloat4x4(&cameraViewMat); // load view matrix
    XMMATRIX projMat = XMLoadFloat4x4(&cameraProjMat); // load projection matrix
    XMMATRIX wvpMat = XMLoadFloat4x4(&cube1WorldMat) * viewMat * projMat; // create wvp matrix
    XMMATRIX transposed = XMMatrixTranspose(wvpMat); // must transpose wvp matrix for the gpu
    XMStoreFloat4x4(&cbuf_.wvpMatrix, transposed); // store transposed wvp matrix in constant buffer

    // copy our ConstantBuffer instance to the mapped constant buffer resource
    memcpy(cbufGpuAddr, &cbuf_, sizeof(cbuf_));

    // now do cube2's world matrix
    // create rotation matrices for cube2
    rotXMat = XMMatrixRotationX(0.0003f);
    rotYMat = XMMatrixRotationY(0.0002f);
    rotZMat = XMMatrixRotationZ(0.0001f);

    // add rotation to cube2's rotation matrix and store it
    rotMat = rotZMat * (XMLoadFloat4x4(&cube2RotMat) * (rotXMat * rotYMat));
    XMStoreFloat4x4(&cube2RotMat, rotMat);

    // create translation matrix for cube 2 to offset it from cube 1 (its position relative to cube1
    XMMATRIX translationOffsetMat = XMMatrixTranslationFromVector(XMLoadFloat4(&cube2PosOffset));

    // we want cube 2 to be half the size of cube 1, so we scale it by .5 in all dimensions
    XMMATRIX scaleMat = XMMatrixScaling(0.5f, 0.5f, 0.5f);

    // reuse worldMat. 
    // first we scale cube2. scaling happens relative to point 0,0,0, so you will almost always want to scale first
    // then we translate it. 
    // then we rotate it. rotation always rotates around point 0,0,0
    // finally we move it to cube 1's position, which will cause it to rotate around cube 1
    worldMat = scaleMat * translationOffsetMat * rotMat * translationMat;

    wvpMat = XMLoadFloat4x4(&cube2WorldMat) * viewMat * projMat; // create wvp matrix
    transposed = XMMatrixTranspose(wvpMat); // must transpose wvp matrix for the gpu
    XMStoreFloat4x4(&cbuf_.wvpMatrix, transposed); // store transposed wvp matrix in constant buffer

    // copy our ConstantBuffer instance to the mapped constant buffer resource
    memcpy(cbufGpuAddr + ConstantBuffer::GPUAlignedSize, &cbuf_, sizeof(cbuf_));

    // store cube2's world matrix
    XMStoreFloat4x4(&cube2WorldMat, worldMat);

    constBuffers_[frameIdx_]->Unmap(0, &writeRange);
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

static void updatePipeline()
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
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle{ dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), frameIdx_, dsHandleSize_ };

    commandList_->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissors_);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList_->IASetIndexBuffer(&indexBufferView_);

    commandList_->SetGraphicsRootSignature(rootSignature_.Get());
    commandList_->SetGraphicsRootConstantBufferView(0, constBuffers_[frameIdx_]->GetGPUVirtualAddress());

    commandList_->DrawIndexedInstanced(numCubeIndices_, 1, 0, 0, 0);

    commandList_->SetGraphicsRootConstantBufferView(0, constBuffers_[frameIdx_]->GetGPUVirtualAddress() + ConstantBuffer::GPUAlignedSize);
    commandList_->DrawIndexedInstanced(numCubeIndices_, 1, 0, 0, 0);

    commandList_->ResourceBarrier(1, &renderTargetToPresent);

    result = commandList_->Close();
    if (FAILED(result))
        errorCallback_();
}

static void waitForPreviousFrame(bool isShutdown)
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


static bool createDxgiFactory()
{
    HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory_.GetAddressOf()));
    if (FAILED(result))
        return false;

    return true;
}

static bool createDevice()
{
    ComPtr<IDXGIAdapter1> adapter;
    HRESULT result;
    int adapterIdx = 0;
    bool adapterFound = false;

    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController_.GetAddressOf())))) {
        debugController_->EnableDebugLayer();
    }

    while (dxgiFactory_->EnumAdapters1(adapterIdx, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND) {
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

    return true;
}

static bool createSwapChain(HWND window, int width, int height, bool fullscreen)
{
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
    dxgiFactory_->CreateSwapChain(commandQueue_.Get(), &swapChainDesc, tmpSwapChain.GetAddressOf());

    HRESULT result = tmpSwapChain.As(&swapChain_);
    if (FAILED(result))
        return false;

    frameIdx_ = swapChain_->GetCurrentBackBufferIndex();

    return true;
}

static bool createCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
    HRESULT result = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue_.GetAddressOf()));
    if (FAILED(result))
        return false;

    return true;
}

static bool createRTVDescHeap()
{
    // create render target view (rtv) descriptor memory. This is basically a
    // memory/buffer, that will hold handles to the backbuffers to which
    // the GPU will write
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = framebufferCount_;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT result = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvDescriptorHeap_.GetAddressOf()));
    if (FAILED(result))
        return false;

    rtvHandleSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // handle to the beginning of memory buffer (or descriptor heap in dx12 terms)
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle{ rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart() };

    // for each buffer in swap chain create render target view (a handle to
    // backbuffer) in the buffer location described by rtvHandle
    for (int i = 0; i < framebufferCount_; ++i) {
        result = swapChain_->GetBuffer(i, IID_PPV_ARGS(renderTargets_[i].GetAddressOf()));
        if (FAILED(result))
            return false;

        device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr, rtvHandle);

        rtvHandle.Offset(1, rtvHandleSize_);
    }

    return true;
}

static bool createDSVDescHeap(int width, int height)
{
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = framebufferCount_;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT result = device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(dsvDescriptorHeap_.GetAddressOf()));
    if (FAILED(result))
        return false;
    dsvDescriptorHeap_->SetName(L"DepthStencilDescriptorHeap");

    dsHandleSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
    depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

    D3D12_CLEAR_VALUE depthClearValue = {};
    depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.DepthStencil.Stencil = 0;

    const auto dsvHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto dsvTexDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle { dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart() };

    for (int i = 0; i < framebufferCount_; ++i) {
        result = device_->CreateCommittedResource(
            &dsvHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &dsvTexDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthClearValue,
            IID_PPV_ARGS(depthStencilBuffer_[i].GetAddressOf()));
        if (FAILED(result))
            return false;

        device_->CreateDepthStencilView(depthStencilBuffer_[i].Get(), &depthStencilDesc, dsvHandle);
        dsvHandle.Offset(1, dsHandleSize_);
    }

    return true;
}

static bool createMainDescHeap()
{
    HRESULT result;

    const auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto bufferProps = CD3DX12_RESOURCE_DESC::Buffer(1024 * 64);

    memset(&cbuf_, 0, sizeof(cbuf_));

    for (int i = 0; i < framebufferCount_; ++i) {
        result = device_->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferProps,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(constBuffers_[i].GetAddressOf()));

        if (FAILED(result))
            return false;
        constBuffers_[i]->SetName(L"ConstantBufferHeap");

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = constBuffers_[i]->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = (sizeof(ConstantBuffer) + 255) & ~255;

        CD3DX12_RANGE readRange{ 0, 0 };
        CD3DX12_RANGE writeRange{ 0, 2 * sizeof(ConstantBuffer::GPUAlignedSize) };
        uint8_t* cbufGpuAddr;

        constBuffers_[i]->Map(0, &readRange, reinterpret_cast<void**>(&cbufGpuAddr));

        memcpy(cbufGpuAddr, &cbuf_, sizeof(cbuf_));
        memcpy(cbufGpuAddr + ConstantBuffer::GPUAlignedSize, &cbuf_, sizeof(cbuf_));

        constBuffers_[i]->Unmap(0, &readRange);
    }

    return true;
}

static bool createCommandResources()
{
    HRESULT result;

    // create command allocators - this is kind of a interface to the GPU
    // memory which holds the commands to be executed by the GPU.
    for (int i = 0; i < framebufferCount_; ++i) {
        result = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocators_[i].GetAddressOf()));
        if (FAILED(result))
            return false;
    }

    // CPU buffer which records GPU commands. The commands are then submitted
    // to GPU memory through the use of commandAllocators_. There only needs to
    // be a single command list (for single threaded apps), since the CPU memory
    // can be everwritten as soon as we exequte commandList on command queue.
    result = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators_[0].Get(), nullptr, IID_PPV_ARGS(commandList_.GetAddressOf()));
    if (FAILED(result))
        return false;

    // Create a fence that will be used by each commandAllocator to check if
    // all the commands in allocator have already been finished or not yet
    for (int i = 0; i < framebufferCount_; ++i) {
        result = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fences_[i].GetAddressOf()));
        if (FAILED(result))
            return false;
        fenceValues_[i] = 0;
    }

    // CPU fence used to wait for command completion on GPU
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_)
        return false;

    return true;
}

static bool createRootSignature()
{
    D3D12_ROOT_DESCRIPTOR rootCBVDesc;
    rootCBVDesc.RegisterSpace = 0;
    rootCBVDesc.ShaderRegister = 0;

    D3D12_ROOT_PARAMETER rootParams[1] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor = rootCBVDesc;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init(
        sizeof(rootParams) / sizeof(rootParams[0]),
        rootParams,
        0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS);

    ComPtr<ID3DBlob> rootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT result = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSig, errorBlob.GetAddressOf());
    if (FAILED(result)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        return false;
    }

    result = device_->CreateRootSignature(0, rootSig->GetBufferPointer(), rootSig->GetBufferSize(), IID_PPV_ARGS(rootSignature_.GetAddressOf()));
    if (FAILED(result))
        return false;

    return true;
}

static bool compileShader(const std::wstring& name, const char* shaderType, ID3DBlob** outShaderBytecode)
{
    ComPtr<ID3DBlob> errorBlob;

    HRESULT result = D3DCompileFromFile(
        (wprojectRoot_ + std::wstring(L"/shaders/") + name).c_str(),
        nullptr,
        nullptr,
        "main",
        shaderType,
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
        0,
        outShaderBytecode,
        errorBlob.GetAddressOf());

    if (FAILED(result)) {
        if (errorBlob)
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        return false;
    }

    return true;
}

static bool createPSO(ID3DBlob* vertexShader, ID3DBlob* pixelShader)
{
    D3D12_SHADER_BYTECODE vertexBytecode;
    vertexBytecode.pShaderBytecode = vertexShader->GetBufferPointer();
    vertexBytecode.BytecodeLength = vertexShader->GetBufferSize();

    D3D12_SHADER_BYTECODE pixelBytecode;
    pixelBytecode.pShaderBytecode = pixelShader->GetBufferPointer();
    pixelBytecode.BytecodeLength = pixelShader->GetBufferSize();

    // would be nice to have just one of these for all assets
    D3D12_INPUT_LAYOUT_DESC layoutDesc = {};
    layoutDesc.NumElements = Vertex::inputLayoutSize;
    layoutDesc.pInputElementDescs = Vertex::inputLayout;

    // There should be only 1 sample desc structure, the same for pso and rt
    DXGI_SAMPLE_DESC sampleDesc = {};
    sampleDesc.Count = 1;

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
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.NumRenderTargets = 1;

    HRESULT result = device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState_.GetAddressOf()));
    if (FAILED(result))
        return false;

    return true;
}

static bool setupGeometry()
{
    Vertex vertices[] = {
        // front face
        { { -0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 1.0f, 1.0f } },
        { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { 0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f, 1.0f } },

        // right side face
        { { 0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 1.0f, 1.0f } },
        { { 0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { 0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f, 1.0f } },

        // left side face
        { { -0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 1.0f, 1.0f } },
        { { -0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f, 1.0f } },

        // back face
        { { 0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 1.0f, 1.0f } },
        { { 0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -0.5f,  0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f, 1.0f } },

        // top face
        { { -0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 1.0f, 1.0f } },
        { { 0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -0.5f,  0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f, 1.0f } },

        // bottom face
        { { 0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 1.0f, 1.0f } },
        { { 0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
    };
    uint32_t vertBufSize = sizeof(vertices);

    uint32_t indices[] = {
        // ffront face
        0, 1, 2, // first triangle
        0, 3, 1, // second triangle

        // left face
        4, 5, 6, // first triangle
        4, 7, 5, // second triangle

        // right face
        8, 9, 10, // first triangle
        8, 11, 9, // second triangle

        // back face
        12, 13, 14, // first triangle
        12, 15, 13, // second triangle

        // top face
        16, 17, 18, // first triangle
        16, 19, 17, // second triangle

        // bottom face
        20, 21, 22, // first triangle
        20, 23, 21, // second triangle
    };
    uint32_t indexBufSize = sizeof(indices);
    numCubeIndices_ = sizeof(indices) / sizeof(indices[0]);

    const auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    const auto vertBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertBufSize);
    const auto indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufSize);

    HRESULT result = device_->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &vertBufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(vertexBuffer_.GetAddressOf()));
    if (FAILED(result))
        return false;
    vertexBuffer_->SetName(L"VertexBufferResource");

    ComPtr<ID3D12Resource> vertBufferUploadRes;
    result = device_->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &vertBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(vertBufferUploadRes.GetAddressOf()));
    if (FAILED(result))
        return false;
    vertBufferUploadRes->SetName(L"VertexBufferUploadResource");

    result = device_->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &indexBufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(indexBuffer_.GetAddressOf()));
    if (FAILED(result))
        return false;
    indexBuffer_->SetName(L"IndexBufferResource");

    ComPtr<ID3D12Resource> indexBufferUploadRes;
    result = device_->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &vertBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(indexBufferUploadRes.GetAddressOf()));
    if (FAILED(result))
        return false;
    indexBufferUploadRes->SetName(L"IndexBufferUploadResource");

    D3D12_SUBRESOURCE_DATA vertexData = {};
    vertexData.pData = reinterpret_cast<BYTE*>(vertices);
    vertexData.RowPitch = vertBufSize;
    vertexData.SlicePitch = vertBufSize;

    D3D12_SUBRESOURCE_DATA indexData = {};
    indexData.pData = reinterpret_cast<BYTE*>(indices);
    indexData.RowPitch = indexBufSize;
    indexData.SlicePitch = indexBufSize;

    const auto vertexTransition = CD3DX12_RESOURCE_BARRIER::Transition(vertexBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    const auto indexTransition = CD3DX12_RESOURCE_BARRIER::Transition(indexBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    UpdateSubresources(commandList_.Get(), vertexBuffer_.Get(), vertBufferUploadRes.Get(), 0, 0, 1, &vertexData);
    commandList_->ResourceBarrier(1, &vertexTransition);
    UpdateSubresources(commandList_.Get(), indexBuffer_.Get(), indexBufferUploadRes.Get(), 0, 0, 1, &indexData);
    commandList_->ResourceBarrier(1, &indexTransition);
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

    indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = indexBufSize;
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

    return true;
}
