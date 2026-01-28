#include <vector>
#include <optional>
#include <d3d11.h>
#include <wrl/client.h>
#include <winsock2.h>
#include <ws2tcpip.h>

struct PositionDTO {
    // Something here
};

struct PixelData {
    const uint8_t* data;  // Contiguous pixel data (no padding)
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
    std::optional<PositionDTO> GetNextPosition();
    bool SendFrame(const PixelData& pixels);

private:
    void SocketManager::Receive(std::stop_token st);

    std::vector<PositionDTO> pos;
    SOCKET listenSocket;
    SOCKET clientSocket;

    std::jthread receiverThread;
    std::mutex mtx;
    std::mutex sendMtx;
};
