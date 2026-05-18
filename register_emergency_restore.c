#include <stdio.h>
#include <windows.h>

void RestoreSecurityOptions(void) {
    HKEY hKey;
    DWORD value = 0;

    // 로그아웃 버튼 복원 (NoLogoff 제거)
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
        0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueA(hKey, "NoLogoff");
        RegCloseKey(hKey);
    }

    // 사용자 전환 버튼 복원 (HideFastUserSwitching = 0)
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
        0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "HideFastUserSwitching", 0, REG_DWORD,
                       (const BYTE*)&value, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    RestoreSecurityOptions();
    MessageBox(NULL,
        "복구 완료!\n\n"
        "- 로그아웃 버튼 복원\n"
        "- 사용자 전환 버튼 복원\n\n"
        "로그아웃 후 다시 로그인하면 완전히 적용됩니다.",
        "Registry Restore", MB_ICONINFORMATION | MB_OK);
    return 0;
}
