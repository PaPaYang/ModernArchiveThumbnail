#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h> // [추가됨] 탐색기 새로고침(SHChangeNotify) 기능을 위한 헤더
#include <new>
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

// 안전한 레지스트리 등록 도우미 함수
HRESULT SetHKCRRegistryKeyAndValue(PCWSTR pszSubKey, PCWSTR pszValueName, PCWSTR pszData) {
    HKEY hKey = NULL;
    HRESULT hr = HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, pszSubKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL));
    if (SUCCEEDED(hr)) {
        if (pszData != NULL) {
            // [수정됨] 널 종료 문자(+1)를 포함하여 안전하게 레지스트리에 기록하도록 수정
            DWORD cbData = (lstrlenW(pszData) + 1) * sizeof(*pszData);
            hr = HRESULT_FROM_WIN32(RegSetValueExW(hKey, pszValueName, 0, REG_SZ, (const BYTE*)pszData, cbData));
        }
        RegCloseKey(hKey);
    }
    return hr;
}

// 설치 시(regsvr32) 탐색기에 썸네일러 등록
STDAPI DllRegisterServer(void) {
    WCHAR szModule[MAX_PATH];
    if (GetModuleFileNameW(g_hInst, szModule, ARRAYSIZE(szModule)) == 0) return HRESULT_FROM_WIN32(GetLastError());

    // 1. COM 객체 등록
    SetHKCRRegistryKeyAndValue(L"CLSID\\{E5D74646-B8A3-E066-8345-603E2B1637A3}", NULL, L"ModernArchiveThumbnail");
    SetHKCRRegistryKeyAndValue(L"CLSID\\{E5D74646-B8A3-E066-8345-603E2B1637A3}\\InprocServer32", NULL, szModule);
    SetHKCRRegistryKeyAndValue(L"CLSID\\{E5D74646-B8A3-E066-8345-603E2B1637A3}\\InprocServer32", L"ThreadingModel", L"Apartment");

    // 2. 압축 파일 확장자에 썸네일 기능만 연결 (기본 연결 프로그램 유지)
    const WCHAR* exts[] = { L".zip", L".rar", L".7z", L".cbz", L".cbr", L".tar", L".gz", L".bz2", L".lzma", L".zstd" };
    for (int i = 0; i < ARRAYSIZE(exts); i++) {
        WCHAR szKey[256];
        wsprintfW(szKey, L"%s\\shellex\\{e357fccd-a995-4576-b01f-234630154e96}", exts[i]);
        SetHKCRRegistryKeyAndValue(szKey, NULL, L"{E5D74646-B8A3-E066-8345-603E2B1637A3}");
    }
    
    // 탐색기 아이콘 새로고침
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return S_OK;
}

// 삭제 시 등록 해제
STDAPI DllUnregisterServer(void) {
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"CLSID\\{E5D74646-B8A3-E066-8345-603E2B1637A3}");
    const WCHAR* exts[] = { L".zip", L".rar", L".7z", L".cbz", L".cbr", L".tar", L".gz", L".bz2", L".lzma", L".zstd" };
    for (int i = 0; i < ARRAYSIZE(exts); i++) {
        WCHAR szKey[256];
        wsprintfW(szKey, L"%s\\shellex\\{e357fccd-a995-4576-b01f-234630154e96}", exts[i]);
        RegDeleteTreeW(HKEY_CLASSES_ROOT, szKey);
    }
    
    // 탐색기 아이콘 새로고침
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return S_OK;
}
