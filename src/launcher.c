// th12_hfr.exe : launches th12.exe / th12e.exe with th12_hfr.dll injected.
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void die(const char* msg) { MessageBoxA(NULL, msg, "th12_hfr launcher", MB_ICONERROR); ExitProcess(1); }

int main(int argc, char** argv) {
    char dir[MAX_PATH], exe[MAX_PATH], dll[MAX_PATH], ini[MAX_PATH], target[64] = "";
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char* p = strrchr(dir, '\\'); if (p) *p = 0;
    snprintf(ini, MAX_PATH, "%s\\th12_hfr.ini", dir);
    snprintf(dll, MAX_PATH, "%s\\th12_hfr.dll", dir);
    if (argc > 1) strncpy(target, argv[1], sizeof(target)-1);
    else GetPrivateProfileStringA("launcher", "exe", "", target, sizeof(target), ini);
    if (!target[0]) {
        snprintf(exe, MAX_PATH, "%s\\th12e.exe", dir);
        if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) strcpy(target, "th12.exe"); else strcpy(target, "th12e.exe");
    }
    snprintf(exe, MAX_PATH, "%s\\%s", dir, target);
    if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) die("Game executable not found next to the launcher (th12.exe / th12e.exe).");
    if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES) die("th12_hfr.dll not found next to the launcher.");

    STARTUPINFOA si = { sizeof(si) }; PROCESS_INFORMATION pi;
    char cmd[MAX_PATH + 4]; snprintf(cmd, sizeof(cmd), "\"%s\"", exe);
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, dir, &si, &pi)) die("CreateProcess failed.");

    SIZE_T len = strlen(dll) + 1;
    void* rem = VirtualAllocEx(pi.hProcess, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!rem || !WriteProcessMemory(pi.hProcess, rem, dll, len, NULL)) { TerminateProcess(pi.hProcess, 1); die("Failed to write into the game process."); }
    HANDLE th = CreateRemoteThread(pi.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"), rem, 0, NULL);
    if (!th) { TerminateProcess(pi.hProcess, 1); die("CreateRemoteThread failed."); }
    WaitForSingleObject(th, 10000);
    DWORD rc = 0; GetExitCodeThread(th, &rc);
    CloseHandle(th);
    VirtualFreeEx(pi.hProcess, rem, 0, MEM_RELEASE);
    if (rc == 0) { TerminateProcess(pi.hProcess, 1); die("th12_hfr.dll failed to load inside the game (see th12_hfr.log)."); }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return 0;
}
