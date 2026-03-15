/*
    Expose HmdDriverFactory as entrance point to driver dll, as expected by openvr.
*/

#include <openvr_driver.h>
#include <cstring>
#include "provider/device_provider.h"

AIVRDeviceProvider g_deviceProvider;

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif

EXPORT void* HmdDriverFactory(const char* pInterfaceName, int* pReturnCode)
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
