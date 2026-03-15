#pragma once

#include <optional>
#include <expected>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
using SOCKET = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

#include "../mpsc/channel.h"

enum class MsgType : uint32_t {
    Frame = 0,
    BodyPosition = 1,
    Controller = 2,
    FrameStart = 3,
    FrameStop = 4
};

struct ClientConnection {
    SOCKET socket;
    std::jthread receiverThread;
    bool streaming = false;
};

struct MsgHeader {
    MsgType type;
    uint32_t size;
};

#pragma pack(push, 1)
struct Pose {
    float posX, posY, posZ;
    float rotW, rotX, rotY, rotZ;  // quaternion

    bool isNull() const {
        return posX == 0.0f && posY == 0.0f && posZ == 0.0f &&
               rotW == 0.0f && rotX == 0.0f && rotY == 0.0f && rotZ == 0.0f;
    }
};

struct ControllerInput {
    // 0 = left, 1 = right
    uint8_t hand;
    // Thumbstick
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
};

struct BodyPosition {
    // HMD
    Pose head;
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

struct Frame {
    uint32_t width;
    uint32_t height;
    Pose pose;
    const uint8_t* leftData;
    const uint8_t* rightData;
};

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
        mpsc::Sender<Pose> headPoseSender,
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
    void Receive(std::stop_token st, ClientConnection* client);

    // Channel senders
    mpsc::Sender<Pose> m_headPoseSender;
    mpsc::Sender<ControllerInput> m_leftControllerInputSender;
    mpsc::Sender<ControllerInput> m_rightControllerInputSender;
    mpsc::Sender<Pose> m_leftHandPoseSender;
    mpsc::Sender<Pose> m_rightHandPoseSender;
    TrackerSenders m_trackerSenders;

    SOCKET listenSocket;

    std::jthread connectionThread;
    std::vector<std::unique_ptr<ClientConnection>> m_clients;
    std::mutex m_clientsMtx;
};
