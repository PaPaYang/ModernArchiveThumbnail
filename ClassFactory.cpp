#include "ClassFactory.h"
#include "ThumbnailProvider.h"

extern long g_cDllRef;

CClassFactory::CClassFactory() : m_cRef(1) { InterlockedIncrement(&g_cDllRef); }
CClassFactory::~CClassFactory() { InterlockedDecrement(&g_cDllRef); }

IFACEMETHODIMP CClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IClassFactory) { *ppv = static_cast<IClassFactory*>(this); AddRef(); return S_OK; }
    *ppv = NULL; return E_NOINTERFACE;
}
IFACEMETHODIMP_(ULONG) CClassFactory::AddRef() { return InterlockedIncrement(&m_cRef); }
IFACEMETHODIMP_(ULONG) CClassFactory::Release() {
    ULONG cRef = InterlockedDecrement(&m_cRef); if (0 == cRef) delete this; return cRef;
}
IFACEMETHODIMP CClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    CThumbnailProvider* pExt = new (std::nothrow) CThumbnailProvider();
    if (!pExt) return E_OUTOFMEMORY;
    HRESULT hr = pExt->QueryInterface(riid, ppv);
    pExt->Release(); return hr;
}
IFACEMETHODIMP CClassFactory::LockServer(BOOL fLock) {
    if (fLock) InterlockedIncrement(&g_cDllRef); else InterlockedDecrement(&g_cDllRef); return S_OK;
}