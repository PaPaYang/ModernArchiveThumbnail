#pragma once
#include <windows.h>
#include <thumbcache.h>
#include <wrl/client.h>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

class CThumbnailProvider : public IInitializeWithStream, public IThumbnailProvider {
public:
    CThumbnailProvider();
    ~CThumbnailProvider();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv);
    IFACEMETHODIMP_(ULONG) AddRef();
    IFACEMETHODIMP_(ULONG) Release();

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* pStream, DWORD grfMode);

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha);

private:
    long m_cRef;
    ComPtr<IStream> m_spStream;

    HRESULT GetThumbnailImpl(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha);
};
