// capture.cpp — Windows 10/11, DXGI Desktop Duplication + compute shader
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// ---------------- global ----------------
static std::atomic<bool> g_running{false};
static std::atomic<bool> g_exit{false};
static std::thread       g_thread;
static std::mutex        g_outMtx;

static void sendJson(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_outMtx);
    fputs(s.c_str(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

// ---------------- shader ----------------
static const char* kShaderSrc = R"(
struct Zone { uint x, y, w, h; };
StructuredBuffer<Zone> zones : register(t0);
Texture2D<float4> input : register(t1);
RWTexture2D<float4> outTex : register(u0);

cbuffer Params : register(b0) { uint numZones; };

float3 srgb_to_linear(float3 c) { return pow(saturate(c), 2.2f); }

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= numZones) return;
    Zone z = zones[tid.x];
    if (z.w == 0 || z.h == 0) { outTex[uint2(tid.x, 0)] = float4(0,0,0,1); return; }
    const int S = 4;
    float3 sum = 0;
    for (int sy = 0; sy < S; ++sy)
    for (int sx = 0; sx < S; ++sx) {
        uint px = z.x + (uint)((sx + 0.5f) * z.w / S);
        uint py = z.y + (uint)((sy + 0.5f) * z.h / S);
        px = min(px, z.x + z.w - 1);
        py = min(py, z.y + z.h - 1);
        float4 c = input.Load(int3(px, py, 0));
        sum += srgb_to_linear(float3(c.r, c.g, c.b));
    }
    outTex[uint2(tid.x, 0)] = float4(sum / (float)(S*S), 1.0f);
}
)";

struct Zone { UINT x, y, w, h; };

// ---------------- monitor helpers ----------------
static std::string wstr_to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], sz, nullptr, nullptr);
    return s;
}

struct MonitorInfo { int index, x, y, w, h; bool primary; std::string name; };

static bool enumerateMonitors(std::vector<MonitorInfo>& out) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) return false;
    int idx = 0;
    for (UINT ai = 0; ; ++ai) {
        IDXGIAdapter1* ad = nullptr;
        if (factory->EnumAdapters1(ai, &ad) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT oi = 0; ; ++oi) {
            IDXGIOutput* o = nullptr;
            if (ad->EnumOutputs(oi, &o) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC d; o->GetDesc(&d);
            MonitorInfo m;
            m.index   = idx++;
            m.x       = d.DesktopCoordinates.left;
            m.y       = d.DesktopCoordinates.top;
            m.w       = d.DesktopCoordinates.right  - d.DesktopCoordinates.left;
            m.h       = d.DesktopCoordinates.bottom - d.DesktopCoordinates.top;
            m.primary = (d.DesktopCoordinates.left == 0 && d.DesktopCoordinates.top == 0);
            m.name    = wstr_to_utf8(d.DeviceName);
            out.push_back(m);
            o->Release();
        }
        ad->Release();
    }
    factory->Release();
    return true;
}

static bool getMonitorSize(int index, UINT& w, UINT& h) {
    std::vector<MonitorInfo> mons;
    if (!enumerateMonitors(mons)) return false;
    for (auto& m : mons) if (m.index == index) { w = (UINT)m.w; h = (UINT)m.h; return true; }
    return false;
}

static IDXGIOutput1* findOutput(int index, UINT& screenW, UINT& screenH) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) return nullptr;
    int idx = 0;
    IDXGIOutput1* result = nullptr;
    for (UINT ai = 0; !result; ++ai) {
        IDXGIAdapter1* ad = nullptr;
        if (factory->EnumAdapters1(ai, &ad) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT oi = 0; ; ++oi) {
            IDXGIOutput* o = nullptr;
            if (ad->EnumOutputs(oi, &o) == DXGI_ERROR_NOT_FOUND) break;
            if (idx == index) {
                DXGI_OUTPUT_DESC d; o->GetDesc(&d);
                screenW = d.DesktopCoordinates.right  - d.DesktopCoordinates.left;
                screenH = d.DesktopCoordinates.bottom - d.DesktopCoordinates.top;
                o->QueryInterface(__uuidof(IDXGIOutput1), (void**)&result);
                o->Release();
                break;
            }
            idx++;
            o->Release();
        }
        ad->Release();
    }
    factory->Release();
    return result;
}

