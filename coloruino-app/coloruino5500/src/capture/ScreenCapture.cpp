#include "ScreenCapture.h"
#include "ColorDetector.h"
#include "core/Config.h"

#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

std::unique_ptr<UltraOptimizedDXGICapture> g_optimizedCapture;
static std::once_flag g_optimizedInitFlag;

// ═══════════════════════════════════════════════════════════════════
// HLSL Compute Shader - finds closest LUT-matching pixel to center
// ═══════════════════════════════════════════════════════════════════
static const char* g_csHLSL = R"(
Texture2D<float4> screenTex : register(t0);
Texture3D<float> lutTex : register(t1);
RWByteAddressBuffer result : register(u0);

cbuffer CB : register(b0) {
 uint rw, rh, cx, cy;
};

[numthreads(16, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
 if (id.x >= rw || id.y >= rh) return;

 float4 px = screenTex[uint2(id.x, id.y)];

 uint ri = (uint)(px.r * 255.0);
 uint gi = (uint)(px.g * 255.0);
 uint bi = (uint)(px.b * 255.0);

 float hit = lutTex[uint3(ri, gi, bi)];
 if (hit > 0.5)
 {
 int dx = (int)id.x - (int)cx;
 int dy = (int)id.y - (int)cy;
 uint d2 = (uint)(dx * dx + dy * dy);
 uint packed = (d2 << 16) | (id.y << 8) | id.x;
 result.InterlockedMin(0, packed);
 }
}
)";

// ═══════════════════════════════════════════════════════════════════
// Initialize - D3D11 device, DXGI duplication, staging textures
// ═══════════════════════════════════════════════════════════════════
bool UltraOptimizedDXGICapture::Initialize() {
 HRESULT hr;

 UINT createFlags = 0;
#ifdef _DEBUG
 createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

 D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
 D3D_FEATURE_LEVEL level;
 hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
 createFlags, levels, ARRAYSIZE(levels),
 D3D11_SDK_VERSION, &device, &level, &context);
 if (FAILED(hr)) return false;

 ComPtr<IDXGIDevice> dxgiDev;
 hr = device.As(&dxgiDev);
 if (FAILED(hr)) return false;

 ComPtr<IDXGIAdapter> adapter;
 hr = dxgiDev->GetAdapter(&adapter);
 if (FAILED(hr)) return false;

 ComPtr<IDXGIOutput> output;
 hr = adapter->EnumOutputs(0, &output);
 if (FAILED(hr)) return false;

 ComPtr<IDXGIOutput1> output1;
 hr = output.As(&output1);
 if (FAILED(hr)) return false;

 hr = output1->DuplicateOutput(device.Get(), &duplication);
 if (FAILED(hr)) return false;

 DXGI_OUTPUT_DESC od;
 output->GetDesc(&od);
 screenWidth = od.DesktopCoordinates.right - od.DesktopCoordinates.left;
 screenHeight = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;

 // Full-screen staging (fallback)
 textureDesc = {};
 textureDesc.Width = screenWidth;
 textureDesc.Height = screenHeight;
 textureDesc.MipLevels = 1;
 textureDesc.ArraySize = 1;
 textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
 textureDesc.SampleDesc.Count = 1;
 textureDesc.Usage = D3D11_USAGE_STAGING;
 textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
 hr = device->CreateTexture2D(&textureDesc, nullptr, &stagingTexture);
 if (FAILED(hr)) return false;

 // Double-buffer pixel data
 constexpr size_t BUF_SZ = 512 * 512 * 4;
 for (auto& bs : buffers) {
 bs.buffer = std::make_unique<BYTE[]>(BUF_SZ);
 bs.size = BUF_SZ;
 }

 initialized = true;
 return true;
}

// ═══════════════════════════════════════════════════════════════════
// Region staging - lazily created, sized to fit FOV with headroom
// ═══════════════════════════════════════════════════════════════════
bool UltraOptimizedDXGICapture::EnsureRegionStaging(int w, int h) {
 if (regionStagingTexture && regionStagingW >= w && regionStagingH >= h)
 return true;

 int nw = (w + 63) & ~63; // align up to 64
 int nh = (h + 63) & ~63;
 if (nw < 128) nw = 128;
 if (nh < 128) nh = 128;

 D3D11_TEXTURE2D_DESC d = {};
 d.Width = nw;
 d.Height = nh;
 d.MipLevels = 1;
 d.ArraySize = 1;
 d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
 d.SampleDesc.Count = 1;
 d.Usage = D3D11_USAGE_STAGING;
 d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

 regionStagingTexture.Reset();
 if (FAILED(device->CreateTexture2D(&d, nullptr, &regionStagingTexture)))
 return false;

 regionStagingW = nw;
 regionStagingH = nh;
 return true;
}

