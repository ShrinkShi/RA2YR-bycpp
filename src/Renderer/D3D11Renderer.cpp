#include "Renderer/D3D11Renderer.h"

#include <d3dcompiler.h>
#include <wincodec.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
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
Texture2D imageTexture : register(t0);
SamplerState imageSampler : register(s0);
struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
float4 main(PSInput input) : SV_TARGET { return imageTexture.Sample(imageSampler, input.uv) * input.color; }
)";

constexpr char kIndexedSpritePixelShader[] = R"(
Texture2D indexedTexture : register(t0);
Texture2D paletteTexture : register(t1);
SamplerState indexedSampler : register(s0);
cbuffer SpriteConstants : register(b0) { float4 ownerColor; };
struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
float4 main(PSInput input) : SV_TARGET {
    const float paletteIndex = indexedTexture.SampleLevel(indexedSampler, input.uv, 0).r * 255.0f;
    const uint index = (uint)round(paletteIndex);
    float4 result = paletteTexture.Load(int3(index, 0, 0));
    if (index == 0) {
        result.a = 0.0f;
    } else if (index >= 192 && index <= 207) {
        const float shade = 0.60f + (index - 192.0f) / 32.0f;
        result.rgb = ownerColor.rgb * shade;
    }
    return result * input.color;
}
)";

bool compileShader(const char* source, const char* entry, const char* profile,
    ComPtr<ID3DBlob>& blob, std::string& error) {
    ComPtr<ID3DBlob> diagnostics;
    const HRESULT result = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, profile,
        D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &diagnostics);
    if (FAILED(result)) {
        error = diagnostics != nullptr ? static_cast<const char*>(diagnostics->GetBufferPointer()) :
            "D3D shader compilation failed";
        return false;
    }
    return true;
}

D3D11Renderer::Vertex vertex(float x, float y, float u, float v, Color color) {
    return {x, y, u, v, color.r, color.g, color.b, color.a};
}

void appendStaticDiamond(std::vector<D3D11Renderer::Vertex>& vertices, float left, float right, float top, float bottom,
    float centerX, float centerY, Color fill, Color edge) {
    vertices.push_back(vertex(centerX, top, 0.0F, 0.0F, fill));
    vertices.push_back(vertex(right, centerY, 0.0F, 0.0F, fill));
    vertices.push_back(vertex(centerX, bottom, 0.0F, 0.0F, fill));
    vertices.push_back(vertex(centerX, top, 0.0F, 0.0F, fill));
    vertices.push_back(vertex(centerX, bottom, 0.0F, 0.0F, fill));
    vertices.push_back(vertex(left, centerY, 0.0F, 0.0F, fill));

    const auto appendLine = [&vertices, edge](ScreenCoord start, ScreenCoord end) {
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0001F) {
            return;
        }
        constexpr float kNdcThickness = 0.0015F;
        const float nx = -dy / length * kNdcThickness * 0.5F;
        const float ny = dx / length * kNdcThickness * 0.5F;
        const ScreenCoord a{start.x + nx, start.y + ny};
        const ScreenCoord b{end.x + nx, end.y + ny};
        const ScreenCoord c{end.x - nx, end.y - ny};
        const ScreenCoord d{start.x - nx, start.y - ny};
        vertices.push_back(vertex(a.x, a.y, 0.0F, 0.0F, edge));
        vertices.push_back(vertex(b.x, b.y, 0.0F, 0.0F, edge));
        vertices.push_back(vertex(c.x, c.y, 0.0F, 0.0F, edge));
        vertices.push_back(vertex(a.x, a.y, 0.0F, 0.0F, edge));
        vertices.push_back(vertex(c.x, c.y, 0.0F, 0.0F, edge));
        vertices.push_back(vertex(d.x, d.y, 0.0F, 0.0F, edge));
    };
    appendLine({centerX, top}, {right, centerY});
    appendLine({right, centerY}, {centerX, bottom});
    appendLine({centerX, bottom}, {left, centerY});
    appendLine({left, centerY}, {centerX, top});
}

