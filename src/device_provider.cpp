#include "device_provider.h"
#include "hmd_device_driver.h"

vr::EVRInitError AIVRDeviceProvider::Init(vr::IVRDriverContext* pDriverContext)
{
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

    m_pHmd = std::make_unique<Driver>();

    if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
            m_pHmd->GetSerialNumber(),
            vr::TrackedDeviceClass_HMD,
            m_pHmd.get()))
    {
        return vr::VRInitError_Driver_Unknown;
    }

    return vr::VRInitError_None;
}

void AIVRDeviceProvider::Cleanup()
{
    m_pHmd.reset();
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

const char* const* AIVRDeviceProvider::GetInterfaceVersions()
{
    return vr::k_InterfaceVersions;
}

void AIVRDeviceProvider::RunFrame()
{
    if (m_pHmd)
    {
        m_pHmd->RunFrame();
    }

    // Poll events
    vr::VREvent_t event;
    while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(event)))
    {
        if (m_pHmd)
        {
            m_pHmd->ProcessEvent(event);
        }
    }
}

bool AIVRDeviceProvider::ShouldBlockStandbyMode()
{
    return false;
}

void AIVRDeviceProvider::EnterStandby()
{
}

void AIVRDeviceProvider::LeaveStandby()
{
}
