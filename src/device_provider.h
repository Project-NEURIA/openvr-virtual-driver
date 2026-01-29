#pragma once

#include <openvr_driver.h>
#include <memory>
#include <array>
#include "hmd_device_driver.h"
#include "controller_device_driver.h"
#include "tracker_device_driver.h"

class AIVRDeviceProvider : public vr::IServerTrackedDeviceProvider
{
public:
    vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;
    void Cleanup() override;
    const char* const* GetInterfaceVersions() override;
    void RunFrame() override;
    bool ShouldBlockStandbyMode() override;
    void EnterStandby() override;
    void LeaveStandby() override;

private:
    std::unique_ptr<Driver> m_pHmd;
    std::unique_ptr<ControllerDriver> m_pLeftController;
    std::unique_ptr<ControllerDriver> m_pRightController;

    // Body trackers (10 total: waist, chest, feet, knees, elbows, shoulders)
    std::array<std::unique_ptr<TrackerDriver>, 10> m_trackers;
};
