#define IMGUI_DEFINE_MATH_OPERATORS
#pragma comment(lib, "windowscodecs.lib")

#include <iostream>
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <tchar.h>
#include <dwmapi.h>
#include <map>
#include <algorithm>
#include <array>

#include "imgui.h"
#include "imgui_freetype.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "../__ext/fonts.h"
using namespace ImGui;

#define _(arg) (arg * eScale)
inline float x = GetSystemMetrics(SM_CXSCREEN);
inline float eScale = (x / 1920);

/* ----------------------------DEFINES------------------------------------ */

//#define DBG
//#define DXBG

#define _C(ARG) (CalcTextSize(ARG))
#define _CO(ARG) (GetColorU32(ARG))
#define _S(ARG) ((int)(sizeof(ARG) / sizeof(*(ARG)))) 

#define _V2 ImVec2
#define _V4 ImVec4
#define RGBA ImColor
#define TIME (Time(17))

// --- Sostituto di D3DX11CreateShaderResourceViewFromMemory ---
// Il DirectX SDK di giugno 2010 e' deprecato e non piu' installabile: questa
// funzione decodifica un'immagine in memoria con WIC (incluso nel Windows SDK)
// e la carica come shader resource view DXGI_FORMAT_R8G8B8A8_UNORM.
static bool CreateSRVFromMemory(ID3D11Device* device, const void* data, size_t size, ID3D11ShaderResourceView** out_srv)
{
    if (!device || !data || size == 0 || !out_srv)
        return false;
    *out_srv = nullptr;

    static IWICImagingFactory* s_factory = nullptr;
    if (s_factory == nullptr)
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_factory))))
            return false;
    }

    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    unsigned char* pixels = nullptr;
    bool ok = false;

    if (SUCCEEDED(s_factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), static_cast<DWORD>(size))) &&
        SUCCEEDED(s_factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(s_factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        UINT width = 0, height = 0;
        if (SUCCEEDED(converter->GetSize(&width, &height)) && width > 0 && height > 0)
        {
            const UINT stride = width * 4;
            pixels = new unsigned char[stride * height];
            if (SUCCEEDED(converter->CopyPixels(nullptr, stride, stride * height, pixels)))
            {
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = width; desc.Height = height;
                desc.MipLevels = 1; desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                D3D11_SUBRESOURCE_DATA sub = {};
                sub.pSysMem = pixels; sub.SysMemPitch = stride;

                ID3D11Texture2D* texture = nullptr;
                if (SUCCEEDED(device->CreateTexture2D(&desc, &sub, &texture)) && texture)
                {
                    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
                    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    sd.Texture2D.MipLevels = 1;
                    ok = SUCCEEDED(device->CreateShaderResourceView(texture, &sd, out_srv));
                    texture->Release();
                }
            }
        }
    }

    delete[] pixels;
    if (converter) converter->Release();
    if (frame)     frame->Release();
    if (decoder)   decoder->Release();
    if (stream)    stream->Release();
    return ok;
}

#define EIMAGE(DVC, PDATA, TARGET) \
 (CreateSRVFromMemory(DVC, PDATA, sizeof(PDATA), &TARGET))

#define EFONT(PDATA, FSIZE, CFG) \
    (io.Fonts->AddFontFromMemoryTTF(PDATA, sizeof(PDATA), FSIZE, &CFG))

/* ----------------------------------------------------------------------- */

enum tabs {
    login,
    home,
    spoofer,
    checker
};

/* ------------------------------WINDOW----------------------------------- */
inline float Time(float t) { return GetIO().DeltaTime * t; }

namespace window
{
    namespace ui
    {
        inline _V2 size = ImVec2(_(485), _(630));
        inline _V2 SizeOnChange = window::ui::size;
        inline float rounding = _(10.f);
    }

    namespace dx
    {
        inline HWND hwnd;
#ifdef DXBG
        inline _V2 size = { 1200, 800 };
#else
        inline _V2 &size = ui::size;
#endif 
    }
}

/* ----------------------------------------------------------------------- */


/* ------------------------------RENDER----------------------------------- */

namespace ui
{
    void BeforeLoop(ID3D11Device* dvc);
    void Render();
}

/* ----------------------------------------------------------------------- */

enum SerialStatus {
    null,
    changed,
    unchanged
};

/* ----------------------------FONTS-&-IMAGES------------------------------ */

struct Fonts {
    std::array<float, 4> PoppinsSizes = { _(15), _(16), _(14), _(12)};
    std::array<float, 4> PoppinsMediumSizes = { _(18), _(20), _(16), _(15) };
    std::array<float, 2> LexendSizes = { _(20), _(26) };
    
    std::array<ImFont*, 4> Poppins = {  };
    std::array<ImFont*, 4> PoppinsMedium = {  };
    std::array<ImFont*, 2> Lexend = {  };
    ImFont* icons;
    ImFont* pass;

    void RenderFonts();
}; inline Fonts f;

inline void Fonts::RenderFonts() {
    ImGuiIO& io = GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_ForceAutoHint | ImGuiFreeTypeBuilderFlags_LightHinting | ImGuiFreeTypeBuilderFlags_LoadColor;

    for (int i = 0; i < f.PoppinsSizes.size(); i++)
        f.Poppins[i] = EFONT(__Poppins, f.PoppinsSizes[i] + _(6), cfg);

    for (int i = 0; i < f.PoppinsMediumSizes.size(); i++)
        f.PoppinsMedium[i] = EFONT(__PoppinsMedium, f.PoppinsMediumSizes[i] + _(6), cfg);

    for (int i = 0; i < f.LexendSizes.size(); i++)
        f.Lexend[i] = EFONT(__Lexend, f.LexendSizes[i] + _(5), cfg);

    f.icons = EFONT(__Icons, _(20), cfg);
    f.pass = EFONT(__Passw, _(25), cfg);
}

namespace Images 
{
    inline ID3D11ShaderResourceView* bg;
    inline ID3D11ShaderResourceView* logo;
}

/* ----------------------------------------------------------------------- */

struct alpha_s {
    int index, tab;
    float alpha, time;

    bool render() {
        if (alpha == 0)
            tab = index;

        time = GetIO().DeltaTime * 9;
        return alpha = std::clamp(alpha + (time * (index == tab ? 1.f : -1.f)), 0.f, 1.f);
    }
};

/* ----------------------------------------------------------------------- */