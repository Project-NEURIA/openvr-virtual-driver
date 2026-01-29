#include "device_provider.h"
#include "hmd_device_driver.h"
#include "controller_device_driver.h"

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

    // Add left controller
    m_pLeftController = std::make_unique<ControllerDriver>(vr::TrackedControllerRole_LeftHand);
    if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
            m_pLeftController->GetSerialNumber(),
            vr::TrackedDeviceClass_Controller,
            m_pLeftController.get()))
    {
        return vr::VRInitError_Driver_Unknown;
    }

    // Add right controller
    m_pRightController = std::make_unique<ControllerDriver>(vr::TrackedControllerRole_RightHand);
    if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
            m_pRightController->GetSerialNumber(),
            vr::TrackedDeviceClass_Controller,
            m_pRightController.get()))
    {
        return vr::VRInitError_Driver_Unknown;
    }

    return vr::VRInitError_None;
}

void AIVRDeviceProvider::Cleanup()
{
    m_pLeftController.reset();
    m_pRightController.reset();
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

        // Get controller input from socket and pass to controllers
        if (auto input = m_pHmd->GetNextControllerInput())
        {
            if (m_pLeftController)
            {
                m_pLeftController->UpdateInput(*input);
            }
            if (m_pRightController)
            {
                m_pRightController->UpdateInput(*input);
            }
        }
    }

    // Run controller frames
    if (m_pLeftController)
    {
        m_pLeftController->RunFrame();
    }
    if (m_pRightController)
    {
        m_pRightController->RunFrame();
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
