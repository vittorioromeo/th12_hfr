/* One launcher for all registered executable profiles. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "identity.h"
#include "fixed_identity.h"

static void die(const char* msg) {MessageBoxA(NULL,msg,"Touhou HFR launcher",MB_ICONERROR);ExitProcess(1);}
static const struct GameIdentity* identify_file(const char* path) {
    FILE* f=fopen(path,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    if(n<=0 || n>16*1024*1024){fclose(f);return NULL;}
    uint8_t* data=malloc(n);const struct GameIdentity* id=NULL;
    if(data && fread(data,1,n,f)==(size_t)n) {
        size_t size=0;uint8_t* image=map_game_file(data,n,&size);
        if(image){id=identify_image(image,size);free(image);}
    }
    free(data);fclose(f);return id;
}
/* Whether the file is a game the patch knows, hidden inside a DRM wrapper. Nothing on disk
   can confirm which game it is -- the code is encrypted until the wrapper's stub runs -- so
   this only answers whether that is why identification failed, which is the difference
   between a useful message and "no supported executable found". */
static int wrapped_file(const char* path) {
    FILE* f=fopen(path,"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    if(n<=0 || n>16*1024*1024){fclose(f);return 0;}
    uint8_t* data=malloc(n);int wrapped=0;
    if(data && fread(data,1,n,f)==(size_t)n) {
        size_t size=0;uint8_t* image=map_game_file(data,n,&size);
        if(image){wrapped=wrapped_executable(image,size);free(image);}
    }
    free(data);fclose(f);return wrapped;
}
static int local_path(char* out,size_t cap,const char* dir,const char* name) {
    /* exe names are relative to the launcher, including optional subfolders. */
    int n=snprintf(out,cap,"%s\\%s",dir,name);return n>=0 && (size_t)n<cap;
}
static int run_x64(const char* dir,const char* exe) {
    char helper[MAX_PATH],command[MAX_PATH*2+8];
    if (!local_path(helper,sizeof helper,dir,"touhou_hfr64.exe") || GetFileAttributesA(helper)==INVALID_FILE_ATTRIBUTES)
        die("TH06 New Classic requires touhou_hfr64.exe and touhou_hfr64.dll next to this launcher. Build with build64.ps1.");
    snprintf(command,sizeof command,"\"%s\" \"%s\"",helper,exe);
    STARTUPINFOA si={0};si.cb=sizeof si;PROCESS_INFORMATION pi={0};
    if (!CreateProcessA(helper,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,dir,&si,&pi)) die("Cannot start the x64 launcher helper.");
    WaitForSingleObject(pi.hProcess,INFINITE);
    DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return (int)code;
}
int main(int argc,char** argv) {
    /* Read-only diagnostic for scripts; no process is created. */
    if(argc==3 && !strcmp(argv[1],"--check"))return identify_file(argv[2]) || fixed_identify_file(argv[2]) ? 0:2;
    char dir[MAX_PATH],exe[MAX_PATH],dll[MAX_PATH],ini[MAX_PATH],target[MAX_PATH]="";
    if(!GetModuleFileNameA(NULL,dir,sizeof dir) || strlen(dir)>=sizeof dir-1)die("Launcher path is too long.");
    char* p=strrchr(dir,'\\');if(!p)die("Cannot locate the launcher directory.");*p=0;
    if(!local_path(ini,sizeof ini,dir,"touhou_hfr.ini") || !local_path(dll,sizeof dll,dir,"touhou_hfr.dll"))die("Launcher path is too long.");
    int common_ini=GetFileAttributesA(ini)!=INVALID_FILE_ATTRIBUTES;
    if(argc>1) {
        if(strlen(argv[1])>=sizeof target)die("Executable name is too long.");
        strcpy(target,argv[1]);
    }else GetPrivateProfileStringA("launcher","exe","",target,sizeof target,ini);
    const struct GameIdentity* selected=NULL;
    if(!target[0]) {
        for(size_t g=0;g<GAME_COUNT;++g)for(int e=0;e<2;++e) {
            if(!local_path(exe,sizeof exe,dir,game_identities[g].executables[e]))continue;
            const struct GameIdentity* id=identify_file(exe);if(!id)continue;
            if(selected && selected!=id)die("More than one supported game is present. Set [launcher] exe in touhou_hfr.ini or pass an executable name.");
            if(!selected){strcpy(target,game_identities[g].executables[e]);selected=id;}
        }
        for (size_t f=0;f<FIXED_GAME_COUNT;++f) {
            if (!local_path(exe,sizeof exe,dir,fixed_games[f]->executable) || !fixed_identify_file(exe)) continue;
            if (target[0]) die("More than one supported game is present. Set [launcher] exe in touhou_hfr.ini or pass an executable name.");
            strcpy(target,fixed_games[f]->executable);
        }
        if(selected && !common_ini && selected->legacy_ini) {
            if(!local_path(ini,sizeof ini,dir,selected->legacy_ini))die("Configuration path is too long.");
            char legacy[MAX_PATH];GetPrivateProfileStringA("launcher","exe","",legacy,sizeof legacy,ini);
            if(legacy[0])strcpy(target,legacy);
        }
    }
    if (target[0] && local_path(exe,sizeof exe,dir,target) && fixed_identify_file(exe)) return run_x64(dir,exe);
    if(!target[0] || !local_path(exe,sizeof exe,dir,target) || !identify_file(exe)) {
        /* A wrapped executable is not a failure to support the game -- it is a build this
           launcher cannot verify or start, because its code is encrypted until its own
           start-up code has run and because starting it outside Steam is Steam's business,
           not ours. The patch installs itself perfectly well there through dinput8.dll. */
        char candidate[MAX_PATH];int wrapped=target[0] && local_path(candidate,sizeof candidate,dir,target) && wrapped_file(candidate);
        for(size_t g=0;!wrapped && g<GAME_COUNT;++g)for(int e=0;!wrapped && e<2;++e)
            if(local_path(candidate,sizeof candidate,dir,game_identities[g].executables[e]))
                wrapped=wrapped_file(candidate);
        if(wrapped)
            die("This looks like a Steam copy of the game. Its code is encrypted until the game "
                "itself starts, so this launcher cannot check it or start it.\n\n"
                "Start the game from Steam instead. With dinput8.dll, touhou_hfr.dll and "
                "touhou_hfr.ini in the game's folder, the patch installs itself on any launch -- "
                "the launcher is not needed.");
        char message[1024] = "No supported executable found. This build recognizes:";
        for (size_t g = 0; g < GAME_COUNT; ++g) {
            size_t used = strlen(message);
            snprintf(message + used, sizeof message - used, "\n%s", game_identities[g].name);
        }
        for (size_t g = 0; g < FIXED_GAME_COUNT; ++g) {
            size_t used = strlen(message);
            snprintf(message + used, sizeof message - used, "\n%s", fixed_games[g]->name);
        }
        size_t used = strlen(message);
        snprintf(message + used, sizeof message - used, "\n\nAn unrecognized or modified executable is not accepted. See the README for each game's available features.");
        die(message);
    }
    if(GetFileAttributesA(dll)==INVALID_FILE_ATTRIBUTES)die("touhou_hfr.dll is missing next to the launcher.");
    STARTUPINFOA si={0};si.cb=sizeof si;PROCESS_INFORMATION pi;
    char cmd[MAX_PATH+4];snprintf(cmd,sizeof cmd,"\"%s\"",exe);
    if(!CreateProcessA(exe,cmd,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,dir,&si,&pi))die("CreateProcess failed.");
    SIZE_T len=strlen(dll)+1;
    void* rem=VirtualAllocEx(pi.hProcess,NULL,len,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!rem || !WriteProcessMemory(pi.hProcess,rem,dll,len,NULL)){TerminateProcess(pi.hProcess,1);die("Cannot write the DLL path into the game process.");}
    HANDLE thread=CreateRemoteThread(pi.hProcess,NULL,0,(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"),"LoadLibraryA"),rem,0,NULL);
    if(!thread){TerminateProcess(pi.hProcess,1);die("Cannot create the DLL loader thread.");}
    if(WaitForSingleObject(thread,10000)!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,1);die("Timed out loading Touhou HFR; the suspended game was stopped.");}
    DWORD loaded=0;GetExitCodeThread(thread,&loaded);CloseHandle(thread);
    VirtualFreeEx(pi.hProcess,rem,0,MEM_RELEASE);
    if(!loaded){TerminateProcess(pi.hProcess,1);die("Touhou HFR failed to load into the game.");}
    ResumeThread(pi.hThread);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return 0;
}
