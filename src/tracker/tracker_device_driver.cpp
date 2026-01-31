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

TrackerDriver::TrackerDriver(TrackerRole role, mpsc::Receiver<Pose> poseReceiver)
    : m_role(role)
    , m_poseReceiver(std::move(poseReceiver))
{
    m_serialNumber = std::string("OVD-TRACKER-") + GetTrackerRoleName(role);
}

vr::EVRInitError TrackerDriver::Activate(uint32_t unObjectId)
{
    m_deviceIndex = unObjectId;

    vr::PropertyContainerHandle_t container = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_deviceIndex);

    vr::VRProperties()->SetStringProperty(container, vr::Prop_ModelNumber_String, "OVD Tracker");
    vr::VRProperties()->SetStringProperty(container, vr::Prop_SerialNumber_String, m_serialNumber.c_str());
    vr::VRProperties()->SetStringProperty(container, vr::Prop_ControllerType_String, GetTrackerRoleHint(m_role));
    vr::VRProperties()->SetUint64Property(container, vr::Prop_CurrentUniverseId_Uint64, 2);

    // Send initial T-pose position based on tracker role
    {
        vr::DriverPose_t pose = {};
        pose.poseIsValid = true;
        pose.result = vr::TrackingResult_Running_OK;
        pose.deviceIsConnected = true;
        pose.qWorldFromDriverRotation.w = 1.0;
        pose.qDriverFromHeadRotation.w = 1.0;
        pose.qRotation.w = 1.0;
        pose.qRotation.x = 0.0;
        pose.qRotation.y = 0.0;
        pose.qRotation.z = 0.0;

        switch (m_role)
        {
            case TrackerRole::Waist:
                pose.vecPosition[0] = 0.0; pose.vecPosition[1] = 0.93; pose.vecPosition[2] = 0.0;
                break;
            case TrackerRole::Chest:
                pose.vecPosition[0] = 0.0; pose.vecPosition[1] = 1.29; pose.vecPosition[2] = 0.0;
                break;
            case TrackerRole::LeftShoulder:
                pose.vecPosition[0] = -0.15; pose.vecPosition[1] = 1.41; pose.vecPosition[2] = 0.0;
                break;
            case TrackerRole::RightShoulder:
                pose.vecPosition[0] = 0.15; pose.vecPosition[1] = 1.41; pose.vecPosition[2] = 0.0;
                break;
            case TrackerRole::LeftElbow:
                pose.vecPosition[0] = -0.45; pose.vecPosition[1] = 1.41; pose.vecPosition[2] = 0.0;
                break;
            case TrackerRole::RightElbow:
                pose.vecPosition[0] = 0.45; pose.vecPosition[1] = 1.41; pose.vecPosition[2] = 0.0;
                break;
            case TrackerRole::LeftKnee:
                pose.vecPosition[0] = -0.09; pose.vecPosition[1] = 0.46; pose.vecPosition[2] = 0.0;
                break;
            case TrackerRole::RightKnee:
                pose.vecPosition[0] = 0.09; pose.vecPosition[1] = 0.46; pose.vecPosition[2] = 0.0;
                break;
            case TrackerRole::LeftFoot:
                pose.vecPosition[0] = -0.09; pose.vecPosition[1] = 0.06; pose.vecPosition[2] = 0.0;
                break;
            case TrackerRole::RightFoot:
                pose.vecPosition[0] = 0.09; pose.vecPosition[1] = 0.06; pose.vecPosition[2] = 0.0;
                break;
        }
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_deviceIndex, pose, sizeof(vr::DriverPose_t));
    }

    // Start pose update thread
    m_poseThread = std::jthread([this](std::stop_token st) {
        while (!st.stop_requested())
        {
            if (auto p = m_poseReceiver.recv())
            {
                vr::DriverPose_t pose = {};
                pose.qWorldFromDriverRotation.w = 1.0;
                pose.qDriverFromHeadRotation.w = 1.0;
                pose.vecPosition[0] = p->posX;
                pose.vecPosition[1] = p->posY;
                pose.vecPosition[2] = p->posZ;
                pose.qRotation.w = p->rotW;
                pose.qRotation.x = p->rotX;
                pose.qRotation.y = p->rotY;
                pose.qRotation.z = p->rotZ;
                if (pose.qRotation.w == 0.0 && pose.qRotation.x == 0.0 &&
                    pose.qRotation.y == 0.0 && pose.qRotation.z == 0.0)
                {
                    pose.qRotation.w = 1.0;
                }
                pose.poseIsValid = true;
                pose.deviceIsConnected = true;
                pose.result = vr::TrackingResult_Running_OK;
                vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_deviceIndex, pose, sizeof(vr::DriverPose_t));
            }
            else
            {
                break; // Channel closed
            }
        }
    });

    return vr::VRInitError_None;
}

void TrackerDriver::Deactivate()
{
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
    // Deprecated - poses are pushed via TrackedDevicePoseUpdated
    vr::DriverPose_t pose = {};
    pose.poseIsValid = false;
    return pose;
}
