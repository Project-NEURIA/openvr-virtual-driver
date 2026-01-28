#include <vector>
#include <optional>
#include <d3d11.h>
#include <wrl/client.h>
#include <winsock2.h>
#include <ws2tcpip.h>

enum class MsgType : uint32_t {
    Frame = 0,
    Position = 1
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


class SocketManager 
{
public:
    SocketManager();
    ~SocketManager();
    Init();
    std::optional<Position> GetNextPosition();
    bool SendFrame(const Frame& frame);

private:
    void SocketManager::Receive(std::stop_token st);

    std::vector<Position> pos;
    SOCKET listenSocket;
    SOCKET clientSocket;

    std::jthread receiverThread;
    std::mutex mtx;
    std::mutex sendMtx;
};
