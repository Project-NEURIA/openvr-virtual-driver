#pragma once

#include <optional>
#include <expected>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <d3d11.h>
#include <wrl/client.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "../mpsc/channel.h"

enum class MsgType : uint32_t {
    Frame = 0,
    Position = 1,
    Controller = 2,
    BodyPose = 3
};

struct MsgHeader {
    MsgType type;
    uint32_t size;
};

struct Position {
    double x;
    double y;
    double z;
    double qw;
    double qx;
    double qy;
    double qz;
};

struct Frame {
    const uint8_t* data;
    uint32_t width;
    uint32_t height;
    uint32_t eye;
};

#pragma pack(push, 1)
struct ControllerInput {
    // Joystick
    float joystickX;
    float joystickY;
    uint8_t joystickClick;
    uint8_t joystickTouch;
    // Trigger
    float trigger;
    uint8_t triggerClick;
    uint8_t triggerTouch;
    // Grip
    float grip;
    uint8_t gripClick;
    uint8_t gripTouch;
    // Buttons
    uint8_t aClick;
    uint8_t aTouch;
    uint8_t bClick;
    uint8_t bTouch;
    uint8_t systemClick;
    uint8_t menuClick;
    // Right controller rotation (radians)
    float rightYaw;
    float rightPitch;
};

struct Pose {
    float posX, posY, posZ;
    float rotW, rotX, rotY, rotZ;  // quaternion
};

struct BodyPose {
    // Controllers (hands)
    Pose leftHand;
    Pose rightHand;
    // Trackers
    Pose waist;
    Pose chest;
    Pose leftFoot;
    Pose rightFoot;
    Pose leftKnee;
    Pose rightKnee;
    Pose leftElbow;
    Pose rightElbow;
    Pose leftShoulder;
    Pose rightShoulder;
};
#pragma pack(pop)

struct TrackerSenders
{
    mpsc::Sender<Pose> waist;
    mpsc::Sender<Pose> chest;
    mpsc::Sender<Pose> leftFoot;
    mpsc::Sender<Pose> rightFoot;
    mpsc::Sender<Pose> leftKnee;
    mpsc::Sender<Pose> rightKnee;
    mpsc::Sender<Pose> leftElbow;
    mpsc::Sender<Pose> rightElbow;
    mpsc::Sender<Pose> leftShoulder;
    mpsc::Sender<Pose> rightShoulder;
};

class SocketManager
{
public:
    SocketManager(
        mpsc::Sender<Position> positionSender,
        mpsc::Sender<ControllerInput> leftControllerInputSender,
        mpsc::Sender<ControllerInput> rightControllerInputSender,
        mpsc::Sender<Pose> leftHandPoseSender,
        mpsc::Sender<Pose> rightHandPoseSender,
        TrackerSenders trackerSenders
    );
    ~SocketManager();
    std::expected<int, std::string> Init();
    bool SendFrame(const Frame& frame);

private:
    void Connect(std::stop_token st);
    void Receive(std::stop_token st);

    // Channel senders
    mpsc::Sender<Position> m_positionSender;
    mpsc::Sender<ControllerInput> m_leftControllerInputSender;
    mpsc::Sender<ControllerInput> m_rightControllerInputSender;
    mpsc::Sender<Pose> m_leftHandPoseSender;
    mpsc::Sender<Pose> m_rightHandPoseSender;
    TrackerSenders m_trackerSenders;

    SOCKET listenSocket;
    SOCKET clientSocket;

    std::jthread connectionThread;
    std::jthread receiverThread;
    std::atomic<bool> connected{false};
    std::mutex sendMtx;
};