// ---------------- layout ----------------
struct LayoutParams {
    int   top = 0, right = 0, bottom = 0, left = 0;
    float thickness = 0.06f;
    int   offset    = 0;
    float cornerGap = 0.02f;
    std::string startCorner = "top-left";
    std::string direction   = "cw";
};

// CW baseline: top L->R, right T->B, bottom R->L, left B->T.
// Then rotate to startCorner and reverse if ccw.
static std::vector<Zone> buildZones(UINT W, UINT H, const LayoutParams& p) {
    std::vector<Zone> cw;
    if (W < 4 || H < 4) return cw;

    UINT minDim = std::min(W, H);

    UINT strip = (UINT)std::max(1.0f, std::round(p.thickness * (float)minDim));
    strip = std::min(strip, minDim / 3);

    UINT off    = (UINT)std::max(0, p.offset);
    UINT maxOff = (minDim / 2 > strip) ? (minDim / 2 - strip) : 0;
    off = std::min(off, maxOff);

    auto range = [](UINT total, int n, int i) -> std::pair<UINT, UINT> {
        return { (UINT)((UINT64)total * i / n), (UINT)((UINT64)total * (i + 1) / n) };
    };

    // top, L->R
    if (p.top > 0) {
        UINT gapPx = (UINT)std::round(p.cornerGap * (float)W);
        gapPx = std::min(gapPx, W / 2 > 1 ? W / 2 - 1 : 0);
        UINT span = W - 2 * gapPx;
        UINT y = off;
        UINT h = (y < H) ? std::min(strip, H - y) : 0;
        if (h > 0 && span > 0)
            for (int i = 0; i < p.top; ++i) {
                auto ab = range(span, p.top, i);
                cw.push_back({ gapPx + ab.first, y, ab.second - ab.first, h });
            }
    }

    // right, T->B
    if (p.right > 0) {
        UINT gapPx = (UINT)std::round(p.cornerGap * (float)H);
        gapPx = std::min(gapPx, H / 2 > 1 ? H / 2 - 1 : 0);
        UINT span = H - 2 * gapPx;
        UINT x = (off + strip < W) ? (W - off - strip) : 0;
        UINT w = std::min(strip, W - x);
        if (w > 0 && span > 0)
            for (int i = 0; i < p.right; ++i) {
                auto ab = range(span, p.right, i);
                cw.push_back({ x, gapPx + ab.first, w, ab.second - ab.first });
            }
    }

    // bottom, R->L
    if (p.bottom > 0) {
        UINT gapPx = (UINT)std::round(p.cornerGap * (float)W);
        gapPx = std::min(gapPx, W / 2 > 1 ? W / 2 - 1 : 0);
        UINT span = W - 2 * gapPx;
        UINT y = (off + strip < H) ? (H - off - strip) : 0;
        UINT h = std::min(strip, H - y);
        if (h > 0 && span > 0)
            for (int i = 0; i < p.bottom; ++i) {
                auto ab = range(span, p.bottom, i);
                cw.push_back({ W - gapPx - ab.second, y, ab.second - ab.first, h });
            }
    }

    // left, B->T
    if (p.left > 0) {
        UINT gapPx = (UINT)std::round(p.cornerGap * (float)H);
        gapPx = std::min(gapPx, H / 2 > 1 ? H / 2 - 1 : 0);
        UINT span = H - 2 * gapPx;
        UINT x = off;
        UINT w = std::min(strip, W - x);
        if (w > 0 && span > 0)
            for (int i = 0; i < p.left; ++i) {
                auto ab = range(span, p.left, i);
                cw.push_back({ x, H - gapPx - ab.second, w, ab.second - ab.first });
            }
    }

    int n = (int)cw.size();
    if (n == 0) return cw;

    int rot = 0;
    if      (p.startCorner == "top-right")    rot = p.top;
    else if (p.startCorner == "bottom-right") rot = p.top + p.right;
    else if (p.startCorner == "bottom-left")  rot = p.top + p.right + p.bottom;
    rot = ((rot % n) + n) % n;

    std::vector<Zone> result(n);
    for (int i = 0; i < n; ++i) result[i] = cw[(rot + i) % n];

    if (p.direction == "ccw") std::reverse(result.begin(), result.end());
    return result;
}