bool decodeImage(const std::filesystem::path& path, std::vector<std::uint8_t>& pixels,
    UINT& width, UINT& height, std::string& error) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    }
    if (FAILED(result)) {
        error = "WIC imaging factory creation failed";
        return false;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromFilename(path.wstring().c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(result)) {
        error = "Unable to decode UI image: " + path.string();
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height))) {
        error = "Unable to read UI image frame: " + path.string();
        return false;
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        error = "Unable to convert UI image to RGBA: " + path.string();
        return false;
    }
    pixels.resize(static_cast<std::size_t>(width) * height * 4U);
    if (FAILED(converter->CopyPixels(nullptr, width * 4U, static_cast<UINT>(pixels.size()), pixels.data()))) {
        error = "Unable to read UI image pixels: " + path.string();
        return false;
    }
    return true;
}

} // namespace

bool SpriteCache::load(std::string_view assetId, const westwood::ShpTsDocument& source,
    std::size_t& uploadCount, std::string& error) {
    if (device_ == nullptr || source.frameCount() == 0 || source.width() == 0 || source.height() == 0) {
        error = "Cannot cache an empty SHP asset";
        return false;
    }
    SpriteAsset asset;
    asset.width = source.width();
    asset.height = source.height();
    asset.frames.reserve(source.frameCount());
    for (std::size_t frameIndex = 0; frameIndex < source.frameCount(); ++frameIndex) {
        const westwood::ShpFrame& frame = source.frame(frameIndex);
        std::vector<std::uint8_t> indexed(static_cast<std::size_t>(asset.width) * asset.height, 0);
        const std::size_t sourceWidth = frame.width + (frame.width % 2U);
        if (frame.x + frame.width > asset.width || frame.y + frame.height > asset.height ||
            frame.pixels.size() < sourceWidth * frame.height) {
            error = "SHP frame exceeds its declared canvas";
            return false;
        }
        for (std::size_t row = 0; row < frame.height; ++row) {
            std::copy_n(frame.pixels.begin() + static_cast<std::ptrdiff_t>(row * sourceWidth), frame.width,
                indexed.begin() + static_cast<std::ptrdiff_t>((frame.y + row) * asset.width + frame.x));
        }
        D3D11_TEXTURE2D_DESC description{};
        description.Width = asset.width;
        description.Height = asset.height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initialData{indexed.data(), asset.width, 0};
        ComPtr<ID3D11Texture2D> texture;
        SpriteFrameGPU gpuFrame;
        if (FAILED(device_->CreateTexture2D(&description, &initialData, &texture)) ||
            FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &gpuFrame.indexedView))) {
            error = "Unable to create indexed GPU texture for SHP frame";
            return false;
        }
        asset.frames.push_back(std::move(gpuFrame));
        ++uploadCount;
    }
    assets_[std::string(assetId)] = std::move(asset);
    return true;
}

const SpriteAsset* SpriteCache::find(std::string_view assetId) const {
    const auto it = assets_.find(std::string(assetId));
    return it == assets_.end() ? nullptr : &it->second;
}

D3D11Renderer::D3D11Renderer() : spriteCache_(nullptr) {}

D3D11Renderer::~D3D11Renderer() {
    d2dTarget_.Reset();
    dwriteFactory_.Reset();
    d2dFactory_.Reset();
    if (comInitialized_) {
        CoUninitialize();
    }
}

bool D3D11Renderer::initialize(SDL_Window* window, std::string& error) {
    window_ = window;
    SDL_GetWindowSizeInPixels(window_, &pixelWidth_, &pixelHeight_);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    comInitialized_ = SUCCEEDED(comResult) && comResult != RPC_E_CHANGED_MODE;
    return createDevice(window_, error) && createPipelines(error) && createTextResources(error) &&
        createBackBuffer(error);
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
    spriteCache_.setDevice(device_.Get());
    return true;
}

