#pragma once

#define NOMINMAX
#include <windows.h>
#include <wrl/client.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <string>

namespace pulse {

struct D2DContext {
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_factory;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_factory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> arena_bitmap;

    bool create_device_independent_resources();
    bool create_device_resources(HWND hwnd);
    void discard_device_resources();
    void release_all_resources();

    Microsoft::WRL::ComPtr<ID2D1Bitmap> load_bitmap_from_file(const std::wstring& file_path);
};

D2D1::ColorF colorref_to_colorf(COLORREF color);

void render_d2d_spike(HWND hwnd, D2DContext& ctx);

} // namespace pulse