// ---------------- capture loop ----------------
struct CaptureConfig {
    int          monitorIndex = 0;
    LayoutParams layout;
};

static void captureLoop(CaptureConfig cfg) {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    UINT devFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, devFlags,
                                   levels, _countof(levels), D3D11_SDK_VERSION,
                                   &device, &fl, &ctx);
    if (FAILED(hr)) { sendJson("{\"event\":\"error\",\"msg\":\"D3D11CreateDevice\"}"); return; }

    ID3DBlob* csBlob = nullptr; ID3DBlob* errBlob = nullptr;
    hr = D3DCompile(kShaderSrc, strlen(kShaderSrc), nullptr, nullptr, nullptr,
                    "main", "cs_5_0", 0, 0, &csBlob, &errBlob);
    if (FAILED(hr)) {
        sendJson("{\"event\":\"error\",\"msg\":\"shader compile\"}");
        if (errBlob) errBlob->Release();
        device->Release(); ctx->Release();
        return;
    }
    ID3D11ComputeShader* cs = nullptr;
    device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &cs);
    csBlob->Release();

    ID3D11Buffer* cb = nullptr;
    {
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = 16;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(&d, nullptr, &cb);
    }

    UINT screenW = 0, screenH = 0;
    IDXGIOutput1*          output1   = nullptr;
    IDXGIOutputDuplication* dupl     = nullptr;
    ID3D11Texture2D*       zonesTex  = nullptr;
    ID3D11UnorderedAccessView* zonesUAV = nullptr;
    ID3D11Texture2D*       stagingTex = nullptr;
    ID3D11Buffer*          zonesBuf  = nullptr;
    ID3D11ShaderResourceView* zonesSRV = nullptr;
    std::vector<Zone>      zones;
    int numZones = 0;
    UINT groupsX = 0;

    auto cleanup = [&]() {
        if (zonesSRV)   { zonesSRV->Release();   zonesSRV = nullptr; }
        if (zonesBuf)   { zonesBuf->Release();   zonesBuf = nullptr; }
        if (zonesUAV)   { zonesUAV->Release();   zonesUAV = nullptr; }
        if (zonesTex)   { zonesTex->Release();   zonesTex = nullptr; }
        if (stagingTex) { stagingTex->Release(); stagingTex = nullptr; }
        if (dupl)       { dupl->Release();       dupl = nullptr; }
        if (output1)    { output1->Release();    output1 = nullptr; }
        zones.clear();
    };

    auto tryInit = [&]() -> bool {
        output1 = findOutput(cfg.monitorIndex, screenW, screenH);
        if (!output1) return false;

        hr = output1->DuplicateOutput(device, &dupl);
        if (FAILED(hr)) return false;

        zones = buildZones(screenW, screenH, cfg.layout);
        numZones = (int)zones.size();
        if (numZones == 0) return false;
        groupsX = (numZones + 63) / 64;

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = (UINT)(zones.size() * sizeof(Zone));
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(Zone);
        D3D11_SUBRESOURCE_DATA sd = {}; sd.pSysMem = zones.data();
        if (FAILED(device->CreateBuffer(&bd, &sd, &zonesBuf))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format = DXGI_FORMAT_UNKNOWN;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvd.Buffer.NumElements = numZones;
        if (FAILED(device->CreateShaderResourceView(zonesBuf, &srvd, &zonesSRV))) return false;

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = numZones; td.Height = 1;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(device->CreateTexture2D(&td, nullptr, &zonesTex))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        uavd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        if (FAILED(device->CreateUnorderedAccessView(zonesTex, &uavd, &zonesUAV))) return false;

        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(&td, nullptr, &stagingTex))) return false;

        ctx->CSSetShader(cs, nullptr, 0);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            *(UINT*)m.pData = (UINT)numZones;
            ctx->Unmap(cb, 0);
        }
        ctx->CSSetConstantBuffers(0, 1, &cb);
        ID3D11ShaderResourceView* srvs[2] = { zonesSRV, nullptr };
        ctx->CSSetShaderResources(0, 2, srvs);
        ctx->CSSetUnorderedAccessViews(0, 1, &zonesUAV, nullptr);
        return true;
    };

    if (!tryInit()) {
        sendJson("{\"event\":\"error\",\"msg\":\"init failed\"}");
        cleanup(); if (cb) cb->Release(); cs->Release();
        device->Release(); ctx->Release();
        return;
    }

    {
        std::ostringstream os;
        os << "{\"event\":\"started\",\"monitor\":" << cfg.monitorIndex
           << ",\"w\":" << screenW << ",\"h\":" << screenH
           << ",\"zones\":[";
        for (size_t i = 0; i < zones.size(); ++i) {
            if (i) os << ',';
            os << "{\"x\":" << zones[i].x << ",\"y\":" << zones[i].y
               << ",\"w\":" << zones[i].w << ",\"h\":" << zones[i].h << "}";
        }
        os << "]}";
        sendJson(os.str());
    }

    uint64_t seq = 0;

    while (g_running.load() && !g_exit.load()) {
        DXGI_OUTDUPL_FRAME_INFO info;
        IDXGIResource* res = nullptr;
        hr = dupl->AcquireNextFrame(500, &info, &res);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;

        if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL) {
            cleanup();
            bool ok = false;
            for (int attempt = 0; attempt < 120; ++attempt) {
                if (!g_running.load() || g_exit.load()) break;
                Sleep(500);
                if (tryInit()) { ok = true; break; }
                cleanup();
            }
            if (!ok) { sendJson("{\"event\":\"error\",\"msg\":\"lost access\"}"); break; }
            sendJson("{\"event\":\"reconnected\"}");
            continue;
        }
        if (FAILED(hr)) { Sleep(100); continue; }

        ID3D11Texture2D* frameTex = nullptr;
        res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&frameTex);
        res->Release();
        if (!frameTex) { dupl->ReleaseFrame(); continue; }

        ID3D11ShaderResourceView* frameSRV = nullptr;
        if (FAILED(device->CreateShaderResourceView(frameTex, nullptr, &frameSRV))) {
            frameTex->Release(); dupl->ReleaseFrame(); continue;
        }

        ctx->CSSetShaderResources(1, 1, &frameSRV);
        ctx->Dispatch(groupsX, 1, 1);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        ctx->CSSetShaderResources(1, 1, &nullSRV);
        ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        ctx->CopyResource(stagingTex, zonesTex);

        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(stagingTex, 0, D3D11_MAP_READ, 0, &m))) {
            std::string s;
            s.reserve(numZones * 12 + 32);
            s += "{\"seq\":" + std::to_string(seq++) + ",\"leds\":[";
            const uint8_t* base = (const uint8_t*)m.pData;
            for (int i = 0; i < numZones; ++i) {
                const float* c = (const float*)(base + i * 16);
                auto to8 = [](float v){
                    v = v < 0 ? 0 : (v > 1 ? 1 : v);
                    return (int)(powf(v, 1.f/2.2f) * 255.f + 0.5f);
                };
                if (i) s += ',';
                s += std::to_string(to8(c[0])); s += ',';
                s += std::to_string(to8(c[1])); s += ',';
                s += std::to_string(to8(c[2]));
            }
            s += "]}";
            sendJson(s);
            ctx->Unmap(stagingTex, 0);
        }

        ctx->CSSetUnorderedAccessViews(0, 1, &zonesUAV, nullptr);
        frameSRV->Release();
        frameTex->Release();
        dupl->ReleaseFrame();
    }

    cleanup();
    if (cb) cb->Release();
    cs->Release();
    device->Release();
    ctx->Release();
    sendJson("{\"event\":\"stopped\"}");
}

