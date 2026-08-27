#pragma once

#include "Engine/Core/Types.h"
#include "Westwood/Palette/Palette.h"
#include "Westwood/Shp/Shp.h"

#include <SDL3/SDL.h>

#include <d2d1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ra2yr::renderer {

struct TerrainTileVisual {
    WorldCoord center{};
    float width = 44.0F;
    float height = 22.0F;
    Color fill{};
    Color edge{};
};

struct RenderStatistics {
    std::size_t drawCalls = 0;
    std::size_t visibleTiles = 0;
    std::size_t visibleEntities = 0;
    std::size_t textLayoutUpdates = 0;
    std::size_t textureUploadCount = 0;
    bool vsyncEnabled = true;
};

struct SpriteFrameGPU {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> indexedView;
    float frameX = 0.0F;
    float frameY = 0.0F;
    float frameWidth = 0.0F;
    float frameHeight = 0.0F;
    float fullWidth = 0.0F;
    float fullHeight = 0.0F;
    // The SHP frame is cropped, but its x/y offset remains relative to the
    // full canvas.  The bottom of the visible crop is the world ground pivot.
    float pivotX = 0.0F;
    float pivotY = 0.0F;
};

struct SpriteAsset {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<SpriteFrameGPU> frames;
};

struct PaletteGPU {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;
    std::array<std::array<westwood::PaletteColor, 16>, 3> houseColorRemap{};
};

class SpriteCache {
public:
    explicit SpriteCache(ID3D11Device* device = nullptr) : device_(device) {}

    void setDevice(ID3D11Device* device) { device_ = device; }
    bool load(std::string_view assetId, const westwood::ShpTsDocument& source,
        std::size_t& uploadCount, std::string& error);
    [[nodiscard]] const SpriteAsset* find(std::string_view assetId) const;

private:
    ID3D11Device* device_ = nullptr;
    std::unordered_map<std::string, SpriteAsset> assets_;
};

class D3D11Renderer {
public:
    D3D11Renderer();
    ~D3D11Renderer();

    bool initialize(SDL_Window* window, std::string& error);
    void resize();
    void beginFrame();
    void present();

    bool loadTexture(std::string_view assetId, const std::filesystem::path& path, std::string& error);
    bool loadPalette(std::string_view assetId, const westwood::Palette& palette, std::string& error);
    bool loadSpriteAsset(std::string_view assetId, const westwood::ShpTsDocument& source, std::string& error);

    void buildStaticTerrain(const std::vector<TerrainTileVisual>& tiles, std::string& error);
    void setWorldCamera(WorldCoord worldCenter, float zoom, ScreenCoord viewportCenter,
        float tileWidth, float tileHeight);
    void drawStaticTerrain();
    void drawImage(std::string_view assetId, Rect rect, Color tint = {1.0F, 1.0F, 1.0F, 1.0F});
    void drawRect(Rect rect, Color color);
    void drawBorder(Rect rect, Color color, float thickness = 2.0F);
    void drawLine(ScreenCoord start, ScreenCoord end, Color color, float thickness = 1.0F);
    void drawCircle(ScreenCoord center, float radius, Color color, float thickness = 2.0F,
        bool dashed = false);
    void drawDiamond(ScreenCoord center, float tileWidth, float tileHeight, Color color, Color edge);
    void drawSprite(std::string_view spriteAssetId, std::string_view paletteAssetId, std::size_t frameIndex,
        Owner owner, ScreenCoord center, float scale);
    [[nodiscard]] Rect spriteBounds(std::string_view spriteAssetId, std::size_t frameIndex,
        ScreenCoord ground, float scale) const;
    void drawText(std::wstring text, Rect rect, int size, Color color, bool centered = true);

    void setWorldStats(std::size_t visibleTiles, std::size_t visibleEntities);
    void setVSync(bool enabled) { vsyncEnabled_ = enabled; }
    void toggleVSync() { vsyncEnabled_ = !vsyncEnabled_; }
    [[nodiscard]] bool vsyncEnabled() const { return vsyncEnabled_; }
    [[nodiscard]] const RenderStatistics& statistics() const { return statistics_; }
    [[nodiscard]] float logicalWidth() const { return logicalWidth_; }
    [[nodiscard]] float logicalHeight() const { return logicalHeight_; }

public:
    struct Vertex {
        float x;
        float y;
        float u;
        float v;
        float r;
        float g;
        float b;
        float a;
    };

    struct TextItem {
        std::wstring text;
        Rect rect;
        int size;
        Color color;
        bool centered;
    };

private:
    bool createDevice(SDL_Window* window, std::string& error);
    bool createBackBuffer(std::string& error);
    bool createPipelines(std::string& error);
    bool createTextResources(std::string& error);
    void flushSolidGeometry();
    void drawTextureInternal(ID3D11ShaderResourceView* view, Rect rect, Color tint);
    void flushText();
    [[nodiscard]] IDWriteTextFormat* textFormat(int size, bool centered);
    [[nodiscard]] IDWriteTextLayout* textLayout(const TextItem& item);
    [[nodiscard]] float ndcX(float logical) const;
    [[nodiscard]] float ndcY(float logical) const;

    SDL_Window* window_ = nullptr;
    int pixelWidth_ = 1280;
    int pixelHeight_ = 720;
    int viewportWidth_ = 1280;
    int viewportHeight_ = 720;
    float logicalWidth_ = 1920.0F;
    float logicalHeight_ = 1080.0F;
    bool comInitialized_ = false;
    bool vsyncEnabled_ = true;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> worldVertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> solidPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> texturePixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> indexedSpritePixelShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> terrainVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> spriteConstantsBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> worldConstantsBuffer_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blendState_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerState_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> pointSamplerState_;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> d2dTarget_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    std::unordered_map<int, Microsoft::WRL::ComPtr<IDWriteTextFormat>> textFormats_;
    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<IDWriteTextLayout>> textLayouts_;

    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> imageTextures_;
    std::unordered_map<std::string, PaletteGPU> paletteTextures_;
    SpriteCache spriteCache_;

    std::size_t terrainVertexCount_ = 0;
    WorldCoord worldCameraCenter_{};
    ScreenCoord worldViewportCenter_{795.0F, 440.0F};
    float worldCameraZoom_ = 1.0F;
    float worldTileWidth_ = 44.0F;
    float worldTileHeight_ = 22.0F;
    std::vector<Vertex> solidVertices_;
    std::vector<TextItem> textItems_;
    RenderStatistics statistics_{};
};

} // namespace ra2yr::renderer
