#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
/* Shared by both the launcher and injected runtime; no executable is modified. */
static int hfr_file_sha256(const char* path, char hex[65]) {
    FILE* f=fopen(path,"rb"); if (!f) return 0;
    BCRYPT_ALG_HANDLE alg=NULL; BCRYPT_HASH_HANDLE hash=NULL;
    unsigned char bytes[65536], digest[32]; size_t n; int ok=0;
    if (BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0 ||
        BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)<0) goto done;
    while ((n=fread(bytes,1,sizeof bytes,f))!=0)
        if (BCryptHashData(hash,bytes,(ULONG)n,0)<0) goto done;
    if (ferror(f) || BCryptFinishHash(hash,digest,sizeof digest,0)<0) goto done;
    for (unsigned i=0;i<sizeof digest;++i) sprintf(hex+2*i,"%02x",digest[i]);
    ok=1;
done:
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg,0);
    fclose(f); return ok;
}
