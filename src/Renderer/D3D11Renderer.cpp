#include "Renderer/D3D11Renderer.h"

#include <d3dcompiler.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <utility>

namespace ra2yr::renderer {
namespace {

using Microsoft::WRL::ComPtr;

constexpr char kVertexShader[] = R"(
struct VSInput { float2 position : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
VSOutput main(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
})";

constexpr char kSolidPixelShader[] = R"(
struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
float4 main(PSInput input) : SV_TARGET { return input.color; }
)";

constexpr char kTexturePixelShader[] = R"(
Texture2D spriteTexture : register(t0);
SamplerState spriteSampler : register(s0);
struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
float4 main(PSInput input) : SV_TARGET { return spriteTexture.Sample(spriteSampler, input.uv) * input.color; }
)";

bool compileShader(const char* source, const char* entry, const char* profile, ComPtr<ID3DBlob>& blob, std::string& error) {
    ComPtr<ID3DBlob> diagnostics;
    const HRESULT result = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, profile,
        D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &diagnostics);
    if (FAILED(result)) {
        error = diagnostics != nullptr ? static_cast<const char*>(diagnostics->GetBufferPointer()) : "D3D shader compilation failed";
        return false;
    }
    return true;
}

float channelForOwner(Owner owner, int offset) {
    constexpr float red[3] = {1.0F, 0.12F, 0.08F};
    constexpr float blue[3] = {0.18F, 0.55F, 1.0F};
    constexpr float neutral[3] = {0.85F, 0.85F, 0.85F};
    const float* values = owner == Owner::Red ? red : owner == Owner::Blue ? blue : neutral;
    return values[offset];
}

} // namespace

struct D3D11Renderer::GdiTextLayer {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    void* pixels = nullptr;
    std::vector<std::uint8_t> rgba;
    static constexpr int width = 1920;
    static constexpr int height = 1080;

    bool initialize() {
        dc = CreateCompatibleDC(nullptr);
        if (dc == nullptr) {
            return false;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (bitmap == nullptr) {
            return false;
        }
        previousBitmap = SelectObject(dc, bitmap);
        rgba.resize(static_cast<std::size_t>(width) * height * 4U);
        clear();
        return true;
    }

    void clear() {
        if (pixels != nullptr) {
            std::memset(pixels, 0, static_cast<std::size_t>(width) * height * 4U);
        }
    }

    void draw(const std::wstring& text, Rect rect, int size, Color color, bool centered) {
        const int fontHeight = -std::max(8, size);
        HFONT font = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Microsoft YaHei UI");
        if (font == nullptr) {
            return;
        }
        const HGDIOBJ oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(
            static_cast<int>(std::clamp(color.r, 0.0F, 1.0F) * 255.0F),
            static_cast<int>(std::clamp(color.g, 0.0F, 1.0F) * 255.0F),
            static_cast<int>(std::clamp(color.b, 0.0F, 1.0F) * 255.0F)));
        RECT target{
            static_cast<LONG>(rect.x), static_cast<LONG>(rect.y),
            static_cast<LONG>(rect.x + rect.width), static_cast<LONG>(rect.y + rect.height)};
        UINT flags = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
        if (centered) {
            flags |= DT_CENTER;
        } else {
            flags |= DT_LEFT;
        }
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &target, flags);
        SelectObject(dc, oldFont);
        DeleteObject(font);
    }

    void prepareTextureBytes() {
        const auto* source = static_cast<const std::uint8_t*>(pixels);
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            const std::uint8_t blue = source[i];
            const std::uint8_t green = source[i + 1];
            const std::uint8_t red = source[i + 2];
            const std::uint8_t alpha = std::max({red, green, blue});
            rgba[i] = red;
            rgba[i + 1] = green;
            rgba[i + 2] = blue;
            rgba[i + 3] = alpha;
        }
    }

    ~GdiTextLayer() {
        if (dc != nullptr && previousBitmap != nullptr) {
            SelectObject(dc, previousBitmap);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (dc != nullptr) {
            DeleteDC(dc);
        }
    }
};

D3D11Renderer::~D3D11Renderer() = default;

bool D3D11Renderer::initialize(SDL_Window* window, std::string& error) {
    window_ = window;
    SDL_GetWindowSizeInPixels(window_, &pixelWidth_, &pixelHeight_);
    return createDevice(window_, error) && createPipelines(error) && createBackBuffer(error) && createDynamicTextures(error);
}