bool D3D11Renderer::createPipelines(std::string& error) {
    ComPtr<ID3DBlob> vertexBlob;
    ComPtr<ID3DBlob> solidBlob;
    ComPtr<ID3DBlob> textureBlob;
    ComPtr<ID3DBlob> indexedSpriteBlob;
    if (!compileShader(kVertexShader, "main", "vs_5_0", vertexBlob, error) ||
        !compileShader(kSolidPixelShader, "main", "ps_5_0", solidBlob, error) ||
        !compileShader(kTexturePixelShader, "main", "ps_5_0", textureBlob, error) ||
        !compileShader(kIndexedSpritePixelShader, "main", "ps_5_0", indexedSpriteBlob, error)) {
        return false;
    }
    if (FAILED(device_->CreateVertexShader(vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &vertexShader_)) ||
        FAILED(device_->CreatePixelShader(solidBlob->GetBufferPointer(), solidBlob->GetBufferSize(), nullptr, &solidPixelShader_)) ||
        FAILED(device_->CreatePixelShader(textureBlob->GetBufferPointer(), textureBlob->GetBufferSize(), nullptr, &texturePixelShader_)) ||
        FAILED(device_->CreatePixelShader(indexedSpriteBlob->GetBufferPointer(), indexedSpriteBlob->GetBufferSize(), nullptr,
            &indexedSpritePixelShader_))) {
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
    D3D11_BUFFER_DESC constantDescription{};
    constantDescription.ByteWidth = 16;
    constantDescription.Usage = D3D11_USAGE_DEFAULT;
    constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device_->CreateBuffer(&constantDescription, nullptr, &spriteConstantsBuffer_))) {
        error = "D3D11 sprite constant buffer creation failed";
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
    D3D11_SAMPLER_DESC linearSampler{};
    linearSampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    linearSampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearSampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearSampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(device_->CreateSamplerState(&linearSampler, &samplerState_))) {
        error = "D3D11 linear sampler creation failed";
        return false;
    }
    D3D11_SAMPLER_DESC pointSampler = linearSampler;
    pointSampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (FAILED(device_->CreateSamplerState(&pointSampler, &pointSamplerState_))) {
        error = "D3D11 point sampler creation failed";
        return false;
    }
    return true;
}

bool D3D11Renderer::createTextResources(std::string& error) {
    D2D1_FACTORY_OPTIONS factoryOptions{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factoryOptions,
            reinterpret_cast<ID2D1Factory**>(d2dFactory_.GetAddressOf()))) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf())))) {
        error = "Direct2D/DirectWrite initialization failed";
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
    ComPtr<IDXGISurface> surface;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface)))) {
        error = "Unable to access swap-chain surface for Direct2D";
        return false;
    }
    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED));
    d2dTarget_.Reset();
    if (FAILED(d2dFactory_->CreateDxgiSurfaceRenderTarget(surface.Get(), &properties, &d2dTarget_))) {
        error = "Direct2D swap-chain render target creation failed";
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
    d2dTarget_.Reset();
    swapChain_->ResizeBuffers(0, static_cast<UINT>(pixelWidth_), static_cast<UINT>(pixelHeight_), DXGI_FORMAT_UNKNOWN, 0);
    std::string ignored;
    createBackBuffer(ignored);
}

bool D3D11Renderer::loadTexture(std::string_view assetId, const std::filesystem::path& path, std::string& error) {
    std::vector<std::uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (!decodeImage(path, pixels, width, height, error)) {
        return false;
    }
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initialData{pixels.data(), width * 4U, 0};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(device_->CreateTexture2D(&description, &initialData, &texture)) ||
        FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
        error = "Unable to create UI texture: " + path.string();
        return false;
    }
    imageTextures_[std::string(assetId)] = std::move(view);
    ++statistics_.textureUploadCount;
    return true;
}

bool D3D11Renderer::loadPalette(std::string_view assetId, const westwood::Palette& palette, std::string& error) {
    std::array<std::uint8_t, 256U * 4U> pixels{};
    for (std::size_t index = 0; index < 256; ++index) {
        const westwood::PaletteColor color = palette.color(static_cast<std::uint8_t>(index));
        pixels[index * 4] = color.r;
        pixels[index * 4 + 1] = color.g;
        pixels[index * 4 + 2] = color.b;
        pixels[index * 4 + 3] = 255;
    }
    D3D11_TEXTURE2D_DESC description{};
    description.Width = 256;
    description.Height = 1;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initialData{pixels.data(), 256U * 4U, 0};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(device_->CreateTexture2D(&description, &initialData, &texture)) ||
        FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
        error = "Unable to create GPU palette texture";
        return false;
    }
    paletteTextures_[std::string(assetId)] = std::move(view);
    ++statistics_.textureUploadCount;
    return true;
}

bool D3D11Renderer::loadSpriteAsset(std::string_view assetId, const westwood::ShpTsDocument& source, std::string& error) {
    return spriteCache_.load(assetId, source, statistics_.textureUploadCount, error);
}

