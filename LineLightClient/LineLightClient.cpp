#include <windows.h>
#include <windowsx.h>
#include <process.h>
#include <CommCtrl.h>
#include <tchar.h>

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <format>

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define MAX_EDIT_STRING 255
#define IDC_BUTTON 1001
#define WMU_THREAD_ERROR (WM_USER + 0x0001)
#define WMU_THREAD_CONNECTED (WM_USER + 0x0002)
#define WMU_THREAD_DISCONNECTED (WM_USER + 0x0003)

typedef struct
{
    HWND hWnd;
    HWND hWndEdit;
    HWND hWndButton;
    HWND hWndStatic;
    HWND hWndDrawArea;
    CRITICAL_SECTION csBitBltBuff;
    HANDLE hEventConnect;
    LONG lAbortRequest;
    HANDLE hThread;
    unsigned uThreadId;
    LONG_PTR pPrevUserData;
} USERDATA;

LPCTSTR const szTitle = TEXT("LineLight Client");
LPCTSTR const szWindowClass = TEXT("LineLightClient_WndClass");

unsigned _stdcall ThreadFunc(void *pObj)
{
    auto pUserData = reinterpret_cast<USERDATA *>(pObj);
    unsigned uRet = 0;

    TCHAR szPort[MAX_EDIT_STRING] = {NULL};
    TCHAR szBuff[MAX_EDIT_STRING] = {NULL};

    HANDLE hComm = INVALID_HANDLE_VALUE;
    DCB dcb = {NULL};
    dcb.BaudRate = 921600;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    COMMTIMEOUTS commTimeouts = {NULL};
    commTimeouts.ReadIntervalTimeout;
    commTimeouts.ReadIntervalTimeout = 500;
    commTimeouts.ReadTotalTimeoutMultiplier = 0;
    commTimeouts.ReadTotalTimeoutConstant = 500;
    commTimeouts.WriteTotalTimeoutMultiplier = 0;
    commTimeouts.WriteTotalTimeoutConstant = 0;

    BYTE bReadBuff[1024] = {NULL};
    DWORD dwReadBytes = 0;
    BOOL fWaitingOnRead = FALSE;
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    DWORD dwCurrentRow = 0;
    while (!InterlockedExchange(&pUserData->lAbortRequest, FALSE))
    {
        if (WaitForSingleObject(pUserData->hEventConnect, 100) == WAIT_TIMEOUT)
        {
            continue;
        }

        if (GetWindowText(pUserData->hWndEdit, szBuff, MAX_EDIT_STRING) == 0)
        {
            SendMessage(pUserData->hWnd, WMU_THREAD_ERROR, 0, 0);
            continue;
        }
        _stprintf_s(szPort, TEXT("\\\\.\\%s"), szBuff);
        hComm = CreateFile(szPort, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (hComm == INVALID_HANDLE_VALUE ||
            !SetCommState(hComm, &dcb) ||
            !SetCommTimeouts(hComm, &commTimeouts))
        {
            if (hComm != INVALID_HANDLE_VALUE)
            {
                CloseHandle(hComm);
            }
            SendMessage(pUserData->hWnd, WMU_THREAD_ERROR, 0, 0);
            continue;
        }
        SendMessage(pUserData->hWnd, WMU_THREAD_CONNECTED, 0, 0);

        while (!InterlockedExchange(&pUserData->lAbortRequest, FALSE))
        {
            if (WaitForSingleObject(pUserData->hEventConnect, 0) == WAIT_OBJECT_0)
            {
                break;
            }
            if (fWaitingOnRead)
            {
                if (WaitForSingleObject(overlapped.hEvent, 100) == WAIT_OBJECT_0 &&
                    GetOverlappedResult(hComm, &overlapped, &dwReadBytes, FALSE))
                {
                    fWaitingOnRead = FALSE;
                }
            }
            else
            {
                if (!ReadFile(hComm, bReadBuff, 1024, &dwReadBytes, &overlapped))
                {
                    if (GetLastError() == ERROR_IO_PENDING)
                    {
                        fWaitingOnRead = TRUE;
                    }
                    else
                    {
                        break;
                    }
                }
            }
            if (!fWaitingOnRead)
            {
                dwReadBytes = min(1023, dwReadBytes);
                bReadBuff[dwReadBytes] = 0;
                OutputDebugStringA(reinterpret_cast<LPCSTR>(bReadBuff));
            }
        }
        CloseHandle(hComm);
        hComm = INVALID_HANDLE_VALUE;
        SendMessage(pUserData->hWnd, WMU_THREAD_DISCONNECTED, 0, 0);
    }
    OutputDebugString(TEXT("ThreadFunc() Aborted\r\n"));

    _endthreadex(uRet);
    return uRet;
}
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto pUserData = reinterpret_cast<USERDATA *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CTLCOLORSTATIC:
        return (INT_PTR)GetStockBrush(WHITE_BRUSH);
    case WM_CREATE:
    {
        auto lpCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
        pUserData = reinterpret_cast<USERDATA *>(lpCreateStruct->lpCreateParams);
        pUserData->pPrevUserData = SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pUserData));
        pUserData->hWnd = hWnd;

        auto hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(hWnd, GWLP_HINSTANCE));
        pUserData->hWndEdit = CreateWindowEx(
            0,
            TEXT("EDIT"),
            NULL,
            WS_BORDER | WS_CHILD | WS_VISIBLE | ES_LEFT,
            10, 10, 100, 25,
            hWnd, GetMenu(hWnd), NULL, NULL);
        SendMessage(pUserData->hWndEdit, EM_SETLIMITTEXT, MAX_EDIT_STRING - 1, 0);
        RECT rcEdit = {0};
        GetClientRect(pUserData->hWndEdit, &rcEdit);
        MapWindowRect(pUserData->hWndEdit, hWnd, &rcEdit);
        pUserData->hWndButton = CreateWindowEx(
            0,
            TEXT("BUTTON"),
            TEXT("Connect"),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            rcEdit.right + 10, rcEdit.top, rcEdit.right - rcEdit.left, rcEdit.bottom - rcEdit.top,
            hWnd, reinterpret_cast<HMENU>(IDC_BUTTON), hInst, NULL);
        pUserData->hWndStatic = CreateWindowEx(
            0,
            TEXT("STATIC"),
            TEXT(""),
            WS_CHILD | WS_VISIBLE,
            rcEdit.left, rcEdit.bottom + 10, 200, rcEdit.bottom - rcEdit.top,
            hWnd, GetMenu(hWnd), hInst, NULL);
        RECT rcStatic = {0};
        RECT rcWnd = {0};
        GetClientRect(pUserData->hWndStatic, &rcStatic);
        MapWindowRect(pUserData->hWndStatic, hWnd, &rcStatic);
        GetClientRect(hWnd, &rcWnd);
        pUserData->hWndDrawArea = CreateWindowEx(
            0,
            TEXT("STATIC"),
            TEXT(""),
            WS_BORDER | WS_CHILD | WS_VISIBLE,
            10, rcStatic.bottom + 10, rcWnd.right - 20, rcWnd.bottom - rcStatic.bottom - 20,
            hWnd, GetMenu(hWnd), hInst, NULL);

        pUserData->hEventConnect = CreateEvent(NULL, FALSE, FALSE, TEXT("Connect_Event"));
        pUserData->hThread = reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, ThreadFunc, reinterpret_cast<LPVOID>(pUserData), 0, &pUserData->uThreadId));
    }
    break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
    }
    break;
    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDC_BUTTON && pUserData != NULL)
        {
            SetEvent(pUserData->hEventConnect);
            EnableWindow(pUserData->hWndEdit, FALSE);
            EnableWindow(pUserData->hWndButton, FALSE);
        }
    }
    break;
    case WMU_THREAD_ERROR:
    case WMU_THREAD_CONNECTED:
    case WMU_THREAD_DISCONNECTED:
    {
        if (pUserData != NULL)
        {
            EnableWindow(pUserData->hWndEdit, TRUE);
            EnableWindow(pUserData->hWndButton, TRUE);

            switch (message)
            {
            case WMU_THREAD_ERROR:
                SetWindowText(pUserData->hWndButton, TEXT("Connect"));
                SetWindowText(pUserData->hWndStatic, TEXT("Error"));
                break;
            case WMU_THREAD_CONNECTED:
                SetWindowText(pUserData->hWndButton, TEXT("Disconnect"));
                SetWindowText(pUserData->hWndStatic, TEXT("Connected"));
                break;
            case WMU_THREAD_DISCONNECTED:
                SetWindowText(pUserData->hWndButton, TEXT("Connect"));
                SetWindowText(pUserData->hWndStatic, TEXT("Disconnected"));
                break;
            }
        }
    }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
                       _In_opt_ HINSTANCE hPrevInstance,
                       _In_ LPTSTR lpCmdLine,
                       _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    USERDATA *pUserData = new USERDATA();
    InitializeCriticalSection(&pUserData->csBitBltBuff);

    WNDCLASSEX wcex = {NULL};
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = NULL;
    wcex.hCursor = NULL;
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = NULL;

    RegisterClassEx(&wcex);

    HWND hWnd = CreateWindowEx(
        0,
        szWindowClass,
        szTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 500,
        nullptr, nullptr, hInstance, reinterpret_cast<LPVOID>(pUserData));

    if (!hWnd)
    {
        return FALSE;
    }
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg = {NULL};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteCriticalSection(&pUserData->csBitBltBuff);
    delete pUserData;

    return (int)msg.wParam;
}
