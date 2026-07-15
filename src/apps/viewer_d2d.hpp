#pragma once

#define NOMINMAX
#include <windows.h>
#include <wrl/client.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <map>
#include <string>

namespace pulse {

struct D2DContext {
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_factory;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_factory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> arena_bitmap;

    // DWrite text formats are device-independent: cached across device-loss
    // recreates, released only at shutdown. Key is (pixel_height << 1) |
    // centered so left- and center-aligned variants cache separately.
    std::map<int, Microsoft::WRL::ComPtr<IDWriteTextFormat>> text_formats;

    bool create_device_independent_resources();
    bool create_device_resources(HWND hwnd);
    void discard_device_resources();
    void release_all_resources();

    Microsoft::WRL::ComPtr<ID2D1Bitmap> load_bitmap_from_file(const std::wstring& file_path);

    // Returns a cached Consolas text format for the given GDI cell height
    // (the same `height` the legacy draw_text takes). May return nullptr.
    IDWriteTextFormat* text_format(int height, bool centered);
};

D2D1::ColorF colorref_to_colorf(COLORREF color);

// D2D twins of the legacy HDC helpers in viewer_assets. Coordinates are
// client-rect pixels (the render target is SetDpi(96,96)).
void d2d_draw_line(D2DContext& ctx, int x1, int y1, int x2, int y2,
                   COLORREF color, int width = 2);
void d2d_draw_text(D2DContext& ctx, int x, int y, const char* text,
                   COLORREF color, int height = 14, bool centered = false);
void d2d_draw_rect(D2DContext& ctx, RECT rc, COLORREF fill,
                   COLORREF stroke = RGB(0, 0, 0));
void d2d_draw_circle(D2DContext& ctx, int x, int y, int r, COLORREF fill,
                     COLORREF stroke);

void render_d2d_spike(HWND hwnd, D2DContext& ctx);

} // namespace pulse