// ═══════════════════════════════════════════════════════════════════
// GPU Compute Shader Init - shader compile, LUT texture, result buf
// ═══════════════════════════════════════════════════════════════════
bool UltraOptimizedDXGICapture::InitializeGPUCompute() {
 if (gpuComputeInitialized) return true;
 if (!device || !context) return false;

 HRESULT hr;

 // ── Compile CS ──
 ComPtr<ID3DBlob> csBlob, errBlob;
 hr = D3DCompile(g_csHLSL, strlen(g_csHLSL), "ColorDetectCS",
 nullptr, nullptr, "CSMain", "cs_5_0",
 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &csBlob, &errBlob);
 if (FAILED(hr)) return false;

 hr = device->CreateComputeShader(csBlob->GetBufferPointer(),
 csBlob->GetBufferSize(), nullptr, &computeShader);
 if (FAILED(hr)) return false;

 // ── GPU capture texture (DEFAULT, SRV-bindable) ──
 D3D11_TEXTURE2D_DESC td = {};
 td.Width = screenWidth;
 td.Height = screenHeight;
 td.MipLevels = 1;
 td.ArraySize = 1;
 td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
 td.SampleDesc.Count = 1;
 td.Usage = D3D11_USAGE_DEFAULT;
 td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
 hr = device->CreateTexture2D(&td, nullptr, &gpuCaptureTex);
 if (FAILED(hr)) return false;

 D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
 srvd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
 srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
 srvd.Texture2D.MipLevels = 1;
 hr = device->CreateShaderResourceView(gpuCaptureTex.Get(), &srvd, &captureSRV);
 if (FAILED(hr)) return false;

 // ── 3D LUT texture (256^3, R8_UNORM) ──
 D3D11_TEXTURE3D_DESC ld = {};
 ld.Width = 256;
 ld.Height = 256;
 ld.Depth = 256;
 ld.MipLevels = 1;
 ld.Format = DXGI_FORMAT_R8_UNORM;
 ld.Usage = D3D11_USAGE_DEFAULT;
 ld.BindFlags = D3D11_BIND_SHADER_RESOURCE;
 hr = device->CreateTexture3D(&ld, nullptr, &lutTexture3D);
 if (FAILED(hr)) return false;

 D3D11_SHADER_RESOURCE_VIEW_DESC lsd = {};
 lsd.Format = DXGI_FORMAT_R8_UNORM;
 lsd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
 lsd.Texture3D.MipLevels = 1;
 hr = device->CreateShaderResourceView(lutTexture3D.Get(), &lsd, &lutSRV);
 if (FAILED(hr)) return false;

 // ── Constant buffer (16 bytes: rw, rh, cx, cy) ──
 D3D11_BUFFER_DESC cbd = {};
 cbd.ByteWidth = 16;
 cbd.Usage = D3D11_USAGE_DYNAMIC;
 cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
 cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
 hr = device->CreateBuffer(&cbd, nullptr, &constantBuffer);
 if (FAILED(hr)) return false;

 // ── Result buffer (UAV, 4 bytes raw) ──
 D3D11_BUFFER_DESC rbd = {};
 rbd.ByteWidth = 4;
 rbd.Usage = D3D11_USAGE_DEFAULT;
 rbd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
 rbd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
 hr = device->CreateBuffer(&rbd, nullptr, &resultBuffer);
 if (FAILED(hr)) return false;

 D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
 ud.Format = DXGI_FORMAT_R32_TYPELESS;
 ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
 ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
 ud.Buffer.NumElements = 1;
 hr = device->CreateUnorderedAccessView(resultBuffer.Get(), &ud, &resultUAV);
 if (FAILED(hr)) return false;

 // ── Staging buffer for 4-byte readback ──
 D3D11_BUFFER_DESC sbd = {};
 sbd.ByteWidth = 4;
 sbd.Usage = D3D11_USAGE_STAGING;
 sbd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
 hr = device->CreateBuffer(&sbd, nullptr, &resultStagingBuf);
 if (FAILED(hr)) return false;

 gpuComputeInitialized = true;
 return true;
}

