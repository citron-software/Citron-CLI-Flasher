#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define ID_BTN_FLASH      1
#define ID_COMBO_DRIVE    2
#define ID_EDIT_LOGS      3
#define ID_PROGRESSBAR    7
#define ID_BTN_BROWSE     8
#define ID_EDIT_ISO       9
#define ID_COMBO_BOOT     10
#define ID_COMBO_TARGET   11
#define ID_LBL_STATUS     12

HWND hEditLogs, hComboDrive, hProgressBar, hBtnFlash, hMainWnd, hEditIso, hComboBoot, hComboTarget, hLblStatus;
HFONT hFontUI, hFontConsolas;
HICON hIconApp;

void LogMessage(const char* message) {
    int len = GetWindowTextLength(hEditLogs);
    SendMessage(hEditLogs, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hEditLogs, EM_REPLACESEL, 0, (LPARAM)(message));
    SendMessage(hEditLogs, EM_REPLACESEL, 0, (LPARAM)"\r\n");
}

void SetProgress(int percent, const char* statusMsg) {
    SendMessage(hProgressBar, PBM_SETPOS, percent, 0);
    if (statusMsg) {
        char statusText[256];
        snprintf(statusText, sizeof(statusText), "%d%% - %s", percent, statusMsg);
        SetWindowText(hLblStatus, statusText);
        LogMessage(statusMsg);
    }
}

