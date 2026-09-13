/* Every function the system's dxgi.dll exports, as one list. The proxy uses it three times:
   to declare each name for export, to emit the jump stub the loader will bind to, and to
   resolve the real address at load time. One list means the three can never disagree.
   Taken from the mingw-w64 import library; a name the running system does not have resolves
   to a stub that fails, which is what calling an absent export would have done anyway. */
#define DXGI_EXPORTS(X) \
    X(ApplyCompatResolutionQuirking) \
    X(CompatString) \
    X(CompatValue) \
    X(CreateDXGIFactory) \
    X(CreateDXGIFactory1) \
    X(CreateDXGIFactory2) \
    X(D3DKMTCloseAdapter) \
    X(D3DKMTCreateAllocation) \
    X(D3DKMTCreateContext) \
    X(D3DKMTCreateDevice) \
    X(D3DKMTCreateSynchronizationObject) \
    X(D3DKMTDestroyAllocation) \
    X(D3DKMTDestroyContext) \
    X(D3DKMTDestroyDevice) \
    X(D3DKMTDestroySynchronizationObject) \
    X(D3DKMTEscape) \
    X(D3DKMTGetContextSchedulingPriority) \
    X(D3DKMTGetDeviceState) \
    X(D3DKMTGetDisplayModeList) \
    X(D3DKMTGetMultisampleMethodList) \
    X(D3DKMTGetRuntimeData) \
    X(D3DKMTGetSharedPrimaryHandle) \
    X(D3DKMTLock) \
    X(D3DKMTOpenAdapterFromHdc) \
    X(D3DKMTOpenResource) \
    X(D3DKMTPresent) \
    X(D3DKMTQueryAdapterInfo) \
    X(D3DKMTQueryAllocationResidency) \
    X(D3DKMTQueryResourceInfo) \
    X(D3DKMTRender) \
    X(D3DKMTSetAllocationPriority) \
    X(D3DKMTSetContextSchedulingPriority) \
    X(D3DKMTSetDisplayMode) \
    X(D3DKMTSetDisplayPrivateDriverFormat) \
    X(D3DKMTSetGammaRamp) \
    X(D3DKMTSetVidPnSourceOwner) \
    X(D3DKMTSignalSynchronizationObject) \
    X(D3DKMTUnlock) \
    X(D3DKMTWaitForSynchronizationObject) \
    X(D3DKMTWaitForVerticalBlankEvent) \
    X(DXGID3D10CreateDevice) \
    X(DXGID3D10CreateLayeredDevice) \
    X(DXGID3D10ETWRundown) \
    X(DXGID3D10GetLayeredDeviceSize) \
    X(DXGID3D10RegisterLayers) \
    X(DXGIDeclareAdapterRemovalSupport) \
    X(DXGIDumpJournal) \
    X(DXGIGetDebugInterface1) \
    X(DXGIReportAdapterConfiguration) \
    X(DXGIRevertToSxS) \
    X(OpenAdapter10) \
    X(OpenAdapter10_2) \
    X(PIXBeginCapture) \
    X(PIXEndCapture) \
    X(PIXGetCaptureState) \
    X(SetAppCompatStringPointer) \
    X(UpdateHMDEmulationStatus)
