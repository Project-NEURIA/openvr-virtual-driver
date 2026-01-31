#include "controller_device_driver.h"
#include <chrono>

ControllerDriver::ControllerDriver(vr::ETrackedControllerRole role,
                                   mpsc::Receiver<ControllerInput> inputReceiver,
                                   mpsc::Receiver<Pose> poseReceiver)
    : m_role(role)
    , m_inputReceiver(std::move(inputReceiver))
    , m_poseReceiver(std::move(poseReceiver))
{
    if (role == vr::TrackedControllerRole_LeftHand)
    {
        m_serialNumber = "OVD-CTRL-LEFT";
    }
    else
    {
        m_serialNumber = "OVD-CTRL-RIGHT";
    }
}

vr::EVRInitError ControllerDriver::Activate(uint32_t unObjectId)
{
    m_deviceIndex = unObjectId;

    vr::PropertyContainerHandle_t container = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_deviceIndex);

    // Set controller properties
    vr::VRProperties()->SetStringProperty(container, vr::Prop_ModelNumber_String, "OVD Controller");
    vr::VRProperties()->SetStringProperty(container, vr::Prop_SerialNumber_String, m_serialNumber.c_str());
    vr::VRProperties()->SetInt32Property(container, vr::Prop_ControllerRoleHint_Int32, m_role);
    vr::VRProperties()->SetStringProperty(container, vr::Prop_ControllerType_String, "ovd_controller");
    vr::VRProperties()->SetStringProperty(container, vr::Prop_InputProfilePath_String, "{openvr_virtual_driver}/input/ovd_controller_profile.json");
    vr::VRProperties()->SetUint64Property(container, vr::Prop_CurrentUniverseId_Uint64, 2);

    // Create joystick input components
    vr::VRDriverInput()->CreateScalarComponent(container, "/input/joystick/x", &m_joystickXHandle,
        vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    vr::VRDriverInput()->CreateScalarComponent(container, "/input/joystick/y", &m_joystickYHandle,
        vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/joystick/click", &m_joystickClickHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/joystick/touch", &m_joystickTouchHandle);

    // Create trigger input components
    vr::VRDriverInput()->CreateScalarComponent(container, "/input/trigger/value", &m_triggerValueHandle,
        vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/trigger/click", &m_triggerClickHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/trigger/touch", &m_triggerTouchHandle);

    // Create grip input components
    vr::VRDriverInput()->CreateScalarComponent(container, "/input/grip/value", &m_gripValueHandle,
        vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/grip/click", &m_gripClickHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/grip/touch", &m_gripTouchHandle);

    // Create button components
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/a/click", &m_aClickHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/a/touch", &m_aTouchHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/b/click", &m_bClickHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/b/touch", &m_bTouchHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/system/click", &m_systemClickHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/application_menu/click", &m_menuClickHandle);

    // Create haptic component
    vr::VRDriverInput()->CreateHapticComponent(container, "/output/haptic", &m_hapticHandle);

    // Start input update thread
    m_inputThread = std::jthread([this](std::stop_token st) { InputUpdateThreadFunc(st); });

    // Start pose update thread
    m_poseThread = std::jthread([this](std::stop_token st) { PoseUpdateThreadFunc(st); });

    return vr::VRInitError_None;
}

void ControllerDriver::InputUpdateThreadFunc(std::stop_token st)
{
    while (!st.stop_requested())
    {
        if (auto input = m_inputReceiver.recv())
        {
            vr::VRDriverInput()->UpdateScalarComponent(m_joystickXHandle, input->joystickX, 0);
            vr::VRDriverInput()->UpdateScalarComponent(m_joystickYHandle, input->joystickY, 0);
            vr::VRDriverInput()->UpdateBooleanComponent(m_joystickClickHandle, input->joystickClick, 0);
            vr::VRDriverInput()->UpdateBooleanComponent(m_joystickTouchHandle, input->joystickTouch, 0);

            vr::VRDriverInput()->UpdateScalarComponent(m_triggerValueHandle, input->trigger, 0);
            vr::VRDriverInput()->UpdateBooleanComponent(m_triggerClickHandle, input->triggerClick, 0);
            vr::VRDriverInput()->UpdateBooleanComponent(m_triggerTouchHandle, input->triggerTouch, 0);

            vr::VRDriverInput()->UpdateScalarComponent(m_gripValueHandle, input->grip, 0);
            vr::VRDriverInput()->UpdateBooleanComponent(m_gripClickHandle, input->gripClick, 0);
            vr::VRDriverInput()->UpdateBooleanComponent(m_gripTouchHandle, input->gripTouch, 0);

            vr::VRDriverInput()->UpdateBooleanComponent(m_aClickHandle, input->aClick, 0);
            vr::VRDriverInput()->UpdateBooleanComponent(m_aTouchHandle, input->aTouch, 0);
            vr::VRDriverInput()->UpdateBooleanComponent(m_bClickHandle, input->bClick, 0);
            vr::VRDriverInput()->UpdateBooleanComponent(m_bTouchHandle, input->bTouch, 0);
            vr::VRDriverInput()->UpdateBooleanComponent(m_systemClickHandle, input->systemClick, 0);
            vr::VRDriverInput()->UpdateBooleanComponent(m_menuClickHandle, input->menuClick, 0);
        }
        else
        {
            break; // Channel closed
        }
    }
}

void ControllerDriver::PoseUpdateThreadFunc(std::stop_token st)
{
    // Initialize with T-pose hand position
    vr::DriverPose_t pose = {};
    pose.poseIsValid = true;
    pose.result = vr::TrackingResult_Running_OK;
    pose.deviceIsConnected = true;
    pose.qWorldFromDriverRotation.w = 1.0;
    pose.qDriverFromHeadRotation.w = 1.0;
    pose.vecPosition[0] = (m_role == vr::TrackedControllerRole_LeftHand) ? -0.67 : 0.67;
    pose.vecPosition[1] = 1.41;
    pose.vecPosition[2] = 0.0;
    pose.qRotation.w = 1.0;

    while (!st.stop_requested())
    {
        // Check for new pose (non-blocking)
        if (auto p = m_poseReceiver.try_recv())
        {
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
        }

        // Always send current pose
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_deviceIndex, pose, sizeof(vr::DriverPose_t));

        std::this_thread::sleep_for(std::chrono::milliseconds(11)); // ~90Hz
    }
}

void ControllerDriver::Deactivate()
{
    if (m_inputThread.joinable())
    {
        m_inputThread.request_stop();
        m_inputThread.join();
    }
    if (m_poseThread.joinable())
    {
        m_poseThread.request_stop();
        m_poseThread.join();
    }
    m_deviceIndex = vr::k_unTrackedDeviceIndexInvalid;
}

void ControllerDriver::EnterStandby()
{
}

void* ControllerDriver::GetComponent(const char* pchComponentNameAndVersion)
{
    return nullptr;
}

void ControllerDriver::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize >= 1)
        pchResponseBuffer[0] = 0;
}

vr::DriverPose_t ControllerDriver::GetPose()
{
    // Deprecated - poses are pushed via TrackedDevicePoseUpdated
    vr::DriverPose_t pose = {};
    pose.poseIsValid = false;
    return pose;
}
