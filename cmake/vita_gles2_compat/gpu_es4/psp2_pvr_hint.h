#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PVRSRV_PSP2_APPHINT {
    char szGLES1[256];
    char szGLES2[256];
    char szWindowSystem[256];
    unsigned int ui32SwTexOpCleanupDelay;
} PVRSRV_PSP2_APPHINT;

static inline void PVRSRVInitializeAppHint(PVRSRV_PSP2_APPHINT* hint)
{
    if (!hint)
        return;

    hint->szGLES1[0]            = '\0';
    hint->szGLES2[0]            = '\0';
    hint->szWindowSystem[0]     = '\0';
    hint->ui32SwTexOpCleanupDelay = 0;
}

static inline void PVRSRVCreateVirtualAppHint(const PVRSRV_PSP2_APPHINT* hint)
{
    (void)hint;
}

#ifdef __cplusplus
}
#endif