void D3D11Renderer::buildStaticTerrain(const std::vector<TerrainTileVisual>& tiles, std::string& error) {
    std::vector<Vertex> vertices;
    vertices.reserve(tiles.size() * 30U);
    for (const TerrainTileVisual& tile : tiles) {
        const float left = ndcX(tile.center.x - tile.width * 0.5F);
        const float right = ndcX(tile.center.x + tile.width * 0.5F);
        const float top = ndcY(tile.center.y - tile.height * 0.5F);
        const float bottom = ndcY(tile.center.y + tile.height * 0.5F);
        appendStaticDiamond(vertices, left, right, top, bottom, ndcX(tile.center.x), ndcY(tile.center.y), tile.fill, tile.edge);
    }
    D3D11_BUFFER_DESC description{};
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initialData{vertices.data(), 0, 0};
    terrainVertexBuffer_.Reset();
    if (!vertices.empty() && FAILED(device_->CreateBuffer(&description, &initialData, &terrainVertexBuffer_))) {
        error = "Static terrain vertex buffer creation failed";
        terrainVertexCount_ = 0;
        return;
    }
    terrainVertexCount_ = vertices.size();
}

void D3D11Renderer::beginFrame() {
    const float clearColor[] = {0.005F, 0.006F, 0.008F, 1.0F};
    context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
    D3D11_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(pixelWidth_), static_cast<float>(pixelHeight_), 0.0F, 1.0F};
    context_->RSSetViewports(1, &viewport);
    context_->ClearRenderTargetView(renderTarget_.Get(), clearColor);
    solidVertices_.clear();
    textItems_.clear();
    statistics_.drawCalls = 0;
    statistics_.vsyncEnabled = vsyncEnabled_;
}

void D3D11Renderer::setWorldStats(std::size_t visibleTiles, std::size_t visibleEntities) {
    statistics_.visibleTiles = visibleTiles;
    statistics_.visibleEntities = visibleEntities;
}

float D3D11Renderer::ndcX(float logical) const {
    return logical / logicalWidth_ * 2.0F - 1.0F;
}

float D3D11Renderer::ndcY(float logical) const {
    return 1.0F - logical / logicalHeight_ * 2.0F;
}

