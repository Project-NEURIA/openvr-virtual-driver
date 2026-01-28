#include <vector>
#include <optional>
#include <d3d11.h>
#include <wrl/client.h>
#include <winsock2.h>
#include <ws2tcpip.h>

struct PositionDTO {
    // Something here
};


class SocketManager 
{
public:
    SocketManager();
    ~SocketManager();
    Init();
    std::optional<PositionDTO> GetNextPosition();
    void SendFrameData(ID3D11Texture2D* pTexture, uint32_t eye, const vr::VRTextureBounds_t& bounds);

private:
    void SocketManager::Receive(std::stop_token st);

    std::vector<PositionDTO> pos;
    SOCKET listenSocket;
    SOCKET clientSocket;

    std::jthread receiverThread;
    std::mutex mtx;
};
