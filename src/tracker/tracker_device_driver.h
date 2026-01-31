#pragma once

#include <openvr_driver.h>
#include <string>
#include <thread>
#include "../socket/socket_manager.h"
#include "../mpsc/channel.h"

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
    TrackerDriver(TrackerRole role, mpsc::Receiver<Pose> poseReceiver);

    // ITrackedDeviceServerDriver
    vr::EVRInitError Activate(uint32_t unObjectId) override;
    void Deactivate() override;
    void EnterStandby() override;
    void* GetComponent(const char* pchComponentNameAndVersion) override;
    void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override;
    vr::DriverPose_t GetPose() override;

    const char* GetSerialNumber() const { return m_serialNumber.c_str(); }

private:
    TrackerRole m_role;
    std::string m_serialNumber;
    uint32_t m_deviceIndex = vr::k_unTrackedDeviceIndexInvalid;
    mpsc::Receiver<Pose> m_poseReceiver;
    std::jthread m_poseThread;
};