void D3D11Renderer::drawStaticTerrain() {
    if (terrainVertexBuffer_ == nullptr || terrainVertexCount_ == 0) {
        return;
    }
    flushSolidGeometry();
    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    context_->IASetInputLayout(inputLayout_.Get());
    context_->IASetVertexBuffers(0, 1, terrainVertexBuffer_.GetAddressOf(), &stride, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context_->PSSetShader(solidPixelShader_.Get(), nullptr, 0);
    context_->OMSetBlendState(blendState_.Get(), nullptr, 0xffffffffU);
    context_->Draw(static_cast<UINT>(terrainVertexCount_), 0);
    ++statistics_.drawCalls;
}

void D3D11Renderer::drawImage(std::string_view assetId, Rect rect, Color tint) {
    const auto it = imageTextures_.find(std::string(assetId));
    if (it != imageTextures_.end()) {
        flushSolidGeometry();
        drawTextureInternal(it->second.Get(), rect, tint);
    }
}

void D3D11Renderer::drawRect(Rect rect, Color color) {
    const float left = ndcX(rect.x);
    const float right = ndcX(rect.x + rect.width);
    const float top = ndcY(rect.y);
    const float bottom = ndcY(rect.y + rect.height);
    const Vertex vertices[] = {
        vertex(left, top, 0.0F, 0.0F, color), vertex(right, top, 0.0F, 0.0F, color), vertex(right, bottom, 0.0F, 0.0F, color),
        vertex(left, top, 0.0F, 0.0F, color), vertex(right, bottom, 0.0F, 0.0F, color), vertex(left, bottom, 0.0F, 0.0F, color),
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
        vertex(ndcX(a.x), ndcY(a.y), 0.0F, 0.0F, color), vertex(ndcX(b.x), ndcY(b.y), 0.0F, 0.0F, color),
        vertex(ndcX(c.x), ndcY(c.y), 0.0F, 0.0F, color), vertex(ndcX(a.x), ndcY(a.y), 0.0F, 0.0F, color),
        vertex(ndcX(c.x), ndcY(c.y), 0.0F, 0.0F, color), vertex(ndcX(d.x), ndcY(d.y), 0.0F, 0.0F, color),
    };
    solidVertices_.insert(solidVertices_.end(), std::begin(vertices), std::end(vertices));
}

void D3D11Renderer::drawDiamond(ScreenCoord center, float tileWidth, float tileHeight, Color color, Color edge) {
    const ScreenCoord top{center.x, center.y - tileHeight * 0.5F};
    const ScreenCoord right{center.x + tileWidth * 0.5F, center.y};
    const ScreenCoord bottom{center.x, center.y + tileHeight * 0.5F};
    const ScreenCoord left{center.x - tileWidth * 0.5F, center.y};
    const Vertex vertices[] = {
        vertex(ndcX(top.x), ndcY(top.y), 0.0F, 0.0F, color), vertex(ndcX(right.x), ndcY(right.y), 0.0F, 0.0F, color),
        vertex(ndcX(bottom.x), ndcY(bottom.y), 0.0F, 0.0F, color), vertex(ndcX(top.x), ndcY(top.y), 0.0F, 0.0F, color),
        vertex(ndcX(bottom.x), ndcY(bottom.y), 0.0F, 0.0F, color), vertex(ndcX(left.x), ndcY(left.y), 0.0F, 0.0F, color),
    };
    solidVertices_.insert(solidVertices_.end(), std::begin(vertices), std::end(vertices));
    drawLine(top, right, edge);
    drawLine(right, bottom, edge);
    drawLine(bottom, left, edge);
    drawLine(left, top, edge);
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
    ++statistics_.drawCalls;
    solidVertices_.clear();
}

void D3D11Renderer::drawTextureInternal(ID3D11ShaderResourceView* view, Rect rect, Color tint) {
    const float left = ndcX(rect.x);
    const float right = ndcX(rect.x + rect.width);
    const float top = ndcY(rect.y);
    const float bottom = ndcY(rect.y + rect.height);
    const Vertex vertices[] = {
        vertex(left, top, 0.0F, 0.0F, tint), vertex(right, top, 1.0F, 0.0F, tint), vertex(right, bottom, 1.0F, 1.0F, tint),
        vertex(left, top, 0.0F, 0.0F, tint), vertex(right, bottom, 1.0F, 1.0F, tint), vertex(left, bottom, 0.0F, 1.0F, tint),
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
    ++statistics_.drawCalls;
}

void D3D11Renderer::drawSprite(std::string_view spriteAssetId, std::string_view paletteAssetId, std::size_t frameIndex,
    Owner owner, ScreenCoord center, float scale) {
    const SpriteAsset* asset = spriteCache_.find(spriteAssetId);
    const auto palette = paletteTextures_.find(std::string(paletteAssetId));
    if (asset == nullptr || asset->frames.empty() || palette == paletteTextures_.end()) {
        return;
    }
    const SpriteFrameGPU& frame = asset->frames[frameIndex % asset->frames.size()];
    const std::array<float, 4> ownerColor = owner == Owner::Red ? std::array<float, 4>{1.0F, 0.12F, 0.08F, 1.0F} :
        owner == Owner::Blue ? std::array<float, 4>{0.18F, 0.55F, 1.0F, 1.0F} :
        std::array<float, 4>{0.85F, 0.85F, 0.85F, 1.0F};
    context_->UpdateSubresource(spriteConstantsBuffer_.Get(), 0, nullptr, ownerColor.data(), 0, 0);
    flushSolidGeometry();
    const Rect target{center.x - static_cast<float>(asset->width) * scale * 0.5F,
        center.y - static_cast<float>(asset->height) * scale,
        static_cast<float>(asset->width) * scale, static_cast<float>(asset->height) * scale};
    const float left = ndcX(target.x);
    const float right = ndcX(target.x + target.width);
    const float top = ndcY(target.y);
    const float bottom = ndcY(target.y + target.height);
    const Vertex vertices[] = {
        vertex(left, top, 0.0F, 0.0F, {1.0F, 1.0F, 1.0F, 1.0F}), vertex(right, top, 1.0F, 0.0F, {1.0F, 1.0F, 1.0F, 1.0F}),
        vertex(right, bottom, 1.0F, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}), vertex(left, top, 0.0F, 0.0F, {1.0F, 1.0F, 1.0F, 1.0F}),
        vertex(right, bottom, 1.0F, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}), vertex(left, bottom, 0.0F, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}),
    };
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(vertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    std::memcpy(mapped.pData, vertices, sizeof(vertices));
    context_->Unmap(vertexBuffer_.Get(), 0);
    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    ID3D11ShaderResourceView* indexedView = frame.indexedView.Get();
    ID3D11ShaderResourceView* paletteView = palette->second.Get();
    context_->IASetInputLayout(inputLayout_.Get());
    context_->IASetVertexBuffers(0, 1, vertexBuffer_.GetAddressOf(), &stride, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context_->PSSetShader(indexedSpritePixelShader_.Get(), nullptr, 0);
    context_->PSSetSamplers(0, 1, pointSamplerState_.GetAddressOf());
    ID3D11ShaderResourceView* views[] = {indexedView, paletteView};
    context_->PSSetShaderResources(0, 2, views);
    context_->PSSetConstantBuffers(0, 1, spriteConstantsBuffer_.GetAddressOf());
    context_->OMSetBlendState(blendState_.Get(), nullptr, 0xffffffffU);
    context_->Draw(6, 0);
    ID3D11ShaderResourceView* nullViews[] = {nullptr, nullptr};
    context_->PSSetShaderResources(0, 2, nullViews);
    ++statistics_.drawCalls;
}

void D3D11Renderer::drawText(std::wstring text, Rect rect, int size, Color color, bool centered) {
    textItems_.push_back({std::move(text), rect, size, color, centered});
}

IDWriteTextFormat* D3D11Renderer::textFormat(int size, bool centered) {
    const int key = std::max(8, size) * 2 + (centered ? 1 : 0);
    const auto found = textFormats_.find(key);
    if (found != textFormats_.end()) {
        return found->second.Get();
    }
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwriteFactory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(std::max(8, size)), L"zh-CN", &format))) {
        return nullptr;
    }
    format->SetTextAlignment(centered ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    auto [it, inserted] = textFormats_.emplace(key, std::move(format));
    return it->second.Get();
}

