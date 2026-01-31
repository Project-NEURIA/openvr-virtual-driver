#include "device_provider.h"
#include "../hmd/hmd_device_driver.h"
#include "../controller/controller_device_driver.h"
#include "../tracker/tracker_device_driver.h"
#include "../mpsc/channel.h"

vr::EVRInitError AIVRDeviceProvider::Init(vr::IVRDriverContext* pDriverContext)
{
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

    // Create channels for position (HMD)
    auto [positionSender, positionReceiver] = mpsc::channel<Position>();

    // Create channels for controller inputs
    auto [leftControllerInputTx, leftControllerInputRx] = mpsc::channel<ControllerInput>();
    auto [rightControllerInputTx, rightControllerInputRx] = mpsc::channel<ControllerInput>();

    // Create channels for hand poses (from BodyPose)
    auto [leftHandPoseTx, leftHandPoseRx] = mpsc::channel<Pose>();
    auto [rightHandPoseTx, rightHandPoseRx] = mpsc::channel<Pose>();

    // Create channels for trackers
    auto [waistTx, waistRx] = mpsc::channel<Pose>();
    auto [chestTx, chestRx] = mpsc::channel<Pose>();
    auto [leftFootTx, leftFootRx] = mpsc::channel<Pose>();
    auto [rightFootTx, rightFootRx] = mpsc::channel<Pose>();
    auto [leftKneeTx, leftKneeRx] = mpsc::channel<Pose>();
    auto [rightKneeTx, rightKneeRx] = mpsc::channel<Pose>();
    auto [leftElbowTx, leftElbowRx] = mpsc::channel<Pose>();
    auto [rightElbowTx, rightElbowRx] = mpsc::channel<Pose>();
    auto [leftShoulderTx, leftShoulderRx] = mpsc::channel<Pose>();
    auto [rightShoulderTx, rightShoulderRx] = mpsc::channel<Pose>();

    // Create socket manager with all senders
    m_pSocketManager = std::make_unique<SocketManager>(
        std::move(positionSender),
        std::move(leftControllerInputTx),
        std::move(rightControllerInputTx),
        std::move(leftHandPoseTx),
        std::move(rightHandPoseTx),
        TrackerSenders{
            std::move(waistTx),
            std::move(chestTx),
            std::move(leftFootTx),
            std::move(rightFootTx),
            std::move(leftKneeTx),
            std::move(rightKneeTx),
            std::move(leftElbowTx),
            std::move(rightElbowTx),
            std::move(leftShoulderTx),
            std::move(rightShoulderTx)
        }
    );

    // Create HMD with position receiver
    m_pHmd = std::make_unique<Driver>(std::move(positionReceiver), m_pSocketManager.get());

    if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
            m_pHmd->GetSerialNumber(),
            vr::TrackedDeviceClass_HMD,
            m_pHmd.get()))
    {
        return vr::VRInitError_Driver_Unknown;
    }

    // Add left controller
    m_pLeftController = std::make_unique<ControllerDriver>(
        vr::TrackedControllerRole_LeftHand,
        std::move(leftControllerInputRx),
        std::move(leftHandPoseRx)
    );
    if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
            m_pLeftController->GetSerialNumber(),
            vr::TrackedDeviceClass_Controller,
            m_pLeftController.get()))
    {
        return vr::VRInitError_Driver_Unknown;
    }

    // Add right controller
    m_pRightController = std::make_unique<ControllerDriver>(
        vr::TrackedControllerRole_RightHand,
        std::move(rightControllerInputRx),
        std::move(rightHandPoseRx)
    );
    if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
            m_pRightController->GetSerialNumber(),
            vr::TrackedDeviceClass_Controller,
            m_pRightController.get()))
    {
        return vr::VRInitError_Driver_Unknown;
    }

    // Add body trackers
    struct TrackerInit { TrackerRole role; mpsc::Receiver<Pose> receiver; };
    TrackerInit trackerInits[] = {
        { TrackerRole::Waist, std::move(waistRx) },
        { TrackerRole::Chest, std::move(chestRx) },
        { TrackerRole::LeftFoot, std::move(leftFootRx) },
        { TrackerRole::RightFoot, std::move(rightFootRx) },
        { TrackerRole::LeftKnee, std::move(leftKneeRx) },
        { TrackerRole::RightKnee, std::move(rightKneeRx) },
        { TrackerRole::LeftElbow, std::move(leftElbowRx) },
        { TrackerRole::RightElbow, std::move(rightElbowRx) },
        { TrackerRole::LeftShoulder, std::move(leftShoulderRx) },
        { TrackerRole::RightShoulder, std::move(rightShoulderRx) }
    };

    for (size_t i = 0; i < m_trackers.size(); ++i)
    {
        m_trackers[i] = std::make_unique<TrackerDriver>(
            trackerInits[i].role,
            std::move(trackerInits[i].receiver)
        );
        if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
                m_trackers[i]->GetSerialNumber(),
                vr::TrackedDeviceClass_GenericTracker,
                m_trackers[i].get()))
        {
            return vr::VRInitError_Driver_Unknown;
        }
    }

    // Initialize socket manager (starts listening)
    m_pSocketManager->Init();

    return vr::VRInitError_None;
}

void AIVRDeviceProvider::Cleanup()
{
    // Reset socket manager first - this closes channels and stops threads
    m_pSocketManager.reset();

    // Then reset devices (their receiver threads will exit when channels close)
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
    // Poll events only - pose/input updates happen via channel threads
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