// ═══════════════════════════════════════════════════════════════════
// Upload LUT - converts bool[16M] → R8 3D texture on GPU
// ═══════════════════════════════════════════════════════════════════
void UltraOptimizedDXGICapture::UploadLUT(const std::array<bool, 256 * 256 * 256>& lut) {
 if (!lutTexture3D) return;

 // 3D texture layout: x=R, y=G, z(depth)=B
 // LUT indexed: (R << 16) | (G << 8) | B
 // Tex linear: z * 65536 + y * 256 + x → B * 65536 + G * 256 + R
 auto texData = std::make_unique<UINT8[]>(256 * 256 * 256);

 for (int r = 0; r < 256; ++r) {
 for (int g = 0; g < 256; ++g) {
 for (int b = 0; b < 256; ++b) {
 int lutIdx = (r << 16) | (g << 8) | b;
 int texIdx = b * 65536 + g * 256 + r;
 texData[texIdx] = lut[lutIdx] ? 255 : 0;
 }
 }
 }

 context->UpdateSubresource(lutTexture3D.Get(), 0, nullptr,
 texData.get(), 256 /*rowPitch*/, 65536 /*depthPitch*/);

 lastLutColorMode = cfg::color_mode;
 lastLutUseFilter = cfg::useIstrigFilter;
}

// ═══════════════════════════════════════════════════════════════════
// CaptureRegionGPU - full GPU path: capture + detect in one shot
// Returns dx/dy relative to region center. Only 4 bytes readback.
// ═══════════════════════════════════════════════════════════════════
bool UltraOptimizedDXGICapture::CaptureRegionGPU(
 int x, int y, int w, int h, int& outDx, int& outDy, bool& outFound, UINT timeout)
{
 outFound = false;
 outDx = 0;
 outDy = 0;

 if (!initialized) return false;
 if (x < 0 || y < 0 || x + w > screenWidth || y + h > screenHeight) return false;
 if (w > 255 || h > 255) return false; // packing limit

 // Lazy GPU init
 if (!gpuComputeInitialized && !InitializeGPUCompute())
 return false;

 // Re-upload LUT if color settings changed
 FastColorDetector::EnsureTable();
 if (lastLutColorMode != cfg::color_mode || lastLutUseFilter != cfg::useIstrigFilter)
 UploadLUT(FastColorDetector::GetLookupTable());

 std::lock_guard<std::mutex> lock(captureMutex);

 // ── Acquire frame ──
 DXGI_OUTDUPL_FRAME_INFO fi;
 ComPtr<IDXGIResource> res;
 HRESULT hr = duplication->AcquireNextFrame(timeout, &fi, &res);
 if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
 if (FAILED(hr)) {
 if (hr == DXGI_ERROR_ACCESS_LOST) {
 duplication.Reset();
 initialized = false;
 Initialize();
 if (initialized) InitializeGPUCompute();
 }
 return false;
 }

 ComPtr<ID3D11Texture2D> deskTex;
 hr = res.As(&deskTex);
 if (FAILED(hr)) { duplication->ReleaseFrame(); return false; }

 // ── Copy FOV region to GPU texture (GPU→GPU, fast) ──
 D3D11_BOX box = {};
 box.left = x; box.top = y;
 box.right = x + w; box.bottom = y + h;
 box.front = 0; box.back = 1;
 context->CopySubresourceRegion(gpuCaptureTex.Get(), 0, 0, 0, 0,
 deskTex.Get(), 0, &box);

 // ── Clear result to 0xFFFFFFFF (no match) ──
 const UINT clr[4] = { 0xFFFFFFFF, 0, 0, 0 };
 context->ClearUnorderedAccessViewUint(resultUAV.Get(), clr);

 // ── Update constant buffer ──
 struct { uint32_t rw, rh, cx, cy; } cb = {
 (uint32_t)w, (uint32_t)h, (uint32_t)(w / 2), (uint32_t)(h / 2)
 };
 D3D11_MAPPED_SUBRESOURCE ms;
 hr = context->Map(constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
 if (SUCCEEDED(hr)) {
 memcpy(ms.pData, &cb, sizeof(cb));
 context->Unmap(constantBuffer.Get(), 0);
 }

 // ── Dispatch compute shader ──
 context->CSSetShader(computeShader.Get(), nullptr, 0);
 ID3D11ShaderResourceView* srvs[] = { captureSRV.Get(), lutSRV.Get() };
 context->CSSetShaderResources(0, 2, srvs);
 context->CSSetUnorderedAccessViews(0, 1, resultUAV.GetAddressOf(), nullptr);
 context->CSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
 context->Dispatch((w + 15) / 16, (h + 15) / 16, 1);

 // ── Readback result (4 bytes) ──
 context->CopyResource(resultStagingBuf.Get(), resultBuffer.Get());

 hr = context->Map(resultStagingBuf.Get(), 0, D3D11_MAP_READ, 0, &ms);
 if (FAILED(hr)) { duplication->ReleaseFrame(); return false; }

 uint32_t packed = *(uint32_t*)ms.pData;
 context->Unmap(resultStagingBuf.Get(), 0);
 duplication->ReleaseFrame();

 // ── Unbind resources ──
 ID3D11ShaderResourceView* nullSrvs[2] = {};
 ID3D11UnorderedAccessView* nullUav = nullptr;
 context->CSSetShaderResources(0, 2, nullSrvs);
 context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);

 // ── Decode result ──
 if (packed != 0xFFFFFFFF) {
 int px = packed & 0xFF;
 int py = (packed >> 8) & 0xFF;
 outDx = px - (w / 2);
 outDy = py - (h / 2);
 outFound = true;
 }

 return true;
}

