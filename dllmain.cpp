#include <windows.h>
#include <shlwapi.h>
#include <new> // 누락되었던 헤더 추가
#include "ClassFactory.h"

// CLSID for ModernArchiveThumbnail: {E5D74646-B8A3-E066-8345-603E2B1637A3}
const CLSID CLSID_ModernArchiveThumbnail = { 0xE5D74646, 0xB8A3, 0xE066, { 0x83, 0x45, 0x60, 0x3E, 0x2B, 0x16, 0x37, 0xA3 } };

long g_cDllRef = 0;
HINSTANCE g_hInst = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) { g_hInst = hModule; DisableThreadLibraryCalls(hModule); }
    return TRUE;
}

STDAPI DllCanUnloadNow(void) { return g_cDllRef == 0 ? S_OK : S_FALSE; }
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!IsEqualCLSID(CLSID_ModernArchiveThumbnail, rclsid)) return CLASS_E_CLASSNOTAVAILABLE;
    CClassFactory* pClassFactory = new (std::nothrow) CClassFactory();
    if (!pClassFactory) return E_OUTOFMEMORY;
    HRESULT hr = pClassFactory->QueryInterface(riid, ppv);
    pClassFactory->Release(); return hr;
}

// NSIS가 등록을 담당하므로 레지스트리 작성 코드는 비워둡니다.
STDAPI DllRegisterServer(void) { return S_OK; }
STDAPI DllUnregisterServer(void) { return S_OK; }