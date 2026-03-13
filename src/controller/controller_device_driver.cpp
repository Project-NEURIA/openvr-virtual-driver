#include "controller_device_driver.h"

ControllerDriver::ControllerDriver(vr::ETrackedControllerRole role,
                                   mpsc::Receiver<ControllerInput> inputReceiver,
                                   mpsc::Receiver<Pose> poseReceiver)
    : m_role(role)
    , m_inputReceiver(std::move(inputReceiver))
    , m_poseReceiver(std::move(poseReceiver))
{
    m_serialNumber = (role == vr::TrackedControllerRole_LeftHand)
        ? "OVD-CTRL-LEFT" : "OVD-CTRL-RIGHT";
}

vr::EVRInitError ControllerDriver::Activate(uint32_t unObjectId)
{
    m_deviceIndex = unObjectId;

    vr::PropertyContainerHandle_t container = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_deviceIndex);

    vr::VRProperties()->SetStringProperty(container, vr::Prop_ModelNumber_String, "OVD Controller");
    vr::VRProperties()->SetStringProperty(container, vr::Prop_SerialNumber_String, m_serialNumber.c_str());
    vr::VRProperties()->SetInt32Property(container, vr::Prop_ControllerRoleHint_Int32, m_role);
    vr::VRProperties()->SetStringProperty(container, vr::Prop_ControllerType_String, "knuckles");
    vr::VRProperties()->SetStringProperty(container, vr::Prop_InputProfilePath_String, "{indexcontroller}/input/index_controller_profile.json");
    vr::VRProperties()->SetUint64Property(container, vr::Prop_CurrentUniverseId_Uint64, 2);

    // Thumbstick
    vr::VRDriverInput()->CreateScalarComponent(container, "/input/thumbstick/x", &m_joystickXHandle,
        vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    vr::VRDriverInput()->CreateScalarComponent(container, "/input/thumbstick/y", &m_joystickYHandle,
        vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/thumbstick/click", &m_joystickClickHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/thumbstick/touch", &m_joystickTouchHandle);

    // Trigger
    vr::VRDriverInput()->CreateScalarComponent(container, "/input/trigger/value", &m_triggerValueHandle,
        vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/trigger/click", &m_triggerClickHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/trigger/touch", &m_triggerTouchHandle);

    // Grip
    vr::VRDriverInput()->CreateScalarComponent(container, "/input/grip/value", &m_gripValueHandle,
        vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/grip/click", &m_gripClickHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/grip/touch", &m_gripTouchHandle);

    // Buttons
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/a/click", &m_aClickHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/a/touch", &m_aTouchHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/b/click", &m_bClickHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/b/touch", &m_bTouchHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/system/click", &m_systemClickHandle);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/application_menu/click", &m_menuClickHandle);

    // Haptic
    vr::VRDriverInput()->CreateHapticComponent(container, "/output/haptic", &m_hapticHandle);

    m_thread = std::jthread([this](std::stop_token st) { UpdateThreadFunc(st); });

    return vr::VRInitError_None;
}

void ControllerDriver::UpdateThreadFunc(std::stop_token st)
{
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
        // Poll input (non-blocking)
        if (auto input = m_inputReceiver.try_recv())
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

        // Poll external pose (non-blocking)
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

        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_deviceIndex, pose, sizeof(vr::DriverPose_t));

        std::this_thread::sleep_for(std::chrono::milliseconds(11)); // ~90Hz
    }
}

void ControllerDriver::Deactivate()
{
    if (m_thread.joinable())
    {
        m_thread.request_stop();
        m_thread.join();
    }
    m_deviceIndex = vr::k_unTrackedDeviceIndexInvalid;
}

void ControllerDriver::EnterStandby() {}

void* ControllerDriver::GetComponent(const char* pchComponentNameAndVersion) { return nullptr; }

void ControllerDriver::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize >= 1)
        pchResponseBuffer[0] = 0;
}

vr::DriverPose_t ControllerDriver::GetPose()
{
    vr::DriverPose_t pose = {};
    pose.poseIsValid = false;
    return pose;
}
