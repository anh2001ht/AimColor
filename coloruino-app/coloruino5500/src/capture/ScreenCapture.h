#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <memory>
#include <mutex>
#include <array>
#include <atomic>
#include <chrono>

using Microsoft::WRL::ComPtr;

class UltraOptimizedDXGICapture {
private:
 ComPtr<ID3D11Device> device;
 ComPtr<ID3D11DeviceContext> context;
 ComPtr<IDXGIOutputDuplication> duplication;
 ComPtr<ID3D11Texture2D> stagingTexture; // full-screen fallback
 ComPtr<ID3D11Texture2D> regionStagingTexture; // FOV-sized staging for CPU path
 D3D11_TEXTURE2D_DESC textureDesc;

 bool initialized = false;
 std::mutex captureMutex;

 int screenWidth = 0;
 int screenHeight = 0;
 int regionStagingW = 0;
 int regionStagingH = 0;

 // ── Double buffer ──
 struct BufferSet {
 std::unique_ptr<BYTE[]> buffer;
 size_t size;
 std::atomic<bool> ready{ false };
 };

 static constexpr int BUFFER_COUNT = 2;
 std::array<BufferSet, BUFFER_COUNT> buffers;
 std::atomic<int> currentReadBuffer{ 0 };
 std::atomic<int> currentWriteBuffer{ 1 };
 std::atomic<bool> skipNextFrame{ false };

 struct CachedRegion {
 int x, y, w, h;
 std::chrono::high_resolution_clock::time_point lastUpdate;
 bool valid = false;
 } cachedRegion;

 struct AdaptiveQuality {
 int currentLevel = 2;
 std::chrono::high_resolution_clock::time_point lastAdjustment;
 float averageFrameTime = 0.0f;
 int frameCount = 0;
 } adaptiveQuality;

 void UpdateAdaptiveQuality(float frameTime);
 bool EnsureRegionStaging(int w, int h);

 // ── GPU Compute Shader members ──
 ComPtr<ID3D11ComputeShader> computeShader;
 ComPtr<ID3D11Texture3D> lutTexture3D;
 ComPtr<ID3D11ShaderResourceView> lutSRV;
 ComPtr<ID3D11Texture2D> gpuCaptureTex; // DEFAULT usage, SRV-bindable
 ComPtr<ID3D11ShaderResourceView> captureSRV;
 ComPtr<ID3D11Buffer> constantBuffer;
 ComPtr<ID3D11Buffer> resultBuffer; // UAV for compute output
 ComPtr<ID3D11UnorderedAccessView> resultUAV;
 ComPtr<ID3D11Buffer> resultStagingBuf; // 4-byte readback

 bool gpuComputeInitialized = false;
 int lastLutColorMode = -1;
 bool lastLutUseFilter = false;

 bool InitializeGPUCompute();

public:
 bool Initialize();
 bool CaptureRegionAdaptive(int x, int y, int w, int h, BYTE** outData, UINT timeout = 0);
 bool CaptureRegionGPU(int x, int y, int w, int h, int& outDx, int& outDy, bool& outFound, UINT timeout = 0);
 void UploadLUT(const std::array<bool, 256 * 256 * 256>& lut);
 void Cleanup();

 int GetScreenWidth() const { return screenWidth; }
 int GetScreenHeight() const { return screenHeight; }
 void SetFrameSkip(bool skip) { skipNextFrame = skip; }
 bool IsGPUComputeReady() const { return gpuComputeInitialized; }

 ~UltraOptimizedDXGICapture() { Cleanup(); }
};

bool InitializeOptimizedCapture();
extern std::unique_ptr<UltraOptimizedDXGICapture> g_optimizedCapture;
