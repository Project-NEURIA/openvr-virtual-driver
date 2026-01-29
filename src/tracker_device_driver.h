#pragma once

#include <openvr_driver.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include "socket_manager.h"

enum class TrackerRole
{
    Waist,
    Chest,
    LeftFoot,
    RightFoot,
    LeftKnee,
    RightKnee,
    LeftElbow,
    RightElbow,
    LeftShoulder,
    RightShoulder
};

class TrackerDriver : public vr::ITrackedDeviceServerDriver
{
public:
    TrackerDriver(TrackerRole role);

    // ITrackedDeviceServerDriver
    vr::EVRInitError Activate(uint32_t unObjectId) override;
    void Deactivate() override;
    void EnterStandby() override;
    void* GetComponent(const char* pchComponentNameAndVersion) override;
    void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override;
    vr::DriverPose_t GetPose() override;

    const char* GetSerialNumber() const { return m_serialNumber.c_str(); }
    void UpdatePose(const Pose& pose);

private:
    TrackerRole m_role;
    std::string m_serialNumber;
    uint32_t m_deviceIndex = vr::k_unTrackedDeviceIndexInvalid;
    std::atomic<bool> m_isActive{false};
    std::jthread m_poseThread;

    Pose m_currentPose{};
    std::mutex m_poseMutex;
};
