#pragma once
// Backends minimos para correr el interprete Fast3D de libultraship sin GPU ni ventana:
// un rasterizador por software (para ver lo que dibujaria el juego) y una "ventana" que no hace nada.
#include <cstdint>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include "fast/backends/gfx_rendering_api.h"
#include "fast/backends/gfx_window_manager_api.h"

namespace Fast {

class GfxRenderingAPISoft final : public GfxRenderingAPI {
  public:
    GfxRenderingAPISoft(int w, int h);
    const char* GetName() override { return "Soft"; }
    int GetMaxTextureSize() override { return 4096; }
    GfxClipParameters GetClipParameters() override { return { false, false }; }
    void UnloadShader(ShaderProgram*) override {}
    void LoadShader(ShaderProgram* p) override { mCur = p; }
    void ClearShaderCache() override {}
    ShaderProgram* CreateAndLoadNewShader(uint64_t id0, uint64_t id1) override;
    ShaderProgram* LookupShader(uint64_t id0, uint64_t id1) override;
    void ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) override;
    uint32_t NewTexture() override;
    void SelectTexture(int tile, uint32_t id) override { mBound[tile & 1] = id; mSelected = tile & 1; }
    void UploadTexture(const uint8_t* rgba, uint32_t w, uint32_t h) override;
    void SetSamplerParameters(int, bool, uint32_t, uint32_t) override {}
    void SetDepthTestAndMask(bool test, bool mask) override { mDepthTest = test; mDepthMask = mask; }
    void SetZmodeDecal(bool) override {}
    void SetViewport(int x, int y, int w, int h) override { mVx = x; mVy = y; mVw = w; mVh = h; }
    void SetScissor(int x, int y, int w, int h) override { mSx = x; mSy = y; mSw = w; mSh = h; }
    void SetUseAlpha(bool a) override { mUseAlpha = a; }
    void DrawTriangles(float vbo[], size_t len, size_t tris) override;
    void Init() override {}
    void OnResize() override {}
    void StartFrame() override {}
    void EndFrame() override {}
    void FinishRender() override {}
    int CreateFramebuffer() override { return ++mFbCount; }
    void UpdateFramebufferParameters(int, uint32_t, uint32_t, uint32_t, bool, bool, bool, bool) override {}
    void StartDrawToFramebuffer(int fb, float) override { SwitchFb(fb); if (getenv("SW_DEBUG")) fprintf(stderr, "[sw] dibujar en fb %d (tris %u)\n", fb, drawnTriangles); }
    void CopyFramebuffer(int, int, int, int, int, int, int, int, int, int) override {}
    void ClearFramebuffer(bool color, bool depth) override;
    void ReadFramebufferToCPU(int, uint32_t, uint32_t, uint16_t*) override {}
    void ResolveMSAAColorBuffer(int, int) override {}
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff> GetPixelDepth(
        int, const std::set<std::pair<float, float>>&) override { return {}; }
    void* GetFramebufferTextureId(int) override { return nullptr; }
    void SelectTextureFb(int) override {}
    void DeleteTexture(uint32_t) override {}
    void SetTextureFilter(FilteringMode) override {}
    FilteringMode GetTextureFilter() override { return FILTER_NONE; }
    void SetSrgbMode() override {}
    ImTextureID GetTextureById(int) override { return 0; }
    void SetCurrentPrimDepth(float) override {}

    bool SavePng(const char* path);
    uint32_t drawnTriangles = 0;

  private:
    struct Tex { uint32_t w = 0, h = 0; std::vector<uint8_t> px; };
    int mW, mH;
    int mFb = 0, mFbCount = 0;
    std::vector<std::vector<uint8_t>> mFbColor;
    void SwitchFb(int fb);
    std::vector<uint8_t> mColor;
    std::vector<float> mDepth;
    std::vector<Tex> mTex;
    std::vector<ShaderProgram*> mPrograms;
    ShaderProgram* mCur = nullptr;
    uint32_t mBound[2] = {};
    int mSelected = 0;
    bool mDepthTest = false, mDepthMask = false, mUseAlpha = false;
    int mVx = 0, mVy = 0, mVw = 320, mVh = 240, mSx = 0, mSy = 0, mSw = 320, mSh = 240;
};

class GfxWindowBackendNull final : public GfxWindowBackend {
  public:
    void Init(const char*, const char*, bool, uint32_t w, uint32_t h, int32_t, int32_t) override { mW = w; mH = h; }
    void Close() override {}
    void SetKeyboardCallbacks(bool (*)(int), bool (*)(int), void (*)()) override {}
    void SetMouseCallbacks(bool (*)(int), bool (*)(int)) override {}
    void SetFullscreenChangedCallback(void (*)(bool)) override {}
    void SetFullscreen(bool) override {}
    void GetActiveWindowRefreshRate(uint32_t* r) override { *r = 60; }
    void SetCursorVisibility(bool) override {}
    void SetMousePos(int32_t, int32_t) override {}
    void GetMousePos(int32_t* x, int32_t* y) override { *x = *y = 0; }
    void GetMouseDelta(int32_t* x, int32_t* y) override { *x = *y = 0; }
    void GetMouseWheel(float* x, float* y) override { *x = *y = 0; }
    bool GetMouseState(uint32_t) override { return false; }
    void SetMouseCapture(bool) override {}
    bool IsMouseCaptured() override { return false; }
    void GetDimensions(uint32_t* w, uint32_t* h, int32_t* x, int32_t* y) override { *w = mW; *h = mH; *x = *y = 0; }
    void SetDimensions(uint32_t, uint32_t, int32_t, int32_t) override {}
    Ship::WindowRect GetPrimaryMonitorRect() override { return {}; }
    void HandleEvents() override {}
    bool IsFrameReady() override { return true; }
    void SwapBuffersBegin() override {}
    void SwapBuffersEnd() override {}
    double GetTime() override { return 0; }
    int GetTargetFps() override { return 30; }
    void SetTargetFps(int) override {}
    void SetMaxFrameLatency(int) override {}
    const char* GetKeyName(int) override { return ""; }
    bool CanDisableVsync() override { return false; }
    bool IsRunning() override { return true; }
    void Destroy() override {}
    bool IsFullscreen() override { return false; }

  private:
    uint32_t mW = 320, mH = 240;
};

} // namespace Fast
