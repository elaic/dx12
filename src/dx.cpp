#include "dx.h"

#pragma warning(push)
#pragma warning(disable : 4324)
#include "d3dx12.h"
#pragma warning(pop)

#include <cstdint>

#include <d3d12.h>
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

int frameIdx_;
unsigned int rtvHandleSize_;

static void updatePipeline();
static void waitForPreviousFrame(bool isShutdown = false);

OnErrorCallback errorCallback_;

bool initd3d(HWND window, int width, int height, bool fullscreen, OnErrorCallback errorCallback)
{
    HRESULT result;

    errorCallback_ = errorCallback;

    IDXGIFactory4* dxgiFactory;
    result = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(result)) {
        return false;
    }

    IDXGIAdapter1* adapter;
    int adapterIdx = 0;
    bool adapterFound = false;

    while (dxgiFactory->EnumAdapters1(adapterIdx, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapterIdx++;
            continue;
        }

        result = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), NULL);
        if (SUCCEEDED(result)) {
            adapterFound = true;
            break;
        }

        adapterIdx++;
    }

    if (!adapterFound)
        return false;

    result = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device_.GetAddressOf()));
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

    IDXGISwapChain* tmpSwapChain;
    dxgiFactory->CreateSwapChain(commandQueue_.Get(), &swapChainDesc, &tmpSwapChain);

    *swapChain_.GetAddressOf() = static_cast<IDXGISwapChain3*>(tmpSwapChain);

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
    commandList_->Close();

    for (int i = 0; i < framebufferCount_; ++i) {
        result = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fences_[i].GetAddressOf()));
        if (FAILED(result))
            return false;
        fenceValues_[i] = 0;
    }

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_)
        return false;

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

    result = commandList_->Reset(commandAllocators_[frameIdx_].Get(), nullptr);
    if (FAILED(result))
        errorCallback_();

    const auto presentToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets_[frameIdx_].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto renderTargetToPresent = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets_[frameIdx_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    commandList_->ResourceBarrier(1, &presentToRenderTarget);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle{ rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), frameIdx_, rtvHandleSize_ };

    commandList_->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

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