bool D3D11Renderer::createDevice(SDL_Window* window, std::string& error) {
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd == nullptr) {
        error = "SDL did not expose a Win32 window handle";
        return false;
    }
    DXGI_SWAP_CHAIN_DESC swapChainDescription{};
    swapChainDescription.BufferCount = 2;
    swapChainDescription.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDescription.OutputWindow = hwnd;
    swapChainDescription.SampleDesc.Count = 1;
    swapChainDescription.Windowed = TRUE;
    swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    const HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1, D3D11_SDK_VERSION, &swapChainDescription,
        &swapChain_, &device_, nullptr, &context_);
    if (FAILED(result)) {
        error = "D3D11CreateDeviceAndSwapChain failed";
        return false;
    }
    return true;
}

bool D3D11Renderer::createPipelines(std::string& error) {
    ComPtr<ID3DBlob> vertexBlob;
    ComPtr<ID3DBlob> solidBlob;
    ComPtr<ID3DBlob> textureBlob;
    if (!compileShader(kVertexShader, "main", "vs_5_0", vertexBlob, error) ||
        !compileShader(kSolidPixelShader, "main", "ps_5_0", solidBlob, error) ||
        !compileShader(kTexturePixelShader, "main", "ps_5_0", textureBlob, error)) {
        return false;
    }
    if (FAILED(device_->CreateVertexShader(vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &vertexShader_)) ||
        FAILED(device_->CreatePixelShader(solidBlob->GetBufferPointer(), solidBlob->GetBufferSize(), nullptr, &solidPixelShader_)) ||
        FAILED(device_->CreatePixelShader(textureBlob->GetBufferPointer(), textureBlob->GetBufferSize(), nullptr, &texturePixelShader_))) {
        error = "D3D11 shader object creation failed";
        return false;
    }
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device_->CreateInputLayout(layout, 3, vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), &inputLayout_))) {
        error = "D3D11 input layout creation failed";
        return false;
    }
    D3D11_BUFFER_DESC vertexDescription{};
    vertexDescription.Usage = D3D11_USAGE_DYNAMIC;
    vertexDescription.ByteWidth = 262144U * sizeof(Vertex);
    vertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&vertexDescription, nullptr, &vertexBuffer_))) {
        error = "D3D11 vertex buffer creation failed";
        return false;
    }
    D3D11_BLEND_DESC blendDescription{};
    blendDescription.RenderTarget[0].BlendEnable = TRUE;
    blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&blendDescription, &blendState_))) {
        error = "D3D11 blend state creation failed";
        return false;
    }
    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(device_->CreateSamplerState(&samplerDescription, &samplerState_))) {
        error = "D3D11 sampler state creation failed";
        return false;
    }
    return true;
}

bool D3D11Renderer::createBackBuffer(std::string& error) {
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) ||
        FAILED(device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget_))) {
        error = "D3D11 back buffer creation failed";
        return false;
    }
    return true;
}

bool D3D11Renderer::createDynamicTextures(std::string& error) {
    auto createTexture = [this, &error](UINT width, UINT height, ComPtr<ID3D11Texture2D>& texture,
        ComPtr<ID3D11ShaderResourceView>& view) {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateTexture2D(&description, nullptr, &texture)) ||
            FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
            error = "D3D11 dynamic texture creation failed";
            return false;
        }
        return true;
    };
    textLayer_ = std::make_unique<GdiTextLayer>();
    if (!textLayer_->initialize() || !createTexture(76, 96, spriteTexture_, spriteView_) ||
        !createTexture(GdiTextLayer::width, GdiTextLayer::height, textTexture_, textView_)) {
        if (error.empty()) {
            error = "GDI text layer initialization failed";
        }
        return false;
    }
    return true;
}

void D3D11Renderer::resize() {
    SDL_GetWindowSizeInPixels(window_, &pixelWidth_, &pixelHeight_);
    if (pixelWidth_ <= 0 || pixelHeight_ <= 0) {
        return;
    }
    renderTarget_.Reset();
    swapChain_->ResizeBuffers(0, static_cast<UINT>(pixelWidth_), static_cast<UINT>(pixelHeight_), DXGI_FORMAT_UNKNOWN, 0);
    std::string ignored;
    createBackBuffer(ignored);
}

void D3D11Renderer::beginFrame() {
    const float clearColor[] = {0.005F, 0.006F, 0.008F, 1.0F};
    context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
    D3D11_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(pixelWidth_), static_cast<float>(pixelHeight_), 0.0F, 1.0F};
    context_->RSSetViewports(1, &viewport);
    context_->ClearRenderTargetView(renderTarget_.Get(), clearColor);
    solidVertices_.clear();
    textItems_.clear();
}

float D3D11Renderer::ndcX(float logical) const {
    return logical / logicalWidth_ * 2.0F - 1.0F;
}

float D3D11Renderer::ndcY(float logical) const {
    return 1.0F - logical / logicalHeight_ * 2.0F;
}