// ═══════════════════════════════════════════════════════════════════
// CaptureRegionAdaptive - CPU path with CopySubresourceRegion opt
// ═══════════════════════════════════════════════════════════════════
bool UltraOptimizedDXGICapture::CaptureRegionAdaptive(
 int x, int y, int w, int h, BYTE** outData, UINT timeout)
{
 if (!initialized) return false;

 if (skipNextFrame.load()) { skipNextFrame = false; return false; }

 auto now = std::chrono::high_resolution_clock::now();

 // Cache hit - reuse last buffer if region unchanged and < 100µs old.
 // Shortened from 500µs to 100µs: silent-aim trigger latency budget
 // is tight; cached frames > 100µs old risk stale coords on one-click shots.
 if (cachedRegion.valid &&
 cachedRegion.x == x && cachedRegion.y == y &&
 cachedRegion.w == w && cachedRegion.h == h &&
 std::chrono::duration_cast<std::chrono::microseconds>(
 now - cachedRegion.lastUpdate).count() < 100)
 {
 int rd = currentReadBuffer.load();
 *outData = buffers[rd].buffer.get();
 return buffers[rd].ready.load();
 }

 std::lock_guard<std::mutex> lock(captureMutex);

 DXGI_OUTDUPL_FRAME_INFO fi;
 ComPtr<IDXGIResource> res;
 HRESULT hr = duplication->AcquireNextFrame(timeout, &fi, &res);
 if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
 if (FAILED(hr)) {
 if (hr == DXGI_ERROR_ACCESS_LOST) {
 duplication.Reset();
 stagingTexture.Reset();
 regionStagingTexture.Reset();
 initialized = false;
 Initialize();
 }
 return false;
 }

 ComPtr<ID3D11Texture2D> deskTex;
 hr = res.As(&deskTex);
 if (FAILED(hr)) { duplication->ReleaseFrame(); return false; }

 if (x < 0 || y < 0 || x + w > screenWidth || y + h > screenHeight) {
 duplication->ReleaseFrame();
 return false;
 }

 // ── CopySubresourceRegion - only copy the FOV box ──
 D3D11_BOX srcBox = {};
 srcBox.left = x; srcBox.top = y;
 srcBox.right = x + w; srcBox.bottom = y + h;
 srcBox.front = 0; srcBox.back = 1;

 bool useRegion = EnsureRegionStaging(w, h);
 ID3D11Texture2D* target = useRegion ? regionStagingTexture.Get() : stagingTexture.Get();

 if (useRegion) {
 context->CopySubresourceRegion(target, 0, 0, 0, 0,
 deskTex.Get(), 0, &srcBox);
 } else {
 context->CopyResource(stagingTexture.Get(), deskTex.Get());
 }

 D3D11_MAPPED_SUBRESOURCE mp;
 hr = context->Map(target, 0, D3D11_MAP_READ, 0, &mp);
 if (FAILED(hr)) { duplication->ReleaseFrame(); return false; }

 int wb = currentWriteBuffer.load();
 auto& bs = buffers[wb];
 size_t need = (size_t)w * h * 4;
 if (need > bs.size) {
 bs.buffer = std::make_unique<BYTE[]>(need * 2);
 bs.size = need * 2;
 }

 BYTE* src = static_cast<BYTE*>(mp.pData);
 BYTE* dst = bs.buffer.get();

 if (useRegion) {
 // Region staging: data at (0,0), tight rows
 for (int row = 0; row < h; ++row)
 memcpy(dst + row * w * 4, src + row * mp.RowPitch, w * 4);
 } else {
 // Full-screen staging fallback
 for (int row = 0; row < h; ++row) {
 int off = (y + row) * mp.RowPitch + x * 4;
 memcpy(dst + row * w * 4, src + off, w * 4);
 }
 }

 context->Unmap(target, 0);
 duplication->ReleaseFrame();

 cachedRegion = { x, y, w, h, now, true };
 bs.ready = true;
 *outData = bs.buffer.get();

 currentReadBuffer = wb;
 currentWriteBuffer = 1 - wb;

 auto end = std::chrono::high_resolution_clock::now();
 float ft = std::chrono::duration<float, std::milli>(end - now).count();
 UpdateAdaptiveQuality(ft);

 return true;
}

