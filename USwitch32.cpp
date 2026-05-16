// USwitch32.cpp : Definit le point d'entree de l'application.
//

#include "framework.h"
#include "USwitch32.h"
// RS232_Advanced_Mono.cpp
HINSTANCE hInst;                                // instance actuelle
WCHAR szTitle[MAX_LOADSTRING];                  // Texte de la barre de titre
WCHAR szWindowClass[MAX_LOADSTRING];            // nom de la classe de fenêtre principale
HWND hComboPorts, hComboBaud, hEditCmd, hBtnSend, hEditOutput;
HWND hChkHex, hChkRTSCTS, hChkDTRDSR, hBtnConnect;
HWND hEditIP, hEditOut;
WNDCLASSEXW wcex;
HANDLE g_hCom = INVALID_HANDLE_VALUE;
HANDLE g_hReadThread = NULL;
HANDLE g_hLogFile = INVALID_HANDLE_VALUE;
volatile bool g_bStopThread = false;
std::wstring g_lastPort;
DWORD g_lastBaud = 9600;
bool g_lastRTSCTS = false;
bool g_lastDTRDSR = false;
bool g_bConnected = false;

void InsereTexteLog(const wchar_t* prefix, const std::wstring& text)
{
    if (g_hLogFile == INVALID_HANDLE_VALUE)
    {
        g_hLogFile = CreateFileW(L"serial_log.txt",GENERIC_WRITE, FILE_SHARE_READ, NULL,OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_hLogFile == INVALID_HANDLE_VALUE)return;
    }
    SetFilePointer(g_hLogFile, 0, NULL, FILE_END);
    std::wstring line = prefix;
    line += L" ";
    line += text;
    line += L"\r\n";
    int len = WideCharToMultiByte(CP_ACP, 0, line.c_str(), -1, NULL, 0, NULL, NULL);
    if (len <= 0) return;
    std::string buf(len - 1, 0);
    WideCharToMultiByte(CP_ACP, 0, line.c_str(), -1, &buf[0], len - 1, NULL, NULL);
    DWORD written = 0;
    WriteFile(g_hLogFile, buf.data(), (DWORD)buf.size(), &written, NULL);
}
std::vector<std::wstring> EnumererCOM()
{
    std::vector<std::wstring> ports;
    for (int i = 1; i <= 32; i++)
    {
        wchar_t name[32];
        wsprintf(name, L"\\\\.\\COM%d", i);
        HANDLE h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE,0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            wchar_t shortName[16];
            wsprintf(shortName, L"COM%d", i);
            ports.push_back(shortName);
            CloseHandle(h);
        }
    }
    return ports;
}
HANDLE OuvrirPortSerie(const std::wstring& port, DWORD baud, bool rtscts, bool dtrdsr)
{
    std::wstring full = L"\\\\.\\" + port;
    HANDLE h = CreateFileW(full.c_str(),GENERIC_READ | GENERIC_WRITE,0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)return INVALID_HANDLE_VALUE;
    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)){CloseHandle(h);return INVALID_HANDLE_VALUE;}
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fOutxCtsFlow = rtscts ? TRUE : FALSE;
    dcb.fOutxDsrFlow = dtrdsr ? TRUE : FALSE;
    dcb.fRtsControl = rtscts ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_ENABLE;
    dcb.fDtrControl = dtrdsr ? DTR_CONTROL_HANDSHAKE : DTR_CONTROL_ENABLE;
    if (!SetCommState(h, &dcb)){CloseHandle(h);return INVALID_HANDLE_VALUE;}
    COMMTIMEOUTS t = { 0 };
    t.ReadIntervalTimeout = 50;
    t.ReadTotalTimeoutConstant = 100;
    t.WriteTotalTimeoutConstant = 100;
    SetCommTimeouts(h, &t);
    return h;
}
void AjouterAuControleTexte(HWND hEdit, const std::wstring& text)
{
    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
}
void EnumererPortsCOM()
{
    auto ports = EnumererCOM();
    for (auto& p : ports)SendMessageW(hComboPorts, CB_ADDSTRING, 0, (LPARAM)p.c_str());
    if (!ports.empty())SendMessageW(hComboPorts, CB_SETCURSEL, 0, 0);
}
void AjouterValeursVitesseCombo()
{
    const int rates[] = { 9600, 19200, 38400, 57600, 115200 };
    for (int r : rates)
    {
        wchar_t s[32];
        wsprintf(s, L"%d", r);
        SendMessageW(hComboBaud, CB_ADDSTRING, 0, (LPARAM)s);
    }
    SendMessageW(hComboBaud, CB_SETCURSEL, 0, 0);
}
DWORD RecupererValeurVitesseChoisie()
{
    int sel = (int)SendMessageW(hComboBaud, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return 9600;
    wchar_t s[32];
    SendMessageW(hComboBaud, CB_GETLBTEXT, sel, (LPARAM)s);
    return (DWORD)_wtoi(s);
}
std::wstring RecupererCOMChoisi()
{
    int sel = (int)SendMessageW(hComboPorts, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return L"";
    wchar_t s[32];
    SendMessageW(hComboPorts, CB_GETLBTEXT, sel, (LPARAM)s);
    return s;
}
void EnvoyerInstruction()
{
    if (g_hCom == INVALID_HANDLE_VALUE)return;
    wchar_t cmdW[512];
    GetWindowTextW(hEditCmd, cmdW, 512);
    int len = WideCharToMultiByte(CP_ACP, 0, cmdW, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return;
    std::string cmd(len - 1, 0);
    WideCharToMultiByte(CP_ACP, 0, cmdW, -1, &cmd[0], len - 1, NULL, NULL);
    cmd += "\r\n";
    DWORD written = 0;
    WriteFile(g_hCom, cmd.data(), (DWORD)cmd.size(), &written, NULL);
    std::wstring wcmd(cmdW);
    InsereTexteLog(L"[TX]", wcmd);
}
DWORD WINAPI ReadThreadProc(LPVOID lpParam)
{
    HWND hWnd = (HWND)lpParam;
    BYTE buf[256];
    DWORD read = 0;
    while (!g_bStopThread)
    {
        if (g_hCom == INVALID_HANDLE_VALUE)
        {
            if (!g_lastPort.empty())
            {
                HANDLE h = OuvrirPortSerie(g_lastPort, g_lastBaud, g_lastRTSCTS, g_lastDTRDSR);
                if (h != INVALID_HANDLE_VALUE)
                {
                    g_hCom = h;
                    InsereTexteLog(L"[INFO]", L"Auto-reconnect OK");
                }
                else
                {
                    InsereTexteLog(L"[WARN]", L"Auto-reconnect echec, nouvelle tentative dans 1s");
                    Sleep(1000);
                    continue;
                }
            }
            else
            {
                Sleep(200);
                continue;
            }
        }
        if (!ReadFile(g_hCom, buf, sizeof(buf), &read, NULL))
        {
            DWORD err = GetLastError();
            std::wstring msg = L"ReadFile erreur: " + std::to_wstring(err);
            InsereTexteLog(L"[ERR]", msg);
            CloseHandle(g_hCom);
            g_hCom = INVALID_HANDLE_VALUE;
            g_bConnected = false;
            InvalidateRect(hWnd, NULL, FALSE); // LED rouge
            Sleep(500);
            continue;
        }
        if (read > 0)
        {
            BYTE* data = (BYTE*)HeapAlloc(GetProcessHeap(), 0, read);
            if (!data) continue;
            CopyMemory(data, buf, read);
            PostMessageW(hWnd, WM_APPEND_TEXT, (WPARAM)data, (LPARAM)read);
        }
        else{Sleep(20);}
    }
    return 0;
}
void DeconnexionRS232()
{
    g_bStopThread = true;
    if (g_hReadThread)
    {
        WaitForSingleObject(g_hReadThread, 2000);
        CloseHandle(g_hReadThread);
        g_hReadThread = NULL;
    }
    if (g_hCom != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hCom);
        g_hCom = INVALID_HANDLE_VALUE;
    }
    g_bConnected = false;
    if (hBtnConnect) SetWindowTextW(hBtnConnect, L"Connecter");
    if (hBtnSend)    EnableWindow(hBtnSend, FALSE);
    InsereTexteLog(L"[INFO]", L"Port deconnecte");
}
void ConnexionRS232(HWND hWnd)
{
    if (g_bConnected)return;
    std::wstring port = RecupererCOMChoisi();
    if (port.empty()){MessageBoxW(hWnd, L"Aucun port selectionne.", L"Erreur", MB_ICONERROR);return;}
    DWORD baud = RecupererValeurVitesseChoisie();
    BOOL rtscts = (SendMessageW(hChkRTSCTS, BM_GETCHECK, 0, 0) == BST_CHECKED);
    BOOL dtrdsr = (SendMessageW(hChkDTRDSR, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_lastPort = port;
    g_lastBaud = baud;
    g_lastRTSCTS = (rtscts == TRUE);
    g_lastDTRDSR = (dtrdsr == TRUE);
    g_hCom = OuvrirPortSerie(port, baud, g_lastRTSCTS, g_lastDTRDSR);
    if (g_hCom == INVALID_HANDLE_VALUE){MessageBoxW(hWnd, L"Impossible d'ouvrir le port.", L"Erreur", MB_ICONERROR);return;}
    g_bStopThread = false;
    g_hReadThread = CreateThread(NULL, 0, ReadThreadProc, hWnd, 0, NULL);
    g_bConnected = true;
    SetWindowTextW(hBtnConnect, L"Deconnecter");
    EnableWindow(hBtnSend, TRUE);
    InsereTexteLog(L"[INFO]", L"Port connecte");
    InvalidateRect(hWnd, NULL, FALSE); 
}
char* IPversBinaire(HWND source,HWND cible)
{
    wchar_t ipText[64];
    GetWindowText(source, ipText, 64);
    int parts[4];
    if (!IPv4Parse(ipText, parts)) { 
		MessageBox(GetDesktopWindow(),ipText, L"Adresse IP invalide. ", MB_ICONERROR);
        return nullptr; 
    }
    std::wstring bin = IntVersBin8bits(parts[0]) + L"." + IntVersBin8bits(parts[1]) + L"." + IntVersBin8bits(parts[2]) + L"." + IntVersBin8bits(parts[3]);
    SetWindowText(cible, bin.c_str());
    return nullptr;
}
void AfficherVoyantStatutConnexion(HDC hdc, RECT& rc)
{
    int radius = 8;
    int margin = 10;
    int x = rc.right - margin - radius * 2;
    int y = margin;
    HBRUSH hBrush = CreateSolidBrush(g_bConnected ? RGB(0, 200, 0) : RGB(200, 0, 0));
    HBRUSH hOld = (HBRUSH)SelectObject(hdc, hBrush);
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    Ellipse(hdc, x, y, x + radius * 2, y + radius * 2);
    SelectObject(hdc, hOld);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}
bool IPv4Parse(const wchar_t* ip, int out[4])
{
    int idx = 0;
    std::wstring part;
    for (int i = 0; ; i++)
    {
        wchar_t c = ip[i];
        if (c == L'.' || c == 0)
        {
            if (part.empty()) return false;
            if (idx >= 4) return false;
            if (part.size() > 1 && part[0] == L'0')return false;
            int v = 0;
            for (wchar_t d : part)
            {
                if (d < L'0' || d > L'9') return false;
                v = v * 10 + (d - L'0');
            }
            if (v < 0 || v > 255) return false;
            out[idx++] = v;
            part.clear();
            if (c == 0) break;
        }
        else
        {
            part += c;
        }
    }
    return (idx == 4);
}
std::wstring IntVersBin8bits(int v)
{
    wchar_t buf[9];
    for (int i = 7; i >= 0; i--)
    {
        buf[i] = (v & 1) ? L'1' : L'0';
        v >>= 1;
    }
    buf[8] = 0;
    return buf;
}
unsigned long IPVersIntNonSigne32bits(const int p[4])
{
    return ((unsigned long)p[0] << 24) |
           ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8)  |
           ((unsigned long)p[3]);
}
int MasqueEnCID(const int p[4])
{
    unsigned long m = IPVersIntNonSigne32bits(p);
    int count = 0;
    while (m & 0x80000000){count++;m <<= 1;}
    return count;
}
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
	hInst = hInstance;  
    LoadStringW(hInst, IDS_APP_TITLE, szTitle, 80);
    LoadStringW(hInst, IDC_USWITCH32, szWindowClass, 80);
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = Principale;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CONSOLE));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(CTLCOLOR_DLG + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_USWITCH32);
    wcex.lpszClassName = szWindowClass;
 //   wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_AUTHEUR));
    RegisterClassExW(&wcex);    
    AllocConsole();
    FILE* fOut;
    freopen_s(&fOut, "CONOUT$", "w", stdout);
    printf("RS232 Win32 avance - console active\n");
    SetConsoleTitle(szTitle);
    HWND hWnd = CreateWindowExW(WS_EX_CLIENTEDGE|WS_EX_DLGMODALFRAME|WS_EX_CONTEXTHELP, szWindowClass,szTitle, WS_OVERLAPPED | WS_CAPTION |WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,640, 480, NULL, NULL, hInst, NULL);
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    printf("Fermeture.\n");
    return 0;
}
LRESULT CALLBACK Principale(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        hComboPorts = CreateWindowW(L"COMBOBOX", NULL,WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,10, 30, 120, 200, hWnd, (HMENU)100, NULL, NULL);
        hComboBaud = CreateWindowW(L"COMBOBOX", NULL,WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,140, 30, 100, 200, hWnd, (HMENU)101, NULL, NULL);
        hChkHex = CreateWindowW(L"BUTTON", L"Hex",WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,250, 10, 60, 20, hWnd, (HMENU)102, NULL, NULL);
        hChkRTSCTS = CreateWindowW(L"BUTTON", L"RTS/CTS",WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,320, 10, 80, 20, hWnd, (HMENU)103, NULL, NULL);
        hChkDTRDSR = CreateWindowW(L"BUTTON", L"DTR/DSR",WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,410, 10, 80, 20, hWnd, (HMENU)104, NULL, NULL);
        hBtnConnect = CreateWindowW(L"BUTTON", L"Connecter",WS_CHILD | WS_VISIBLE,280, 30, 110, 24, hWnd, (HMENU)500, NULL, NULL);
        hBtnSend = CreateWindowW(L"BUTTON", L"Envoyer", WS_CHILD | WS_VISIBLE | WS_DISABLED, 410, 30, 90, 24, hWnd, (HMENU)201, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Assistant", WS_CHILD | WS_VISIBLE , 500, 30, 90, 24, hWnd, (HMENU)IDM_WIZZARD, NULL, NULL);
        hEditCmd = CreateWindowW(L"EDIT", L"show config",WS_CHILD | WS_VISIBLE | WS_BORDER,10, 75, 360, 24, hWnd, (HMENU)200, NULL, NULL);
        hEditOutput = CreateWindowW(L"EDIT", L"",WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |ES_AUTOVSCROLL | WS_VSCROLL,10, 110, 480, 270, hWnd, (HMENU)300, NULL, NULL);
        EnumererPortsCOM();
        AjouterValeursVitesseCombo();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_WIZZARD:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_WIZZARD), hWnd, Assistant);
            break;
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, Apropos);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        case 500: // Connecter / Deconnecter
            if (!g_bConnected)ConnexionRS232(hWnd);
            else DeconnexionRS232();
            return 0;
        case 201: // Envoyer
            if (!g_bConnected)return 0;
            EnvoyerInstruction();
            return 0;
        }
        return 0;
    case WM_APPEND_TEXT:
    {
        BYTE* data = (BYTE*)wParam;
        DWORD size = (DWORD)lParam;
        if (!data || size == 0) return 0;
        BOOL hex = (SendMessageW(hChkHex, BM_GETCHECK, 0, 0) == BST_CHECKED);
        std::wstring out;
        if (hex)
        {
            wchar_t tmp[8];
            for (DWORD i = 0; i < size; ++i)
            {
                wsprintf(tmp, L"%02X ", data[i]);
                out += tmp;
            }
            out += L"\r\n";
        }
        else
        {
            int len = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)data, size, NULL, 0);
            if (len > 0)
            {
                out.resize(len);
                MultiByteToWideChar(CP_ACP, 0, (LPCSTR)data, size, &out[0], len);
            }
        }
        AjouterAuControleTexte(hEditOutput, out);
        InsereTexteLog(L"[RX]", out);
        HeapFree(GetProcessHeap(), 0, data);
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        AfficherVoyantStatutConnexion(hdc, rc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        DeconnexionRS232();
        if (g_hLogFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_hLogFile);
            g_hLogFile = INVALID_HANDLE_VALUE;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
INT_PTR CALLBACK Apropos(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
INT_PTR CALLBACK Assistant(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG: 
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BUTTON1:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_CALCULATEUR_IP), hDlg, CalculateurIP);
            break;
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hDlg, Apropos);
            break;
        case IDCANCEL:{
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
            }
        }
        return 0;

    }
    return (INT_PTR)FALSE;
}
INT_PTR CALLBACK CalculateurIP(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
    {
        CreateWindow(L"STATIC", L"Adresse IP :", WS_CHILD | WS_VISIBLE, 10, 10, 100, 20, hDlg, NULL, NULL, NULL);
        hEditIP = CreateWindow(L"EDIT", L"192.168.0.10", WS_CHILD | WS_VISIBLE | WS_BORDER, 110, 10, 150, 22, hDlg, NULL, NULL, NULL);
        CreateWindow(L"BUTTON", L"Convertir", WS_CHILD | WS_VISIBLE, 270, 10, 100, 22, hDlg, (HMENU)300, NULL, NULL);
        hEditOut = CreateWindow(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER /* | ES_MULTILINE*/ | ES_AUTOVSCROLL | WS_VSCROLL, 10, 50, 360, 100, hDlg, NULL, NULL, NULL);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case 300: {
			SendMessage(hEditOut, LB_RESETCONTENT, 0, 0);
            wchar_t ipText[64];
            GetWindowText(hEditIP, ipText, 64);
            int parts[4];
            if (!IPv4Parse(ipText, parts)) { SetWindowTextW(hEditOut, L"IP invalide"); return (INT_PTR)FALSE; }
            std::wstring bin = IntVersBin8bits(parts[0]) + L"." + IntVersBin8bits(parts[1]) + L"." + IntVersBin8bits(parts[2]) + L"." + IntVersBin8bits(parts[3]);
            unsigned long ip32 = IPVersIntNonSigne32bits(parts);
            int cidr = MasqueEnCID(parts);
            wchar_t out[256];
			SendMessage(hEditOut, LB_ADDSTRING, 0, (LPARAM)L"IP valide");
			wsprintf(out, L"Décimal : %d.%d.%d.%d", parts[0], parts[1], parts[2], parts[3]);
			SendMessage(hEditOut, LB_ADDSTRING, 0, (LPARAM)out);
			wsprintf(out, L"Binaire : %s", bin.c_str());
			SendMessage(hEditOut, LB_ADDSTRING, 0, (LPARAM)out);
			wsprintf(out, L"Uint32  : %lu", ip32);
			SendMessage(hEditOut, LB_ADDSTRING, 0, (LPARAM)out);
			wsprintf(out, L"CIDR    : /%d", cidr);
            SendMessage(hEditOut, LB_ADDSTRING, 0, (LPARAM)out);
        }
            break;
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hDlg, Apropos);
            break;
        case IDCANCEL: {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        }
        return 0;


    }
    return (INT_PTR)FALSE;
}
