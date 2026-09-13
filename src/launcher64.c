/* Architecture helper for the common launcher. Explicit DLL initialization runs
   outside DllMain while the game's main thread is still suspended. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "fixed_identity.h"
static int identify(const char* path) {
    return fixed_identify_file(path)!=NULL;
}
static int fail(const char* message) {
    fprintf(stderr,"%s (Windows error %lu)\n",message,GetLastError());
    if (!GetEnvironmentVariableA("HFR_TEST_MODE",NULL,0))
        MessageBoxA(NULL,message,"Touhou HFR x64",MB_ICONERROR);
    return 1;
}
static uintptr_t remote_module(DWORD pid,const char* name) {
    HANDLE snap=INVALID_HANDLE_VALUE;
    for (unsigned i=0;i<100;++i) {
        snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);
        if (snap!=INVALID_HANDLE_VALUE || GetLastError()!=ERROR_BAD_LENGTH) break;
        Sleep(1);
    }
    if (snap==INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32 m={0};m.dwSize=sizeof m;uintptr_t address=0;
    if (Module32First(snap,&m)) do {
        if (!_stricmp(m.szModule,name)) {address=(uintptr_t)m.modBaseAddr;break;}
    } while (Module32Next(snap,&m));
    CloseHandle(snap);return address;
}
static int remote_call(HANDLE process,uintptr_t fn,void* param,DWORD* result) {
    HANDLE t=CreateRemoteThread(process,NULL,0,(LPTHREAD_START_ROUTINE)fn,param,0,NULL);
    if (!t) return 0;
    int ok=WaitForSingleObject(t,15000)==WAIT_OBJECT_0 && GetExitCodeThread(t,result);
    CloseHandle(t);return ok;
}
int main(int argc,char** argv) {
    if (argc==3 && !strcmp(argv[1],"--check")) return identify(argv[2])?0:2;
    char dir[MAX_PATH],exe[MAX_PATH],dll[MAX_PATH],command[MAX_PATH+4];
    if (!GetModuleFileNameA(NULL,dir,sizeof dir) || strlen(dir)>MAX_PATH-32) return fail("Launcher path is too long.");
    char* slash=strrchr(dir,'\\');if (!slash) return 1;*slash=0;
    snprintf(dll,sizeof dll,"%s\\touhou_hfr64.dll",dir);
    if (argc>1) {
        DWORD length=GetFullPathNameA(argv[1],sizeof exe,exe,NULL);
        if (!length || length>=sizeof exe) return fail("Cannot resolve the game path, or the path is too long.");
    } else {
        exe[0]=0;
        for (size_t i=0;i<FIXED_GAME_COUNT;++i) {
            char candidate[MAX_PATH];snprintf(candidate,sizeof candidate,"%s\\%s",dir,fixed_games[i]->executable);
            if (!identify(candidate)) continue;
            if (exe[0]) return fail("More than one x64 game is present. Pass an executable path.");
            strcpy(exe,candidate);
        }
    }
    if (!identify(exe)) return fail("This executable is not the verified TH06 New Classic build. No game process was started.");
    HMODULE local=LoadLibraryExA(dll,NULL,DONT_RESOLVE_DLL_REFERENCES);
    if (!local) return fail("touhou_hfr64.dll is missing or invalid.");
    FARPROC entry=GetProcAddress(local,"hfr_start");
    uintptr_t entry_rva=(uintptr_t)entry-(uintptr_t)local;
    FreeLibrary(local);
    if (!entry) return fail("The x64 runtime has no initialization entry point.");
    char working[MAX_PATH];strcpy(working,exe);slash=strrchr(working,'\\');if (!slash) return 1;*slash=0;
    snprintf(command,sizeof command,"\"%s\"",exe);
    STARTUPINFOA si={0};si.cb=sizeof si;PROCESS_INFORMATION pi={0};
    if (!CreateProcessA(exe,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,working,&si,&pi)) return fail("Cannot start the game.");
    const char* error=NULL;DWORD result=0;
    void* remote=VirtualAllocEx(pi.hProcess,NULL,strlen(dll)+1,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    HMODULE kernel=GetModuleHandleA("kernel32.dll");
    uintptr_t load_rva=(uintptr_t)GetProcAddress(kernel,"LoadLibraryA")-(uintptr_t)kernel;
    uintptr_t remote_kernel=remote_module(pi.dwProcessId,"kernel32.dll");
    /* kernel32 is not loaded until the suspended process initializes its loader.
       System DLLs share their ASLR mapping within this same-architecture session. */
    uintptr_t load=remote_kernel ? remote_kernel+load_rva : (uintptr_t)GetProcAddress(kernel,"LoadLibraryA");
    if (!remote || !WriteProcessMemory(pi.hProcess,remote,dll,strlen(dll)+1,NULL) ||
        !remote_call(pi.hProcess,load,remote,&result)) error="Could not load the runtime; the suspended game was stopped.";
    uintptr_t loaded=error?0:remote_module(pi.dwProcessId,"touhou_hfr64.dll");
    fprintf(stdout,"Loader result=%08lx module=%p\n",result,(void*)loaded);fflush(stdout);
    if (!error && (!loaded || !remote_call(pi.hProcess,loaded+entry_rva,NULL,&result) || result!=1))
        error="Runtime initialization failed. See touhou_hfr.log. The suspended game was stopped.";
    if (error) TerminateProcess(pi.hProcess,1);
    else {ResumeThread(pi.hThread);printf("Started pid=%lu\n",pi.dwProcessId);}
    if (remote) VirtualFreeEx(pi.hProcess,remote,0,MEM_RELEASE);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    return error?fail(error):0;
}

