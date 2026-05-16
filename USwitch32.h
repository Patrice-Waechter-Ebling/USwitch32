#pragma once

int __stdcall wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int);
INT_PTR CALLBACK Apropos(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK Assistant(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK Principale(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK CalculateurIP(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void InsereTexteLog(const wchar_t* prefix, const std::wstring& text);
std::vector<std::wstring> EnumererCOM();
HANDLE OuvrirPortSerie(const std::wstring& port, DWORD baud, bool rtscts, bool dtrdsr);
void AjouterAuControleTexte(HWND hEdit, const std::wstring& text);
void EnumererPortsCOM();
void AjouterValeursVitesseCombo();
DWORD RecupererValeurVitesseChoisie();
std::wstring RecupererCOMChoisi();
void EnvoyerInstruction();
DWORD WINAPI ReadThreadProc(LPVOID lpParam);
void DeconnexionRS232();
void ConnexionRS232(HWND hWnd);
void AfficherVoyantStatutConnexion(HDC hdc, RECT& rc);
bool IPv4Parse(const wchar_t* ip, int out[4]);
std::wstring IntVersBin8bits(int v);
unsigned long IPVersIntNonSigne32bits(const int p[4]);
int MasqueEnCID(const int p[4]);
