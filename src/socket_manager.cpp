#include "socket_manager.h"
#include <expected>
#include <algorithm>

SocketManager::SocketManager() :
    listenSocket(INVALID_SOCKET),
    clientSocket(INVALID_SOCKET),
    receiverThread(&SocketManager::Receive, this)
    {}

SocketManager::~SocketManager()
{
    closesocket(clientSocket);
    closesocket(listenSocket);
    WSACleanup();
    return 0;
}

std::expected<int, std::string> SocketManager::Init()
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) 
    {
        return std::unexpected("WSAStartup failed");
    }

    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) 
    {
        return std::unexpected("socket failed");
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (listenSocket == INVALID_SOCKET) 
    {
        return std::unexpected("WSAStartup failed");
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(21213);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) 
    {
        return std::unexpected("WSAStartup failed");
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) 
    {
        return std::unexpected("WSAStartup failed");
    }

    std::cout << "Listening on port 21213...\n";

    clientSocket = accept(listenSocket, nullptr, nullptr);
    if (clientSocket == INVALID_SOCKET) 
    {
        return std::unexpected("WSAStartup failed");
    }

    std::cout << "Client connected!\n";
    
    return 0;
}

void SocketManager::Receive(std::stop_token st) 
{
    while (!st.stop_requested()) 
    {
        char buffer[1024];
        int bytes = recv(clientSocket, buffer, sizeof(buffer), 0);

        PositionDTO dto = json::parse(std::string(buffer, bytes));
        
        std::unique_lock lock(mtx);
        pos.push_back(dto);
        lock.unlock();
    }
}

std::optional<PositionDTO> SocketManager::GetNextPosition()
{
    std::unique_lock lock(mtx);
    if (pos.empty()) 
    {
        return std::nullopt;
    }

    PositionDTO p = pos.front();
    pos.erase(pos.begin());
    lock.unlock();

    return p;
}

bool SocketManager::SendFrame(const PixelData& pixels)
{
    uint32_t header[3] = { pixels.width, pixels.height, pixels.eye };

    std::lock_guard<std::mutex> lock(sendMtx);

    if (send(clientSocket, reinterpret_cast<const char*>(header), sizeof(header), 0) == SOCKET_ERROR)
    {
        return false;
    }

    uint32_t totalSize = pixels.width * pixels.height * 4;
    if (send(clientSocket, reinterpret_cast<const char*>(pixels.data), totalSize, 0) == SOCKET_ERROR)
    {
        return false;
    }

    return true;
}

