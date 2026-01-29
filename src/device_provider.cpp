#include "device_provider.h"
#include "hmd_device_driver.h"
#include "controller_device_driver.h"
#include "tracker_device_driver.h"

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

    // Add body trackers
    const TrackerRole trackerRoles[] = {
        TrackerRole::Waist,
        TrackerRole::Chest,
        TrackerRole::LeftFoot,
        TrackerRole::RightFoot,
        TrackerRole::LeftKnee,
        TrackerRole::RightKnee,
        TrackerRole::LeftElbow,
        TrackerRole::RightElbow,
        TrackerRole::LeftShoulder,
        TrackerRole::RightShoulder
    };

    for (size_t i = 0; i < m_trackers.size(); ++i)
    {
        m_trackers[i] = std::make_unique<TrackerDriver>(trackerRoles[i]);
        if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
                m_trackers[i]->GetSerialNumber(),
                vr::TrackedDeviceClass_GenericTracker,
                m_trackers[i].get()))
        {
            return vr::VRInitError_Driver_Unknown;
        }
    }

    return vr::VRInitError_None;
}

void AIVRDeviceProvider::Cleanup()
{
    for (auto& tracker : m_trackers)
    {
        tracker.reset();
    }
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

        // Get body pose from socket and pass to trackers
        if (auto bodyPose = m_pHmd->GetNextBodyPose())
        {
            // Update trackers with body poses
            // Order matches TrackerRole enum and m_trackers array
            if (m_trackers[0]) m_trackers[0]->UpdatePose(bodyPose->waist);
            if (m_trackers[1]) m_trackers[1]->UpdatePose(bodyPose->chest);
            if (m_trackers[2]) m_trackers[2]->UpdatePose(bodyPose->leftFoot);
            if (m_trackers[3]) m_trackers[3]->UpdatePose(bodyPose->rightFoot);
            if (m_trackers[4]) m_trackers[4]->UpdatePose(bodyPose->leftKnee);
            if (m_trackers[5]) m_trackers[5]->UpdatePose(bodyPose->rightKnee);
            if (m_trackers[6]) m_trackers[6]->UpdatePose(bodyPose->leftElbow);
            if (m_trackers[7]) m_trackers[7]->UpdatePose(bodyPose->rightElbow);
            if (m_trackers[8]) m_trackers[8]->UpdatePose(bodyPose->leftShoulder);
            if (m_trackers[9]) m_trackers[9]->UpdatePose(bodyPose->rightShoulder);

            // Update controller positions from body pose hands
            if (m_pLeftController)
            {
                m_pLeftController->UpdateHandPose(bodyPose->leftHand);
            }
            if (m_pRightController)
            {
                m_pRightController->UpdateHandPose(bodyPose->rightHand);
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