void D3D11Renderer::drawRect(Rect rect, Color color) {
    const float left = ndcX(rect.x);
    const float right = ndcX(rect.x + rect.width);
    const float top = ndcY(rect.y);
    const float bottom = ndcY(rect.y + rect.height);
    const Vertex vertices[] = {
        {left, top, 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {right, top, 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {right, bottom, 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {left, top, 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {right, bottom, 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {left, bottom, 0.0F, 0.0F, color.r, color.g, color.b, color.a},
    };
    solidVertices_.insert(solidVertices_.end(), std::begin(vertices), std::end(vertices));
}

void D3D11Renderer::drawBorder(Rect rect, Color color, float thickness) {
    drawRect({rect.x, rect.y, rect.width, thickness}, color);
    drawRect({rect.x, rect.y + rect.height - thickness, rect.width, thickness}, color);
    drawRect({rect.x, rect.y, thickness, rect.height}, color);
    drawRect({rect.x + rect.width - thickness, rect.y, thickness, rect.height}, color);
}

void D3D11Renderer::drawLine(ScreenCoord start, ScreenCoord end, Color color, float thickness) {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.01F) {
        return;
    }
    const float nx = -dy / length * thickness * 0.5F;
    const float ny = dx / length * thickness * 0.5F;
    const ScreenCoord a{start.x + nx, start.y + ny};
    const ScreenCoord b{end.x + nx, end.y + ny};
    const ScreenCoord c{end.x - nx, end.y - ny};
    const ScreenCoord d{start.x - nx, start.y - ny};
    const Vertex vertices[] = {
        {ndcX(a.x), ndcY(a.y), 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {ndcX(b.x), ndcY(b.y), 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {ndcX(c.x), ndcY(c.y), 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {ndcX(a.x), ndcY(a.y), 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {ndcX(c.x), ndcY(c.y), 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {ndcX(d.x), ndcY(d.y), 0.0F, 0.0F, color.r, color.g, color.b, color.a},
    };
    solidVertices_.insert(solidVertices_.end(), std::begin(vertices), std::end(vertices));
}

void D3D11Renderer::drawDiamond(ScreenCoord center, float tileWidth, float tileHeight, Color color, Color edge) {
    const ScreenCoord top{center.x, center.y - tileHeight * 0.5F};
    const ScreenCoord right{center.x + tileWidth * 0.5F, center.y};
    const ScreenCoord bottom{center.x, center.y + tileHeight * 0.5F};
    const ScreenCoord left{center.x - tileWidth * 0.5F, center.y};
    const Vertex vertices[] = {
        {ndcX(top.x), ndcY(top.y), 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {ndcX(right.x), ndcY(right.y), 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {ndcX(bottom.x), ndcY(bottom.y), 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {ndcX(top.x), ndcY(top.y), 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {ndcX(bottom.x), ndcY(bottom.y), 0.0F, 0.0F, color.r, color.g, color.b, color.a},
        {ndcX(left.x), ndcY(left.y), 0.0F, 0.0F, color.r, color.g, color.b, color.a},
    };
    solidVertices_.insert(solidVertices_.end(), std::begin(vertices), std::end(vertices));
    drawLine(top, right, edge, 1.0F);
    drawLine(right, bottom, edge, 1.0F);
    drawLine(bottom, left, edge, 1.0F);
    drawLine(left, top, edge, 1.0F);
}

void D3D11Renderer::flushSolidGeometry() {
    if (solidVertices_.empty()) {
        return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(vertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        solidVertices_.clear();
        return;
    }
    std::memcpy(mapped.pData, solidVertices_.data(), solidVertices_.size() * sizeof(Vertex));
    context_->Unmap(vertexBuffer_.Get(), 0);
    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    context_->IASetInputLayout(inputLayout_.Get());
    context_->IASetVertexBuffers(0, 1, vertexBuffer_.GetAddressOf(), &stride, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context_->PSSetShader(solidPixelShader_.Get(), nullptr, 0);
    context_->OMSetBlendState(blendState_.Get(), nullptr, 0xffffffffU);
    context_->Draw(static_cast<UINT>(solidVertices_.size()), 0);
    solidVertices_.clear();
}

void D3D11Renderer::drawTextureInternal(ID3D11ShaderResourceView* view, Rect rect) {
    const float left = ndcX(rect.x);
    const float right = ndcX(rect.x + rect.width);
    const float top = ndcY(rect.y);
    const float bottom = ndcY(rect.y + rect.height);
    const Vertex vertices[] = {
        {left, top, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F},
        {right, top, 1.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F},
        {right, bottom, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F},
        {left, top, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F},
        {right, bottom, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F},
        {left, bottom, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F},
    };
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(vertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    std::memcpy(mapped.pData, vertices, sizeof(vertices));
    context_->Unmap(vertexBuffer_.Get(), 0);
    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    context_->IASetInputLayout(inputLayout_.Get());
    context_->IASetVertexBuffers(0, 1, vertexBuffer_.GetAddressOf(), &stride, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context_->PSSetShader(texturePixelShader_.Get(), nullptr, 0);
    context_->PSSetSamplers(0, 1, samplerState_.GetAddressOf());
    context_->PSSetShaderResources(0, 1, &view);
    context_->OMSetBlendState(blendState_.Get(), nullptr, 0xffffffffU);
    context_->Draw(6, 0);
    ID3D11ShaderResourceView* nullView = nullptr;
    context_->PSSetShaderResources(0, 1, &nullView);
}

void D3D11Renderer::drawSprite(const westwood::ShpFrame& frame, const westwood::Palette& palette,
    Owner owner, ScreenCoord center, float scale) {
    if (frame.width == 0 || frame.height == 0 || frame.pixels.empty()) {
        return;
    }
    const std::size_t dataWidth = frame.width + (frame.width % 2U);
    const std::size_t textureWidth = 76;
    const std::size_t textureHeight = 96;
    std::vector<std::uint8_t> rgba(textureWidth * textureHeight * 4U, 0);
    for (std::size_t row = 0; row < frame.height; ++row) {
        for (std::size_t column = 0; column < frame.width; ++column) {
            const std::size_t sourceIndex = row * dataWidth + column;
            const std::uint8_t paletteIndex = frame.pixels[sourceIndex];
            const auto color = palette.color(paletteIndex);
            if (paletteIndex == 0 || frame.x + column >= textureWidth || frame.y + row >= textureHeight) {
                continue;
            }
            const std::size_t destinationIndex = (static_cast<std::size_t>(frame.y) + row) * textureWidth +
                static_cast<std::size_t>(frame.x) + column;
            float red = color.r / 255.0F;
            float green = color.g / 255.0F;
            float blue = color.b / 255.0F;
            if (paletteIndex >= 0xc0U && paletteIndex <= 0xcfU) {
                const float shade = 0.60F + static_cast<float>(paletteIndex - 0xc0U) / 32.0F;
                red = channelForOwner(owner, 0) * shade;
                green = channelForOwner(owner, 1) * shade;
                blue = channelForOwner(owner, 2) * shade;
            }
            rgba[destinationIndex * 4] = static_cast<std::uint8_t>(std::clamp(red, 0.0F, 1.0F) * 255.0F);
            rgba[destinationIndex * 4 + 1] = static_cast<std::uint8_t>(std::clamp(green, 0.0F, 1.0F) * 255.0F);
            rgba[destinationIndex * 4 + 2] = static_cast<std::uint8_t>(std::clamp(blue, 0.0F, 1.0F) * 255.0F);
            rgba[destinationIndex * 4 + 3] = 255;
        }
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(spriteTexture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    for (std::size_t row = 0; row < textureHeight; ++row) {
        std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + row * mapped.RowPitch,
            rgba.data() + row * textureWidth * 4U, textureWidth * 4U);
    }
    context_->Unmap(spriteTexture_.Get(), 0);
    flushSolidGeometry();
    const Rect target{
        center.x - static_cast<float>(textureWidth) * scale * 0.5F,
        center.y - static_cast<float>(textureHeight) * scale,
        static_cast<float>(textureWidth) * scale,
        static_cast<float>(textureHeight) * scale};
    drawTextureInternal(spriteView_.Get(), target);
}

void D3D11Renderer::drawText(std::wstring text, Rect rect, int size, Color color, bool centered) {
    textItems_.push_back({std::move(text), rect, size, color, centered});
}

void D3D11Renderer::flushText() {
    if (textItems_.empty() || textLayer_ == nullptr) {
        return;
    }
    textLayer_->clear();
    for (const TextItem& item : textItems_) {
        textLayer_->draw(item.text, item.rect, item.size, item.color, item.centered);
    }
    textLayer_->prepareTextureBytes();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(textTexture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    for (int row = 0; row < GdiTextLayer::height; ++row) {
        std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch,
            textLayer_->rgba.data() + static_cast<std::size_t>(row) * GdiTextLayer::width * 4U,
            static_cast<std::size_t>(GdiTextLayer::width) * 4U);
    }
    context_->Unmap(textTexture_.Get(), 0);
    drawTextureInternal(textView_.Get(), {0.0F, 0.0F, logicalWidth_, logicalHeight_});
}

void D3D11Renderer::present() {
    flushSolidGeometry();
    flushText();
    swapChain_->Present(1, 0);
}

} // namespace ra2yr::renderer
