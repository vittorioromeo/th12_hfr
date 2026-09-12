/* One launcher for all registered executable profiles. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "identity.h"

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
static int local_path(char* out,size_t cap,const char* dir,const char* name) {
    /* exe names are relative to the launcher, including optional subfolders. */
    int n=snprintf(out,cap,"%s\\%s",dir,name);return n>=0 && (size_t)n<cap;
}
int main(int argc,char** argv) {
    /* Read-only diagnostic for scripts; no process is created. */
    if(argc==3 && !strcmp(argv[1],"--check"))return identify_file(argv[2])?0:2;
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
        if(selected && !common_ini && selected->legacy_ini) {
            if(!local_path(ini,sizeof ini,dir,selected->legacy_ini))die("Configuration path is too long.");
            char legacy[MAX_PATH];GetPrivateProfileStringA("launcher","exe","",legacy,sizeof legacy,ini);
            if(legacy[0])strcpy(target,legacy);
        }
    }
    if(!target[0] || !local_path(exe,sizeof exe,dir,target) || !identify_file(exe))
        die("No supported executable found. Supported: TH10 v1.00a, TH11 v1.00a, TH12 v1.00b and TH13 v1.00c (Japanese or English executables). Code modified by another patch is not accepted.");
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
