#pragma once

#include <openvr_driver.h>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include "socket_manager.h"

class ControllerDriver : public vr::ITrackedDeviceServerDriver
{
public:
    ControllerDriver(vr::ETrackedControllerRole role);
    ~ControllerDriver() = default;

    // ITrackedDeviceServerDriver interface
    vr::EVRInitError Activate(uint32_t unObjectId) override;
    void Deactivate() override;
    void EnterStandby() override;
    void* GetComponent(const char* pchComponentNameAndVersion) override;
    void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override;
    vr::DriverPose_t GetPose() override;

    // Public methods
    const char* GetSerialNumber() const { return m_serialNumber.c_str(); }
    void UpdateInput(const struct ControllerInput& input);
    void RunFrame();

private:
    void PoseUpdateThread();

    uint32_t m_deviceIndex = vr::k_unTrackedDeviceIndexInvalid;
    vr::ETrackedControllerRole m_role;
    std::string m_serialNumber;

    // Input component handles - Joystick
    vr::VRInputComponentHandle_t m_joystickXHandle = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_joystickYHandle = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_joystickClickHandle = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_joystickTouchHandle = vr::k_ulInvalidInputComponentHandle;
    // Input component handles - Trigger
    vr::VRInputComponentHandle_t m_triggerValueHandle = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_triggerClickHandle = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_triggerTouchHandle = vr::k_ulInvalidInputComponentHandle;
    // Input component handles - Grip
    vr::VRInputComponentHandle_t m_gripValueHandle = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_gripClickHandle = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_gripTouchHandle = vr::k_ulInvalidInputComponentHandle;
    // Input component handles - Buttons
    vr::VRInputComponentHandle_t m_aClickHandle = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_aTouchHandle = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_bClickHandle = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_bTouchHandle = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_systemClickHandle = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_menuClickHandle = vr::k_ulInvalidInputComponentHandle;
    // Haptic
    vr::VRInputComponentHandle_t m_hapticHandle = vr::k_ulInvalidInputComponentHandle;

    // Input state
    ControllerInput m_inputState{};
    std::mutex m_inputMutex;

    // Thread
    std::atomic<bool> m_isActive{false};
    std::jthread m_poseThread;
};
