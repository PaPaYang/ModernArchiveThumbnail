#include "ThumbnailProvider.h"
#include <wincodec.h>
#include <shlwapi.h>
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

using namespace std;

// (이전에 작성된 상수 및 유틸리티 함수들: MIN_ASPECT_RATIO, MAX_FILE_SIZE, GetWIC(), GetFilePriority 등 유지)
const double MIN_ASPECT_RATIO = 0.15;
const double MAX_ASPECT_RATIO = 4.5;
const long long MAX_FILE_SIZE = 40 * 1024 * 1024;
const int SCAN_TIMEOUT_MS = 3000;
const int MAX_SCAN_FILES = 200;

static INIT_ONCE g_WicOnce = INIT_ONCE_STATIC_INIT;
static IWICImagingFactory* g_pWICFactory = nullptr;

BOOL CALLBACK InitWicFactory(PINIT_ONCE, PVOID, PVOID*) {
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_pWICFactory)));
}
IWICImagingFactory* GetWIC() {
    InitOnceExecuteOnce(&g_WicOnce, InitWicFactory, NULL, NULL);
    return g_pWICFactory;
}

int GetFirstNumberFromUtf8(const string& fn) {
    int num = 0; bool found = false;
    for (char c : fn) {
        if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); found = true; if (num > 999999) break; }
        else if (found) break;
    }
    return found ? num : 1000000;
}

int GetFilePriority(const string& fn) {
    if (fn.find("cover") != string::npos) return 1;
    if (fn.find("front") != string::npos) return 2;
    if (fn.find("index") != string::npos) return 3;
    if (fn.find("folder") != string::npos) return 4;
    return 100;
}

int GetDepth(const string& path) {
    int d = 0; for (char c : path) if (c == '/' || c == '\\') d++; return d;
}

HRESULT CreateHBITMAPFromData(const vector<BYTE>& buf, UINT cx, HBITMAP* phbmp) {
    if (buf.empty() || !GetWIC()) return E_FAIL;
    ComPtr<IStream> spStream(SHCreateMemStream(buf.data(), (UINT)buf.size()));
    if (!spStream) return E_OUTOFMEMORY;

    ComPtr<IWICBitmapDecoder> spDec;
    if (FAILED(GetWIC()->CreateDecoderFromStream(spStream.Get(), NULL, WICDecodeMetadataCacheOnLoad, &spDec))) return E_FAIL;
    ComPtr<IWICBitmapFrameDecode> spFrame;
    if (FAILED(spDec->GetFrame(0, &spFrame)) || !spFrame) return E_FAIL;

    UINT w, h; spFrame->GetSize(&w, &h);
    if (w == 0 || h == 0) return E_FAIL;
    double r = (double)w / h; if (r < MIN_ASPECT_RATIO || r > MAX_ASPECT_RATIO) return S_FALSE;

    UINT th = (UINT)((double)h * cx / w); if (th < 1) th = 1; UINT tw = cx;
    ComPtr<IWICBitmapScaler> spScaler;
    if (FAILED(GetWIC()->CreateBitmapScaler(&spScaler)) || !spScaler) return E_FAIL;
    spScaler->Initialize(spFrame.Get(), tw, th, WICBitmapInterpolationModeFant);

    ComPtr<IWICFormatConverter> spConv;
    if (FAILED(GetWIC()->CreateFormatConverter(&spConv)) || !spConv) return E_FAIL;
    spConv->Initialize(spScaler.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);

    BITMAPINFO bmi = { 0 }; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = tw; bmi.bmiHeader.biHeight = -(long)th;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;

    void* dstPtr = nullptr; *phbmp = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &dstPtr, NULL, 0);
    if (*phbmp && dstPtr) {
        if (FAILED(spConv->CopyPixels(NULL, tw * 4, tw * 4 * th, (BYTE*)dstPtr))) { DeleteObject(*phbmp); *phbmp = NULL; return E_FAIL; }
    }
    return *phbmp ? S_OK : E_FAIL;
}

struct StreamContext { IStream* pStream; BYTE buffer[65536]; };
la_ssize_t StreamRead(struct archive* a, void* cd, const void** b) {
    StreamContext* ctx = (StreamContext*)cd; ULONG r = 0;
    if (FAILED(ctx->pStream->Read(ctx->buffer, sizeof(ctx->buffer), &r))) return -1;
    *b = ctx->buffer; return (la_ssize_t)r;
}
la_int64_t StreamSeek(struct archive* a, void* cd, la_int64_t req, int w) {
    StreamContext* ctx = (StreamContext*)cd; LARGE_INTEGER li; li.QuadPart = req; ULARGE_INTEGER np;
    ctx->pStream->Seek(li, w == SEEK_SET ? 0 : (w == SEEK_CUR ? 1 : 2), &np); return (la_int64_t)np.QuadPart;
}

