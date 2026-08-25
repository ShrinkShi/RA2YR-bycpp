#pragma once

#include "Engine/Core/Types.h"
#include "Westwood/Palette/Palette.h"
#include "Westwood/Shp/Shp.h"

#include <SDL3/SDL.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ra2yr::renderer {

class D3D11Renderer {
public:
    D3D11Renderer() = default;
    ~D3D11Renderer();

    bool initialize(SDL_Window* window, std::string& error);
    void resize();
    void beginFrame();
    void present();

    void drawRect(Rect rect, Color color);
    void drawBorder(Rect rect, Color color, float thickness = 2.0F);
    void drawLine(ScreenCoord start, ScreenCoord end, Color color, float thickness = 1.0F);
    void drawDiamond(ScreenCoord center, float tileWidth, float tileHeight, Color color, Color edge);
    void drawSprite(const westwood::ShpFrame& frame, const westwood::Palette& palette,
        Owner owner, ScreenCoord center, float scale);
    void drawText(std::wstring text, Rect rect, int size, Color color, bool centered = true);

    [[nodiscard]] float logicalWidth() const { return logicalWidth_; }
    [[nodiscard]] float logicalHeight() const { return logicalHeight_; }

private:
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

    struct GdiTextLayer;

    bool createDevice(SDL_Window* window, std::string& error);
    bool createBackBuffer(std::string& error);
    bool createPipelines(std::string& error);
    bool createDynamicTextures(std::string& error);
    void flushSolidGeometry();
    void drawTextureInternal(ID3D11ShaderResourceView* view, Rect rect);
    void flushText();
    [[nodiscard]] float ndcX(float logical) const;
    [[nodiscard]] float ndcY(float logical) const;

    SDL_Window* window_ = nullptr;
    int pixelWidth_ = 1280;
    int pixelHeight_ = 720;
    float logicalWidth_ = 1920.0F;
    float logicalHeight_ = 1080.0F;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> solidPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> texturePixelShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blendState_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerState_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> spriteTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> spriteView_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> textTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> textView_;

    std::unique_ptr<GdiTextLayer> textLayer_;
    std::vector<Vertex> solidVertices_;
    std::vector<TextItem> textItems_;
};

} // namespace ra2yr::renderer