DWORD WINAPI FlashThreadProc(LPVOID lpParam) {
    int selIndex = SendMessage(hComboDrive, CB_GETCURSEL, 0, 0);
    if (selIndex == CB_ERR) {
        LogMessage("[-] Erreur : Selectionnez un peripherique cible valide.");
        EnableWindow(hBtnFlash, TRUE);
        return 0;
    }

    char drive[16] = {0};
    char comboText[128];
    SendMessageA(hComboDrive, CB_GETLBTEXT, selIndex, (LPARAM)comboText);

    if (comboText[0] != 'A' && comboText[0] != 'C') {
        drive[0] = comboText[0];
        drive[1] = ':';
        drive[2] = '\0';
    } else {
        LogMessage("[-] Erreur : Peripherique cible invalide.");
        EnableWindow(hBtnFlash, TRUE);
        return 0;
    }

    char isoPath[MAX_PATH];
    GetWindowText(hEditIso, isoPath, MAX_PATH);

    struct stat buffer;
    if (stat(isoPath, &buffer) != 0) {
        LogMessage("[-] Erreur : Le fichier ISO specifie est introuvable.");
        EnableWindow(hBtnFlash, TRUE);
        return 0;
    }

    char* ext = strrchr(isoPath, '.');
    if (!ext || _stricmp(ext, ".iso") != 0) {
        MessageBox(hMainWnd, "Le fichier selectionne n'est pas une image ISO valide.", "Erreur de format", MB_OK | MB_ICONERROR);
        LogMessage("[-] Erreur : Le fichier selectionne n'a pas l'extension .iso.");
        EnableWindow(hBtnFlash, TRUE);
        return 0;
    }

    int response = MessageBox(hMainWnd, "ATTENTION : Toutes les donnees sur cet appareil vont etre effacees !\n\nCliquez sur OK pour formater et continuer, ou Annuler pour arreter.", "Avertissement - Citron CLI Flasher v2.5", MB_OKCANCEL | MB_ICONWARNING);
    if (response == IDCANCEL) {
        LogMessage("[-] Operation annulee par l'utilisateur.");
        EnableWindow(hBtnFlash, TRUE);
        return 0;
    }

    SendMessage(hProgressBar, PBM_SETSTATE, PBST_NORMAL, 0);
    SetProgress(5, "Montage virtuel de l'image disque en arriere-plan...");

    char psMountCmd[1024];
    snprintf(psMountCmd, sizeof(psMountCmd), 
        "powershell -NoProfile -Command \"$mount = Mount-DiskImage -ImagePath '%s' -PassThru; $vol = Get-Volume -DiskImage $mount; $vol.DriveLetter | Out-File -Encoding ASCII temp_drive.txt\"", isoPath);
    
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, psMountCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        EnableWindow(hBtnFlash, TRUE);
        return 0;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    char isoDrive[8] = {0};
    FILE* fDrive = fopen("temp_drive.txt", "r");
    if (fDrive) {
        if (fgets(isoDrive, sizeof(isoDrive), fDrive) != NULL) {
            isoDrive[strcspn(isoDrive, "\r\n")] = 0;
        }
        fclose(fDrive);
        remove("temp_drive.txt");
    }

    if (strlen(isoDrive) == 0) {
        LogMessage("[-] Erreur : Echec du montage virtuel de l'ISO.");
        EnableWindow(hBtnFlash, TRUE);
        SendMessage(hProgressBar, PBM_SETSTATE, PBST_ERROR, 0);
        return 0;
    }

    char srcPath[16];
    snprintf(srcPath, sizeof(srcPath), "%s:\\", isoDrive);

    char pathCitronDir[MAX_PATH];
    char pathCliExe[MAX_PATH];
    char pathConfigDll[MAX_PATH];
    char pathLoginDat[MAX_PATH];
    char pathPasswordDat[MAX_PATH];
    char pathSignCs[MAX_PATH];
    char pathPkgsDir[MAX_PATH];

    snprintf(pathCitronDir, sizeof(pathCitronDir), "%sCitron", srcPath);
    snprintf(pathCliExe, sizeof(pathCliExe), "%sCitron\\citron_cli.exe", srcPath);
    snprintf(pathConfigDll, sizeof(pathConfigDll), "%sCitron\\citron_config.dll", srcPath);
    snprintf(pathLoginDat, sizeof(pathLoginDat), "%sCitron\\login.dat", srcPath);
    snprintf(pathPasswordDat, sizeof(pathPasswordDat), "%sCitron\\password.dat", srcPath);
    snprintf(pathSignCs, sizeof(pathSignCs), "%sCitron\\sign.cs", srcPath);
    snprintf(pathPkgsDir, sizeof(pathPkgsDir), "%sCitron\\citron_pkgs", srcPath);

    struct stat stDir, stExe, stDll, stLogin, stPass, stSign, stPkgs;
    int isValidStructure = (stat(pathCitronDir, &stDir) == 0 &&
                            stat(pathCliExe, &stExe) == 0 &&
                            stat(pathConfigDll, &stDll) == 0 &&
                            stat(pathLoginDat, &stLogin) == 0 &&
                            stat(pathPasswordDat, &stPass) == 0 &&
                            stat(pathSignCs, &stSign) == 0 &&
                            stat(pathPkgsDir, &stPkgs) == 0);

    int isValidSignContent = 0;
    if (isValidStructure) {
        FILE* fSign = fopen(pathSignCs, "r");
        if (fSign) {
            char signContent[512] = {0};
            fread(signContent, 1, sizeof(signContent) - 1, fSign);
            fclose(fSign);

            if (strstr(signContent, "t3XMEftIuJ8tN58A4Hnjt2FwQYCLyrWVnislWLYy") != NULL) {
                isValidSignContent = 1;
            }
        }
    }

    if (!isValidStructure || !isValidSignContent) {
        char unmountCmd[512];
        snprintf(unmountCmd, sizeof(unmountCmd), "powershell -NoProfile -Command \"Dismount-DiskImage -ImagePath '%s'\" >nul 2>&1", isoPath);
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        ZeroMemory(&pi, sizeof(pi));
        if (CreateProcessA(NULL, unmountCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        LogMessage("[-] Erreur : L'ISO ne contient pas la structure Citron valide.");
        MessageBox(hMainWnd, "Erreur : L'ISO selectionnee ne contient pas la structure Citron valide.", "Erreur de structure", MB_OK | MB_ICONERROR);
        
        EnableWindow(hBtnFlash, TRUE);
        SendMessage(hProgressBar, PBM_SETSTATE, PBST_ERROR, 0);
        return 0;
    }

    SetProgress(20, "Verification de l'ISO : Structure et signature validees.");

    SetProgress(30, "Formatage du volume cible en FAT32...");
    char formatCmd[256];
    snprintf(formatCmd, sizeof(formatCmd), "format %s /FS:FAT32 /Q /V:CITRON_CLI /Y >nul 2>&1", drive);
    
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessA(NULL, formatCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    SetProgress(45, "Formatage termine avec succes.");

    SetProgress(65, "Transfert des fichiers en cours...");

    char robocopyCmd[512];
    snprintf(robocopyCmd, sizeof(robocopyCmd), "robocopy %s %s\\ /E /NJH /NJS /R:0 /W:0", srcPath, drive);

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe, hWritePipe;
    CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessA(NULL, robocopyCmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hWritePipe);
        char bufferPipe[512];
        DWORD bytesRead;
        int progressCount = 65;

        while (ReadFile(hReadPipe, bufferPipe, sizeof(bufferPipe) - 1, &bytesRead, NULL) && bytesRead > 0) {
            bufferPipe[bytesRead] = '\0';
            char* line = strtok(bufferPipe, "\r\n");
            while (line != NULL) {
                while (*line == ' ' || *line == '\t') line++;
                if (strlen(line) > 2) {
                    char statusText[256];
                    snprintf(statusText, sizeof(statusText), "Copie : %s", line);
                    
                    if (progressCount < 95) progressCount++;
                    SetProgress(progressCount, statusText);
                    
                    Sleep(80); 
                }
                line = strtok(NULL, "\r\n");
            }
        }
        CloseHandle(hReadPipe);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    char unmountCmd[512];
    snprintf(unmountCmd, sizeof(unmountCmd), "powershell -NoProfile -Command \"Dismount-DiskImage -ImagePath '%s'\" >nul 2>&1", isoPath);
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessA(NULL, unmountCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    SetProgress(100, "Deploiement reussi !");
    SendMessage(hProgressBar, PBM_SETSTATE, PBST_NORMAL, 0);
    MessageBox(hMainWnd, "Le deploiement de Citron CLI sur la cle USB est termine avec succes !", "Succes", MB_OK | MB_ICONINFORMATION);

    EnableWindow(hBtnFlash, TRUE);
    return 0;
}

void ExecuteFlash() {
    EnableWindow(hBtnFlash, FALSE);
    CreateThread(NULL, 0, FlashThreadProc, NULL, 0, NULL);
}

LRESULT CALLBACK WindowProcedure(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            hMainWnd = hWnd;
            hIconApp = (HICON)LoadImage(NULL, "citron.ico", IMAGE_ICON, 32, 32, LR_LOADFROMFILE | LR_SHARED);
            if (hIconApp) {
                SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIconApp);
                SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconApp);
            }

            // Utilisation de la police moderne Segoe UI
            hFontUI = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                               OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
            
            hFontConsolas = CreateFont(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                     OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");

            int xL = 15, xR = 130, wR = 330, y = 15, h = 25;

            HWND hGrp1 = CreateWindow("BUTTON", " Peripherique ", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, xL, y, 465, 68, hWnd, NULL, NULL, NULL);
            SendMessage(hGrp1, WM_SETFONT, (WPARAM)hFontUI, TRUE);

            HWND hLblDrive = CreateWindow("STATIC", "Peripherique :", WS_VISIBLE | WS_CHILD, xL + 15, y + 27, 100, 18, hWnd, NULL, NULL, NULL);
            SendMessage(hLblDrive, WM_SETFONT, (WPARAM)hFontUI, TRUE);

            hComboDrive = CreateWindow("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, xR + 10, y + 25, wR, 150, hWnd, (HMENU)ID_COMBO_DRIVE, NULL, NULL);
            SendMessage(hComboDrive, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            
            char logicalDrives[256];
            DWORD len = GetLogicalDriveStringsA(sizeof(logicalDrives), logicalDrives);
            if (len > 0) {
                char* pDrive = logicalDrives;
                while (*pDrive) {
                    if (GetDriveTypeA(pDrive) == DRIVE_REMOVABLE) {
                        char volName[MAX_PATH + 1] = {0};
                        GetVolumeInformationA(pDrive, volName, sizeof(volName), NULL, NULL, NULL, NULL, 0);
                        char entry[128];
                        if (strlen(volName) > 0) {
                            snprintf(entry, sizeof(entry), "%s (%s) [FAT32]", pDrive, volName);
                        } else {
                            snprintf(entry, sizeof(entry), "%s [FAT32]", pDrive);
                        }
                        int index = SendMessageA(hComboDrive, CB_ADDSTRING, 0, (LPARAM)entry);
                        SendMessageA(hComboDrive, CB_SETITEMDATA, index, (LPARAM)(pDrive[0]));
                    }
                    pDrive += strlen(pDrive) + 1;
                }
            }
            if (SendMessage(hComboDrive, CB_GETCOUNT, 0, 0) > 0) {
                SendMessage(hComboDrive, CB_SETCURSEL, 0, 0);
            } else {
                SendMessageA(hComboDrive, CB_ADDSTRING, 0, (LPARAM)"Aucun lecteur amovible detecte");
                SendMessage(hComboDrive, CB_SETCURSEL, 0, 0);
            }

            y += 78;

            HWND hGrp2 = CreateWindow("BUTTON", " Parametres de demarrage ", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, xL, y, 465, 98, hWnd, NULL, NULL, NULL);
            SendMessage(hGrp2, WM_SETFONT, (WPARAM)hFontUI, TRUE);

            HWND hLblBoot = CreateWindow("STATIC", "Type de boot :", WS_VISIBLE | WS_CHILD, xL + 15, y + 27, 100, 18, hWnd, NULL, NULL, NULL);
            SendMessage(hLblBoot, WM_SETFONT, (WPARAM)hFontUI, TRUE);

            hComboBoot = CreateWindow("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_TABSTOP, xR + 10, y + 25, wR, 100, hWnd, (HMENU)ID_COMBO_BOOT, NULL, NULL);
            SendMessage(hComboBoot, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            SendMessage(hComboBoot, CB_ADDSTRING, 0, (LPARAM)"Image ISO Citron CLI Bootable");
            SendMessage(hComboBoot, CB_SETCURSEL, 0, 0);

            HWND hLblIso = CreateWindow("STATIC", "Fichier ISO :", WS_VISIBLE | WS_CHILD, xL + 15, y + 60, 100, 18, hWnd, NULL, NULL, NULL);
            SendMessage(hLblIso, WM_SETFONT, (WPARAM)hFontUI, TRUE);

            hEditIso = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, xR + 10, y + 58, wR - 85, h, hWnd, (HMENU)ID_EDIT_ISO, NULL, NULL);
            SendMessage(hEditIso, WM_SETFONT, (WPARAM)hFontUI, TRUE);

            HWND hBtnBrowse = CreateWindow("BUTTON", "SELECTION...", WS_VISIBLE | WS_CHILD | WS_TABSTOP, xR + wR - 75, y + 57, 80, 26, hWnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);
            SendMessage(hBtnBrowse, WM_SETFONT, (WPARAM)hFontUI, TRUE);

            y += 108;

            HWND hGrp3 = CreateWindow("BUTTON", " Options avancees ", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, xL, y, 465, 68, hWnd, NULL, NULL, NULL);
            SendMessage(hGrp3, WM_SETFONT, (WPARAM)hFontUI, TRUE);

            HWND hLblTarget = CreateWindow("STATIC", "Type d'amorcage :", WS_VISIBLE | WS_CHILD, xL + 15, y + 29, 110, 18, hWnd, NULL, NULL, NULL);
            SendMessage(hLblTarget, WM_SETFONT, (WPARAM)hFontUI, TRUE);

            hComboTarget = CreateWindow("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_TABSTOP, xR + 10, y + 27, wR, 100, hWnd, (HMENU)ID_COMBO_TARGET, NULL, NULL);
            SendMessage(hComboTarget, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            SendMessage(hComboTarget, CB_ADDSTRING, 0, (LPARAM)"BIOS");
            SendMessage(hComboTarget, CB_ADDSTRING, 0, (LPARAM)"UEFI");
            SendMessage(hComboTarget, CB_ADDSTRING, 0, (LPARAM)"BIOS/UEFI");
            SendMessage(hComboTarget, CB_SETCURSEL, 2, 0);

            y += 78;

            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
            icex.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
            InitCommonControlsEx(&icex);

            hProgressBar = CreateWindowEx(0, PROGRESS_CLASS, "", WS_VISIBLE | WS_CHILD | PBS_SMOOTH, xL, y, 465, 20, hWnd, (HMENU)ID_PROGRESSBAR, NULL, NULL);
            SendMessage(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessage(hProgressBar, PBM_SETPOS, 0, 0);
            SendMessage(hProgressBar, PBM_SETBARCOLOR, 0, (LPARAM)RGB(16, 124, 65));

            y += 24;

            hLblStatus = CreateWindow("STATIC", "Pret.", WS_VISIBLE | WS_CHILD, xL, y, 465, 18, hWnd, (HMENU)ID_LBL_STATUS, NULL, NULL);
            SendMessage(hLblStatus, WM_SETFONT, (WPARAM)hFontUI, TRUE);

            y += 24;

            hBtnFlash = CreateWindow("BUTTON", "DEMARRER", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_TABSTOP, xL, y, 465, 36, hWnd, (HMENU)ID_BTN_FLASH, NULL, NULL);
            SendMessage(hBtnFlash, WM_SETFONT, (WPARAM)hFontUI, TRUE);

            y += 46;

            hEditLogs = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY, xL, y, 465, 95, hWnd, (HMENU)ID_EDIT_LOGS, NULL, NULL);
            SendMessage(hEditLogs, WM_SETFONT, (WPARAM)hFontConsolas, TRUE);

            LogMessage("[*] Citron CLI Flasher v2.5 - Pret.");
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == ID_BTN_BROWSE) {
                char isoPath[MAX_PATH] = {0};
                GetWindowText(hEditIso, isoPath, MAX_PATH);
                OPENFILENAME ofn;
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hMainWnd;
                ofn.lpstrFilter = "Images ISO (*.iso)\0*.iso\0Tous les fichiers (*.*)\0*.*\0";
                ofn.lpstrFile = isoPath;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrTitle = "Selectionner l'image ISO Citron";
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                if (GetOpenFileName(&ofn)) {
                    SetWindowText(hEditIso, isoPath);
                    LogMessage("[*] Image ISO chargee avec succes.");
                }
            }
            else if (id == ID_BTN_FLASH) {
                ExecuteFlash();
            }
            break;
        }

        case WM_DESTROY:
            if (hIconApp) DestroyIcon(hIconApp);
            DeleteObject(hFontUI);
            DeleteObject(hFontConsolas);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, msg, wp, lp);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR args, int ncmdshow) {
    // Activation de la sensibilite DPI pour eviter le flou sur les ecrans Haute Resolution
    HMODULE hUser32 = LoadLibrary("user32.dll");
    if (hUser32) {
        typedef BOOL(WINAPI* SetProcessDpiAwarenessType)(void);
        // Tente d'activer la conscience DPI V2 par defaut si disponible
        HMODULE hShCore = LoadLibrary("shcore.dll");
        if (hShCore) {
            typedef HRESULT(WINAPI* SetProcessDpiAwarenessFunc)(int);
            SetProcessDpiAwarenessFunc pSetProcessDpiAwareness = (SetProcessDpiAwarenessFunc)GetProcAddress(hShCore, "SetProcessDpiAwareness");
            if (pSetProcessDpiAwareness) {
                pSetProcessDpiAwareness(2); // PROCESS_PER_MONITOR_DPI_AWARE
            }
            FreeLibrary(hShCore);
        }
        FreeLibrary(hUser32);
    }

    WNDCLASS wc = {0};
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hInstance = hInst;
    wc.lpszClassName = "CitronRufusClass";
    wc.lpfnWndProc = WindowProcedure;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (!RegisterClass(&wc)) return 1;

    HWND hWnd = CreateWindow("CitronRufusClass", "Citron CLI Flasher v2.5", 
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE, 
                             CW_USEDEFAULT, CW_USEDEFAULT, 510, 560, NULL, NULL, hInst, NULL);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}