// CThumbnailProvider 구현
CThumbnailProvider::CThumbnailProvider() : m_cRef(1) {}
CThumbnailProvider::~CThumbnailProvider() {}

IFACEMETHODIMP CThumbnailProvider::QueryInterface(REFIID riid, void** ppv) {
    static const QITAB qit[] = {
        QITABENT(CThumbnailProvider, IInitializeWithStream),
        QITABENT(CThumbnailProvider, IThumbnailProvider),
        { 0 },
    };
    return QISearch(this, qit, riid, ppv);
}
IFACEMETHODIMP_(ULONG) CThumbnailProvider::AddRef() { return InterlockedIncrement(&m_cRef); }
IFACEMETHODIMP_(ULONG) CThumbnailProvider::Release() {
    ULONG cRef = InterlockedDecrement(&m_cRef);
    if (0 == cRef) delete this;
    return cRef;
}

IFACEMETHODIMP CThumbnailProvider::Initialize(IStream* pStream, DWORD grfMode) {
    m_spStream = pStream; return S_OK;
}

HRESULT CThumbnailProvider::GetThumbnailImpl(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) {
    struct archive* a = archive_read_new(); if (!a) return E_FAIL;
    archive_read_support_format_all(a); archive_read_support_filter_all(a);
    archive_read_set_seek_callback(a, StreamSeek);
    StreamContext ctx = { m_spStream.Get() }; LARGE_INTEGER liZero = { 0 }; m_spStream->Seek(liZero, 0, NULL);
    if (archive_read_open(a, &ctx, NULL, StreamRead, NULL) != ARCHIVE_OK) { archive_read_free(a); return E_FAIL; }

    struct archive_entry* entry; ULONGLONG start = GetTickCount64(); int scanCount = 0;
    vector<BYTE> bestData; int bestP = 1001, minN = 1000001; long long maxSz = -1;

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (GetTickCount64() - start > SCAN_TIMEOUT_MS || ++scanCount > MAX_SCAN_FILES) break;
        if (archive_entry_filetype(entry) != AE_IFREG) { archive_read_data_skip(a); continue; }

        const char* p = archive_entry_pathname(entry); if (!p) { archive_read_data_skip(a); continue; }
        string s = p; transform(s.begin(), s.end(), s.begin(), ::tolower);
        auto ends = [&](const string& t, const string& e) { return t.size() >= e.size() && t.compare(t.size() - e.size(), e.size(), e) == 0; };
        if (!(ends(s, ".jpg") || ends(s, ".png") || ends(s, ".webp"))) { archive_read_data_skip(a); continue; }

        long long sz = archive_entry_size(entry);
        if (sz < 1024 || sz > MAX_FILE_SIZE || GetDepth(s) > 0) { archive_read_data_skip(a); continue; }

        string fn = s.substr(s.find_last_of("/\\") + 1);
        int pr = GetFilePriority(fn), n = GetFirstNumberFromUtf8(fn);
        bool better = (pr < bestP) || (pr == bestP && (n < minN || (n == minN && sz > maxSz)));

        if (better) {
            vector<BYTE> tmp(sz);
            if (archive_read_data(a, tmp.data(), (size_t)sz) == (la_ssize_t)sz) {
                if (pr == 1 && SUCCEEDED(CreateHBITMAPFromData(tmp, cx, phbmp))) { archive_read_free(a); return S_OK; }
                bestData = move(tmp); bestP = pr; minN = n; maxSz = sz;
            }
        } else archive_read_data_skip(a);
    }
    archive_read_free(a);
    return bestData.empty() ? E_FAIL : CreateHBITMAPFromData(bestData, cx, phbmp);
}

IFACEMETHODIMP CThumbnailProvider::GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) {
    if (!m_spStream || !phbmp || cx == 0) return E_INVALIDARG;
    if (pdwAlpha) *pdwAlpha = WTSAT_ARGB;
    HRESULT hr = E_FAIL, hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    __try { hr = GetThumbnailImpl(cx, phbmp, pdwAlpha); }
    __finally { if (hrCo == S_OK || hrCo == S_FALSE) CoUninitialize(); }
    return hr;
}
