#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <thumbcache.h>
#include <archive.h>
#include <archive_entry.h>
#include <string>
#include <vector>
#include <algorithm>
#include <new>
#include "ThumbnailProvider.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

using namespace std;

const double MIN_ASPECT_RATIO = 0.15;
const double MAX_ASPECT_RATIO = 4.5;
const long long MAX_FILE_SIZE = 40 * 1024 * 1024;
const int SCAN_TIMEOUT_MS = 3000;
const int MAX_SCAN_FILES = 200;

template <typename T>
class ThumbnailRawPtr {
    T* ptr = nullptr;
public:
    ThumbnailRawPtr() {}
    ThumbnailRawPtr(T* p) : ptr(p) {}
    ~ThumbnailRawPtr() { if (ptr) ptr->Release(); }
    ThumbnailRawPtr(ThumbnailRawPtr&& o) noexcept : ptr(o.ptr) { o.ptr = nullptr; }
    ThumbnailRawPtr& operator=(ThumbnailRawPtr&& o) noexcept {
        if (this != &o) { if (ptr) ptr->Release(); ptr = o.ptr; o.ptr = nullptr; }
        return *this;
    }
    ThumbnailRawPtr(const ThumbnailRawPtr&) = delete;
    ThumbnailRawPtr& operator=(const ThumbnailRawPtr&) = delete;
    T* operator->() const { return ptr; }
    T* Get() const { return ptr; }
    T** operator&() { if (ptr) { ptr->Release(); ptr = nullptr; } return &ptr; }
    operator bool() const { return ptr != nullptr; }
};

static INIT_ONCE g_WicOnce = INIT_ONCE_STATIC_INIT;
static IWICImagingFactory* g_pWICFactory = nullptr;

BOOL CALLBACK InitWicFactory(PINIT_ONCE, PVOID, PVOID*) {
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_pWICFactory)));
}

IWICImagingFactory* GetWIC() {
    if (!InitOnceExecuteOnce(&g_WicOnce, InitWicFactory, NULL, NULL)) return nullptr;
    return g_pWICFactory;
}

int GetFirstNumberFromUtf8(const string& fn) {
    int num = 0; bool found = false;
    for (char c : fn) {
        if (c >= '0' && c <= '9') {
            num = num * 10 + (c - '0');
            found = true;
            if (num > 999999) break;
        }
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
    ThumbnailRawPtr<IStream> spStream(SHCreateMemStream(buf.data(), (UINT)buf.size()));
    if (!spStream) return E_OUTOFMEMORY;

    ThumbnailRawPtr<IWICBitmapDecoder> spDec;
    if (FAILED(GetWIC()->CreateDecoderFromStream(spStream.Get(), NULL, WICDecodeMetadataCacheOnLoad, &spDec))) return E_FAIL;
    ThumbnailRawPtr<IWICBitmapFrameDecode> spFrame;
    if (FAILED(spDec->GetFrame(0, &spFrame)) || !spFrame) return E_FAIL;

    UINT w, h; spFrame->GetSize(&w, &h);
    if (w == 0 || h == 0) return E_FAIL;
    double r = (double)w / h;
    if (r < MIN_ASPECT_RATIO || r > MAX_ASPECT_RATIO) return S_FALSE;

    UINT tw = cx, th = max((UINT)((double)h * cx / w), 1U);
    ThumbnailRawPtr<IWICBitmapScaler> spScaler;
    if (FAILED(GetWIC()->CreateBitmapScaler(&spScaler)) || !spScaler