// ---------------- JSON helpers ----------------
static int findInt(const std::string& s, const std::string& key, int def) {
    std::string pat = "\"" + key + "\"";
    auto p = s.find(pat);
    if (p == std::string::npos) return def;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return def;
    ++p;
    while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
    bool neg = false;
    if (p < s.size() && s[p] == '-') { neg = true; ++p; }
    long long v = 0; bool any = false;
    while (p < s.size() && std::isdigit((unsigned char)s[p])) { v = v*10 + (s[p]-'0'); any = true; ++p; }
    return any ? (int)(neg ? -v : v) : def;
}

static double findDouble(const std::string& s, const std::string& key, double def) {
    std::string pat = "\"" + key + "\"";
    auto p = s.find(pat);
    if (p == std::string::npos) return def;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return def;
    ++p;
    while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
    char* end = nullptr;
    double v = strtod(s.c_str() + p, &end);
    if (end == s.c_str() + p) return def;
    return v;
}

static std::string findStr(const std::string& s, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    auto p = s.find(pat);
    if (p == std::string::npos) return "";
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return "";
    p = s.find('"', p);
    if (p == std::string::npos) return "";
    auto e = s.find('"', p + 1);
    if (e == std::string::npos) return "";
    return s.substr(p + 1, e - p - 1);
}

