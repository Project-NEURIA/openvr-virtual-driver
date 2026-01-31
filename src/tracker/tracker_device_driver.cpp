#include "tracker_device_driver.h"

static const char* GetTrackerRoleName(TrackerRole role)
{
    switch (role)
    {
        case TrackerRole::Waist: return "waist";
        case TrackerRole::Chest: return "chest";
        case TrackerRole::LeftFoot: return "left_foot";
        case TrackerRole::RightFoot: return "right_foot";
        case TrackerRole::LeftKnee: return "left_knee";
        case TrackerRole::RightKnee: return "right_knee";
        case TrackerRole::LeftElbow: return "left_elbow";
        case TrackerRole::RightElbow: return "right_elbow";
        case TrackerRole::LeftShoulder: return "left_shoulder";
        case TrackerRole::RightShoulder: return "right_shoulder";
        default: return "unknown";
    }
}

static const char* GetTrackerRoleHint(TrackerRole role)
{
    // These match the vr::TrackerRole enum values expected by SteamVR
    switch (role)
    {
        case TrackerRole::Waist: return "vive_tracker_waist";
        case TrackerRole::Chest: return "vive_tracker_chest";
        case TrackerRole::LeftFoot: return "vive_tracker_left_foot";
        case TrackerRole::RightFoot: return "vive_tracker_right_foot";
        case TrackerRole::LeftKnee: return "vive_tracker_left_knee";
        case TrackerRole::RightKnee: return "vive_tracker_right_knee";
        case TrackerRole::LeftElbow: return "vive_tracker_left_elbow";
        case TrackerRole::RightElbow: return "vive_tracker_right_elbow";
        case TrackerRole::LeftShoulder: return "vive_tracker_left_shoulder";
        case TrackerRole::RightShoulder: return "vive_tracker_right_shoulder";
        default: return "vive_tracker_handed";
    }
}

TrackerDriver::TrackerDriver(TrackerRole role)
    : m_role(role)
{
    m_serialNumber = std::string("OVD-TRACKER-") + GetTrackerRoleName(role);
}

vr::EVRInitError TrackerDriver::Activate(uint32_t unObjectId)
{
    m_deviceIndex = unObjectId;
    m_isActive = true;

    vr::PropertyContainerHandle_t container = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_deviceIndex);

    // Set tracker properties
    vr::VRProperties()->SetStringProperty(container, vr::Prop_ModelNumber_String, "OVD Tracker");
    vr::VRProperties()->SetStringProperty(container, vr::Prop_SerialNumber_String, m_serialNumber.c_str());
    vr::VRProperties()->SetStringProperty(container, vr::Prop_ControllerType_String, GetTrackerRoleHint(m_role));
    vr::VRProperties()->SetUint64Property(container, vr::Prop_CurrentUniverseId_Uint64, 2);

    // Start pose update thread
    m_poseThread = std::jthread([this](std::stop_token st) {
        while (!st.stop_requested() && m_isActive)
        {
            vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_deviceIndex, GetPose(), sizeof(vr::DriverPose_t));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    return vr::VRInitError_None;
}

void TrackerDriver::Deactivate()
{
    m_isActive = false;
    if (m_poseThread.joinable())
    {
        m_poseThread.request_stop();
        m_poseThread.join();
    }
    m_deviceIndex = vr::k_unTrackedDeviceIndexInvalid;
}

void TrackerDriver::EnterStandby()
{
}

void* TrackerDriver::GetComponent(const char* pchComponentNameAndVersion)
{
    return nullptr;
}

void TrackerDriver::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize >= 1)
        pchResponseBuffer[0] = 0;
}

vr::DriverPose_t TrackerDriver::GetPose()
{
    vr::DriverPose_t pose = {};

    pose.qWorldFromDriverRotation.w = 1.0;
    pose.qDriverFromHeadRotation.w = 1.0;

    Pose currentPose;
    {
        std::lock_guard<std::mutex> lock(m_poseMutex);
        currentPose = m_currentPose;
    }

    pose.vecPosition[0] = currentPose.posX;
    pose.vecPosition[1] = currentPose.posY;
    pose.vecPosition[2] = currentPose.posZ;

    pose.qRotation.w = currentPose.rotW;
    pose.qRotation.x = currentPose.rotX;
    pose.qRotation.y = currentPose.rotY;
    pose.qRotation.z = currentPose.rotZ;

    // Default rotation if not set
    if (pose.qRotation.w == 0.0 && pose.qRotation.x == 0.0 &&
        pose.qRotation.y == 0.0 && pose.qRotation.z == 0.0)
    {
        pose.qRotation.w = 1.0;
    }

    pose.poseIsValid = true;
    pose.deviceIsConnected = true;
    pose.result = vr::TrackingResult_Running_OK;

    return pose;
}

void TrackerDriver::UpdatePose(const Pose& pose)
{
    std::lock_guard<std::mutex> lock(m_poseMutex);
    m_currentPose = pose;
}
