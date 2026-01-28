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
        // Read message header
        MsgHeader msgHeader;
        int bytes = recv(clientSocket, reinterpret_cast<char*>(&msgHeader), sizeof(msgHeader), MSG_WAITALL);
        if (bytes != sizeof(msgHeader))
            continue;

        if (msgHeader.type == MsgType::Position && msgHeader.size == sizeof(Position))
        {
            Position position;
            bytes = recv(clientSocket, reinterpret_cast<char*>(&position), sizeof(Position), MSG_WAITALL);
            if (bytes != sizeof(Position))
                continue;

            std::unique_lock lock(mtx);
            pos.push_back(position);
            lock.unlock();
        }
    }
}

std::optional<Position> SocketManager::GetNextPosition()
{
    std::unique_lock lock(mtx);
    if (pos.empty()) 
    {
        return std::nullopt;
    }

    Position p = pos.front();
    pos.erase(pos.begin());
    lock.unlock();

    return p;
}

bool SocketManager::SendFrame(const Frame& frame)
{
    std::lock_guard<std::mutex> lock(sendMtx);

    uint32_t pixelDataSize = frame.width * frame.height * 4;

    // Send message header
    MsgHeader msgHeader { MsgType::Frame, static_cast<uint32_t>(12 + pixelDataSize) };
    if (send(clientSocket, reinterpret_cast<const char*>(&msgHeader), sizeof(msgHeader), 0) == SOCKET_ERROR)
    {
        return false;
    }

    // Send frame info (width, height, eye)
    uint32_t frameInfo[3] = { frame.width, frame.height, frame.eye };
    if (send(clientSocket, reinterpret_cast<const char*>(frameInfo), sizeof(frameInfo), 0) == SOCKET_ERROR)
    {
        return false;
    }

    // Send pixel data
    if (send(clientSocket, reinterpret_cast<const char*>(frame.data), pixelDataSize, 0) == SOCKET_ERROR)
    {
        return false;
    }

    return true;
}