IDWriteTextLayout* D3D11Renderer::textLayout(const TextItem& item) {
    std::wstring key = item.text;
    key.push_back(L'\x1f');
    key += std::to_wstring(item.size);
    key.push_back(L'\x1f');
    key += std::to_wstring(item.rect.width);
    key.push_back(L'\x1f');
    key += std::to_wstring(item.rect.height);
    key.push_back(L'\x1f');
    key += item.centered ? L"1" : L"0";
    const auto found = textLayouts_.find(key);
    if (found != textLayouts_.end()) {
        return found->second.Get();
    }
    IDWriteTextFormat* format = textFormat(item.size, item.centered);
    if (format == nullptr) {
        return nullptr;
    }
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwriteFactory_->CreateTextLayout(item.text.c_str(), static_cast<UINT32>(item.text.size()), format,
        std::max(1.0F, item.rect.width), std::max(1.0F, item.rect.height), &layout))) {
        return nullptr;
    }
    auto [it, inserted] = textLayouts_.emplace(std::move(key), std::move(layout));
    if (inserted) {
        ++statistics_.textLayoutUpdates;
    }
    return it->second.Get();
}

void D3D11Renderer::flushText() {
    if (textItems_.empty() || d2dTarget_ == nullptr) {
        return;
    }
    flushSolidGeometry();
    context_->Flush();
    d2dTarget_->BeginDraw();
    d2dTarget_->SetTransform(D2D1::Matrix3x2F::Scale(
        static_cast<float>(pixelWidth_) / logicalWidth_, static_cast<float>(pixelHeight_) / logicalHeight_));
    for (const TextItem& item : textItems_) {
        IDWriteTextLayout* layout = textLayout(item);
        if (layout == nullptr) {
            continue;
        }
        ComPtr<ID2D1SolidColorBrush> brush;
        const D2D1_COLOR_F color{item.color.r, item.color.g, item.color.b, item.color.a};
        if (FAILED(d2dTarget_->CreateSolidColorBrush(color, &brush))) {
            continue;
        }
        d2dTarget_->DrawTextLayout(D2D1::Point2F(item.rect.x, item.rect.y), layout, brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    const HRESULT result = d2dTarget_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        d2dTarget_.Reset();
    }
}

void D3D11Renderer::present() {
    flushSolidGeometry();
    flushText();
    swapChain_->Present(vsyncEnabled_ ? 1 : 0, 0);
}

} // namespace ra2yr::renderer
