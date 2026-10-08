// Draws the app icon and writes res/FilterLab.ico (the Windows port of scripts/make_icon.swift).
// Usage: make_icon <output.ico>
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <vector>

namespace {

template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

D2D1_COLOR_F color(uint32_t hex, float a = 1) {
    return D2D1::ColorF(float((hex >> 16) & 0xFF) / 255, float((hex >> 8) & 0xFF) / 255, float(hex & 0xFF) / 255, a);
}

ID2D1Factory* gFactory = nullptr;
IWICImagingFactory* gWic = nullptr;

ID2D1PathGeometry* path(const std::vector<D2D1_POINT_2F>& pts, bool closed) {
    ID2D1PathGeometry* geo = nullptr;
    gFactory->CreatePathGeometry(&geo);
    ID2D1GeometrySink* sink = nullptr;
    geo->Open(&sink);
    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(pts.data() + 1, UINT32(pts.size() - 1));
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    sink->Close();
    release(sink);
    return geo;
}

/// Renders the icon at `size` pixels and returns PNG bytes.
std::vector<uint8_t> drawIcon(UINT size) {
    IWICBitmap* bitmap = nullptr;
    gWic->CreateBitmap(size, size, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    ID2D1RenderTarget* rt = nullptr;
    gFactory->CreateWicBitmapRenderTarget(
        bitmap,
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                     D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96),
        &rt);
    float s = float(size) / 1024;
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    // Core Graphics coordinates: 1024 units, y up.
    rt->SetTransform(D2D1::Matrix3x2F::Scale(s, -s) * D2D1::Matrix3x2F::Translation(0, float(size)));
    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(color(0), &brush);
    ID2D1StrokeStyle* round = nullptr;
    gFactory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                            D2D1_LINE_JOIN_ROUND),
                                nullptr, 0, &round);

    D2D1_RECT_F card = D2D1::RectF(100, 100, 924, 924);
    D2D1_ROUNDED_RECT shape = D2D1::RoundedRect(card, 185, 185);
    // Soft drop shadow below the card.
    for (int i = 14; i >= 1; i--) {
        float grow = float(i) * 2.2f;
        D2D1_RECT_F r = D2D1::RectF(card.left - grow, card.top - 10 - grow, card.right + grow, card.bottom - 10 + grow);
        brush->SetColor(color(0x3A2A10, 0.35f / 14.0f));
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 185 + grow, 185 + grow), brush);
    }
    brush->SetColor(color(0xFFFDF8));
    rt->FillRoundedRectangle(shape, brush);

    ID2D1RoundedRectangleGeometry* clipGeo = nullptr;
    gFactory->CreateRoundedRectangleGeometry(shape, &clipGeo);
    rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clipGeo), nullptr);
    // Parchment.
    D2D1_GRADIENT_STOP stops[] = {{0, color(0xFFFDF6)}, {1, color(0xF1E7D3)}};
    ID2D1GradientStopCollection* stopCollection = nullptr;
    rt->CreateGradientStopCollection(stops, 2, &stopCollection);
    ID2D1LinearGradientBrush* gradient = nullptr;
    rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(512, 924), D2D1::Point2F(512, 100)),
                                  stopCollection, &gradient);
    rt->FillRectangle(card, gradient);
    // Graph paper.
    brush->SetColor(color(0xDCE6D8));
    for (float v = 100; v <= 924.01f; v += 41.2f) {
        rt->DrawLine(D2D1::Point2F(v, 100), D2D1::Point2F(v, 924), brush, 2);
        rt->DrawLine(D2D1::Point2F(100, v), D2D1::Point2F(924, v), brush, 2);
    }
    brush->SetColor(color(0xC5D4C2));
    for (float v = 100; v <= 924.01f; v += 206) {
        rt->DrawLine(D2D1::Point2F(v, 100), D2D1::Point2F(v, 924), brush, 3.5f);
        rt->DrawLine(D2D1::Point2F(100, v), D2D1::Point2F(924, v), brush, 3.5f);
    }

    // Unit circle with axes.
    const float cx = 512, cy = 600, r = 235;
    brush->SetColor(color(0x8C8270, 0.6f));
    rt->DrawLine(D2D1::Point2F(cx - r - 70, cy), D2D1::Point2F(cx + r + 70, cy), brush, 5, round);
    rt->DrawLine(D2D1::Point2F(cx, cy - r - 70), D2D1::Point2F(cx, cy + r + 70), brush, 5, round);
    brush->SetColor(color(0x2B2A33));
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush, 14);

    // Zeros (blue rings) on the circle, poles (crimson crosses) inside.
    brush->SetColor(color(0x2675B8));
    for (double a : {2.35, -2.35, 3.14159265358979}) {
        D2D1_POINT_2F p = D2D1::Point2F(cx + r * float(std::cos(a)), cy + r * float(std::sin(a)));
        rt->DrawEllipse(D2D1::Ellipse(p, 34, 34), brush, 18);
    }
    brush->SetColor(color(0xC0392B));
    for (double a : {0.55, -0.55}) {
        D2D1_POINT_2F p = D2D1::Point2F(cx + 0.8f * r * float(std::cos(a)), cy + 0.8f * r * float(std::sin(a)));
        const float d = 34;
        rt->DrawLine(D2D1::Point2F(p.x - d, p.y - d), D2D1::Point2F(p.x + d, p.y + d), brush, 18, round);
        rt->DrawLine(D2D1::Point2F(p.x - d, p.y + d), D2D1::Point2F(p.x + d, p.y - d), brush, 18, round);
    }

    // Low-pass magnitude response sweeping across the bottom, in ink blue.
    std::vector<D2D1_POINT_2F> curve;
    for (int i = 0; i <= 200; i++) {
        double t = double(i) / 200;
        float x = 150 + float(t) * 724;
        double f = std::pow(10.0, t * 2.2 - 1.1);
        double mag = 1 / std::sqrt(1 + std::pow(f, 8));
        double db = 20 * std::log10(std::fmax(mag, 1e-5));
        curve.push_back(D2D1::Point2F(x, 255 + float(std::fmax(-60.0, db)) * 2.7f));
    }
    std::vector<D2D1_POINT_2F> fill = curve;
    fill.push_back(D2D1::Point2F(874, 100));
    fill.push_back(D2D1::Point2F(150, 100));
    ID2D1PathGeometry* fillGeo = path(fill, true);
    brush->SetColor(color(0x2675B8, 0.2f));
    rt->FillGeometry(fillGeo, brush);
    ID2D1PathGeometry* curveGeo = path(curve, false);
    brush->SetColor(color(0x1F4E9E));
    rt->DrawGeometry(curveGeo, brush, 20, round);
    rt->PopLayer();

    // Thin frame.
    brush->SetColor(color(0xD9CDB5));
    rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(103, 103, 921, 921), 182, 182), brush, 6);
    rt->EndDraw();

    // Encode as PNG.
    IStream* stream = nullptr;
    CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    IWICBitmapEncoder* encoder = nullptr;
    gWic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    encoder->Initialize(stream, WICBitmapEncoderNoCache);
    IWICBitmapFrameEncode* frame = nullptr;
    encoder->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(size, size);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&format);
    frame->WriteSource(bitmap, nullptr);
    frame->Commit();
    encoder->Commit();
    STATSTG stat;
    stream->Stat(&stat, STATFLAG_NONAME);
    std::vector<uint8_t> png(size_t(stat.cbSize.QuadPart));
    LARGE_INTEGER zero = {};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    stream->Read(png.data(), ULONG(png.size()), &read);

    release(frame);
    release(encoder);
    release(stream);
    release(curveGeo);
    release(fillGeo);
    release(gradient);
    release(stopCollection);
    release(clipGeo);
    release(round);
    release(brush);
    release(rt);
    release(bitmap);
    return png;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: make_icon <output.ico>\n");
        return 2;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &gFactory);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, reinterpret_cast<void**>(&gWic));
    if (!gFactory || !gWic) return 1;

    const UINT sizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};
    std::vector<std::vector<uint8_t>> images;
    for (UINT s : sizes) images.push_back(drawIcon(s));

    FILE* f = _wfopen(argv[1], L"wb");
    if (!f) return 1;
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const size_t count = std::size(sizes);
    u16(0);
    u16(1);
    u16(uint16_t(count));
    uint32_t offset = uint32_t(6 + 16 * count);
    for (size_t i = 0; i < count; i++) {
        uint8_t dim = sizes[i] >= 256 ? 0 : uint8_t(sizes[i]);
        std::fwrite(&dim, 1, 1, f);
        std::fwrite(&dim, 1, 1, f);
        uint8_t zero = 0;
        std::fwrite(&zero, 1, 1, f);
        std::fwrite(&zero, 1, 1, f);
        u16(1);
        u16(32);
        u32(uint32_t(images[i].size()));
        u32(offset);
        offset += uint32_t(images[i].size());
    }
    for (const auto& img : images) std::fwrite(img.data(), 1, img.size(), f);
    std::fclose(f);
    std::wprintf(L"Wrote %ls\n", argv[1]);
    return 0;
}
