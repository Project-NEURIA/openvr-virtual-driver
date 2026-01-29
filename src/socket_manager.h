#pragma once

#include <vector>
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

enum class MsgType : uint32_t {
    Frame = 0,
    Position = 1,
    Controller = 2
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
#pragma pack(pop)


class SocketManager 
{
public:
    SocketManager();
    ~SocketManager();
    std::expected<int, std::string> Init();
    std::optional<Position> GetNextPosition();
    std::optional<ControllerInput> GetNextControllerInput();
    bool SendFrame(const Frame& frame);

private:
    void Connect(std::stop_token st);
    void Receive(std::stop_token st);

    std::vector<Position> pos;
    std::vector<ControllerInput> controllerInputs;
    SOCKET listenSocket;
    SOCKET clientSocket;

    std::jthread connectionThread;
    std::jthread receiverThread;
    std::atomic<bool> connected{false};
    std::mutex mtx;
    std::mutex sendMtx;
};
