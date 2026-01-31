/*
    Expose HmdDriverFactory as entrance point to driver dll, as expected by openvr.
*/

#include <openvr_driver.h>
#include "provider/device_provider.h"

AIVRDeviceProvider g_deviceProvider;

extern "C" __declspec(dllexport) void* HmdDriverFactory(const char* pInterfaceName, int* pReturnCode)
{
    if (0 == strcmp(vr::IServerTrackedDeviceProvider_Version, pInterfaceName))
    {
        return &g_deviceProvider;
    }

    if (pReturnCode)
    {
        *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
    }

    return nullptr;
}
