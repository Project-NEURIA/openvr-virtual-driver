#pragma once

#include <openvr_driver.h>
#include <memory>
#include "hmd_device_driver.h"

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
};
