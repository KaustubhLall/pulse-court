#define NOMINMAX

#include "viewer_d2d.hpp"
#include "viewer_assets.hpp"

#include <filesystem>

// windows.h maps DrawText -> DrawTextW, which mangles the ID2D1RenderTarget
// method of the same name.
#ifdef DrawText
#undef DrawText
#endif

namespace pulse {

bool D2DContext::create_device_independent_resources() {
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        reinterpret_cast<void**>(factory.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(write_factory.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(IWICImagingFactory),
        reinterpret_cast<void**>(wic_factory.GetAddressOf()));
    if (FAILED(hr)) return false;

    return true;
}

bool D2DContext::create_device_resources(HWND hwnd) {
    if (!factory) return false;

    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

    D2D1_RENDER_TARGET_PROPERTIES props = {};
    props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
    props.dpiX = 0.0f;
    props.dpiY = 0.0f;
    props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props = {};
    hwnd_props.hwnd = hwnd;
    hwnd_props.pixelSize = size;
    hwnd_props.presentOptions = D2D1_PRESENT_OPTIONS_IMMEDIATELY;

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt;
    HRESULT hr = factory->CreateHwndRenderTarget(props, hwnd_props, &rt);
    if (FAILED(hr)) return false;

    render_target = rt;

    render_target->SetDpi(96.0f, 96.0f);
    render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    hr = render_target->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::White),
        reinterpret_cast<ID2D1SolidColorBrush**>(brush.GetAddressOf()));
    if (FAILED(hr)) {
        render_target.Reset();
        return false;
    }

    return true;
}

void D2DContext::discard_device_resources() {
    render_target.Reset();
    brush.Reset();
    arena_bitmap.Reset();
}

void D2DContext::release_all_resources() {
    factory.Reset();
    write_factory.Reset();
    wic_factory.Reset();
    render_target.Reset();
    brush.Reset();
    arena_bitmap.Reset();
    text_formats.clear();
}

IDWriteTextFormat* D2DContext::text_format(int height, bool centered) {
    if (!write_factory) return nullptr;
    int key = (height << 1) | (centered ? 1 : 0);
    auto it = text_formats.find(key);
    if (it != text_formats.end()) return it->second.Get();

    // GDI CreateFontA height is a cell height; the DWrite em size is smaller.
    // 0.85 was chosen to visually match the legacy sidebar text.
    float em_size = static_cast<float>(height) * 0.85f;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    HRESULT hr = write_factory->CreateTextFormat(
        L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, em_size, L"",
        reinterpret_cast<IDWriteTextFormat**>(format.GetAddressOf()));
    if (FAILED(hr)) return nullptr;
    if (centered) {
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    text_formats[key] = format;
    return text_formats[key].Get();
}

Microsoft::WRL::ComPtr<ID2D1Bitmap> D2DContext::load_bitmap_from_file(const std::wstring& file_path) {
    if (!wic_factory || !render_target) return nullptr;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wic_factory->CreateDecoderFromFilename(
        file_path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        reinterpret_cast<IWICBitmapDecoder**>(decoder.GetAddressOf()));
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, reinterpret_cast<IWICBitmapFrameDecode**>(frame.GetAddressOf()));
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = wic_factory->CreateFormatConverter(reinterpret_cast<IWICFormatConverter**>(converter.GetAddressOf()));
    if (FAILED(hr)) return nullptr;

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    hr = render_target->CreateBitmapFromWicBitmap(
        converter.Get(),
        reinterpret_cast<ID2D1Bitmap**>(bitmap.GetAddressOf()));
    if (FAILED(hr)) return nullptr;

    return bitmap;
}

D2D1::ColorF colorref_to_colorf(COLORREF color) {
    return D2D1::ColorF(
        static_cast<float>(GetRValue(color)) / 255.0f,
        static_cast<float>(GetGValue(color)) / 255.0f,
        static_cast<float>(GetBValue(color)) / 255.0f);
}

void d2d_draw_line(D2DContext& ctx, int x1, int y1, int x2, int y2,
                   COLORREF color, int width) {
    if (!ctx.render_target || !ctx.brush) return;
    ctx.brush->SetColor(colorref_to_colorf(color));
    ctx.brush->SetOpacity(1.0f);
    // Offset by half a pixel so a 1px-wide antialiased line lands on pixel
    // centers instead of straddling two rows half-covered.
    float off = (width % 2 == 1) ? 0.5f : 0.0f;
    ctx.render_target->DrawLine(
        D2D1::Point2F(static_cast<float>(x1) + off, static_cast<float>(y1) + off),
        D2D1::Point2F(static_cast<float>(x2) + off, static_cast<float>(y2) + off),
        ctx.brush.Get(), static_cast<float>(width));
}

void d2d_draw_text(D2DContext& ctx, int x, int y, const char* text,
                   COLORREF color, int height, bool centered) {
    if (!ctx.render_target || !ctx.brush || !text || !text[0]) return;
    IDWriteTextFormat* format = ctx.text_format(height, centered);
    if (!format) return;

    wchar_t wide[512];
    int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 512);
    if (len <= 0) return;

    ctx.brush->SetColor(colorref_to_colorf(color));
    ctx.brush->SetOpacity(1.0f);

    // Legacy TextOutA drew with the cell top at y. A generous no-wrap layout
    // box keeps DWrite from clipping; centered text centers on x.
    // DrawTextLayout is used instead of DrawText because windows.h's DrawText
    // macro mangles the method name.
    float box_w = centered ? 4096.0f : 4096.0f;
    float origin_x = centered ? static_cast<float>(x) - box_w / 2.0f
                              : static_cast<float>(x);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = ctx.write_factory->CreateTextLayout(
        wide, static_cast<UINT32>(len - 1), format, box_w,
        static_cast<float>(height * 2),
        reinterpret_cast<IDWriteTextLayout**>(layout.GetAddressOf()));
    if (FAILED(hr)) return;
    ctx.render_target->DrawTextLayout(
        D2D1::Point2F(origin_x, static_cast<float>(y)), layout.Get(),
        ctx.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void render_d2d_spike(HWND hwnd, D2DContext& ctx) {
    if (!ctx.render_target) {
        if (!ctx.create_device_resources(hwnd)) {
            return;
        }
    }

    if (!ctx.arena_bitmap) {
        std::filesystem::path assets_dir = find_assets_directory();
        std::filesystem::path arena_path = assets_dir / "arena.png";
        ctx.arena_bitmap = ctx.load_bitmap_from_file(arena_path.wstring());
    }

    ctx.render_target->BeginDraw();
    ctx.render_target->Clear(D2D1::ColorF(D2D1::ColorF::Black));

    if (ctx.arena_bitmap) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        D2D1_RECT_F dest_rect = D2D1::RectF(
            static_cast<float>(rc.left),
            static_cast<float>(rc.top),
            static_cast<float>(rc.right),
            static_cast<float>(rc.bottom));

        D2D1_SIZE_F bitmap_size = ctx.arena_bitmap->GetSize();
        D2D1_RECT_F src_rect = D2D1::RectF(
            0.0f,
            0.0f,
            bitmap_size.width,
            bitmap_size.height);

        ctx.render_target->DrawBitmap(
            ctx.arena_bitmap.Get(),
            dest_rect,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            src_rect);
    }

    HRESULT hr = ctx.render_target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ctx.discard_device_resources();
    }
}

} // namespace pulse