// ═══════════════════════════════════════════════════════════════════
// Adaptive quality
// ═══════════════════════════════════════════════════════════════════
void UltraOptimizedDXGICapture::UpdateAdaptiveQuality(float frameTime) {
 adaptiveQuality.averageFrameTime =
 (adaptiveQuality.averageFrameTime * adaptiveQuality.frameCount + frameTime)
 / (adaptiveQuality.frameCount + 1);
 adaptiveQuality.frameCount++;

 auto now = std::chrono::high_resolution_clock::now();
 auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
 now - adaptiveQuality.lastAdjustment).count();

 if (elapsed > 1000) {
 float target = 1000.0f / MonitorInfo::GetRefreshRate();
 if (adaptiveQuality.averageFrameTime > target * 1.5f && adaptiveQuality.currentLevel > 0)
 adaptiveQuality.currentLevel--;
 else if (adaptiveQuality.averageFrameTime < target * 0.8f && adaptiveQuality.currentLevel < 4)
 adaptiveQuality.currentLevel++;

 adaptiveQuality.lastAdjustment = now;
 adaptiveQuality.averageFrameTime = 0;
 adaptiveQuality.frameCount = 0;
 }
}

// ═══════════════════════════════════════════════════════════════════
// Cleanup
// ═══════════════════════════════════════════════════════════════════
void UltraOptimizedDXGICapture::Cleanup() {
 // GPU compute
 resultStagingBuf.Reset();
 resultUAV.Reset();
 resultBuffer.Reset();
 constantBuffer.Reset();
 captureSRV.Reset();
 gpuCaptureTex.Reset();
 lutSRV.Reset();
 lutTexture3D.Reset();
 computeShader.Reset();
 gpuComputeInitialized = false;

 // Core
 regionStagingTexture.Reset();
 if (duplication) duplication.Reset();
 if (stagingTexture) stagingTexture.Reset();
 if (context) context.Reset();
 if (device) device.Reset();
 initialized = false;
}

// ═══════════════════════════════════════════════════════════════════
// Global initializer
// ═══════════════════════════════════════════════════════════════════
bool InitializeOptimizedCapture() {
 std::call_once(g_optimizedInitFlag, []() {
 g_optimizedCapture = std::make_unique<UltraOptimizedDXGICapture>();
 if (!g_optimizedCapture->Initialize())
 g_optimizedCapture.reset();
 });
 return g_optimizedCapture != nullptr;
}