static LayoutParams parseLayout(const std::string& line) {
    LayoutParams lp;
    lp.top    = findInt(line, "top", 0);
    lp.right  = findInt(line, "right", 0);
    lp.bottom = findInt(line, "bottom", 0);
    lp.left   = findInt(line, "left", 0);
    lp.thickness  = (float)findDouble(line, "thickness", 0.06);
    lp.offset     = findInt(line, "offset", 0);
    lp.cornerGap  = (float)findDouble(line, "cornerGap", 0.02);
    lp.startCorner = findStr(line, "startCorner");
    if (lp.startCorner.empty()) lp.startCorner = "top-left";
    lp.direction = findStr(line, "direction");
    if (lp.direction.empty()) lp.direction = "cw";
    return lp;
}

// ---------------- main ----------------
int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::vector<MonitorInfo> mons;
    enumerateMonitors(mons);
    {
        std::string s = "{\"event\":\"monitors\",\"list\":[";
        for (size_t i = 0; i < mons.size(); ++i) {
            auto& m = mons[i];
            if (i) s += ',';
            char buf[256];
            snprintf(buf, sizeof(buf),
                "{\"index\":%d,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"primary\":%s,\"name\":\"%s\"}",
                m.index, m.x, m.y, m.w, m.h, m.primary ? "true" : "false", m.name.c_str());
            s += buf;
        }
        s += "]}";
        sendJson(s);
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::string cmd = findStr(line, "cmd");

        if (cmd == "start") {
            if (g_running.load()) { sendJson("{\"event\":\"error\",\"msg\":\"already running\"}"); continue; }
            CaptureConfig c;
            c.monitorIndex = findInt(line, "monitor", 0);
            c.layout = parseLayout(line);
            if (c.layout.top + c.layout.right + c.layout.bottom + c.layout.left == 0) {
                sendJson("{\"event\":\"error\",\"msg\":\"no leds configured\"}");
                continue;
            }
            g_running.store(true);
            if (g_thread.joinable()) g_thread.join();
            g_thread = std::thread(captureLoop, c);
        }
        else if (cmd == "stop") {
            g_running.store(false);
            if (g_thread.joinable()) g_thread.join();
        }
        else if (cmd == "exit") {
            g_running.store(false);
            g_exit.store(true);
            if (g_thread.joinable()) g_thread.join();
            break;
        }
    }

    g_running.store(false);
    g_exit.store(true);
    if (g_thread.joinable()) g_thread.join();
    return 0;
}