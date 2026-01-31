#include "socket_manager.h"

SocketManager::SocketManager(
    mpsc::Sender<Position> positionSender,
    mpsc::Sender<ControllerInput> leftControllerSender,
    mpsc::Sender<ControllerInput> rightControllerSender,
    TrackerSenders trackerSenders
) :
    m_positionSender(std::move(positionSender)),
    m_leftControllerSender(std::move(leftControllerSender)),
    m_rightControllerSender(std::move(rightControllerSender)),
    m_trackerSenders(std::move(trackerSenders)),
    listenSocket(INVALID_SOCKET),
    clientSocket(INVALID_SOCKET)
{}

SocketManager::~SocketManager()
{
    closesocket(clientSocket);
    closesocket(listenSocket);
    WSACleanup();
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
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(21213);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        return std::unexpected("bind failed");
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        return std::unexpected("listen failed");
    }

    connectionThread = std::jthread([this](std::stop_token st) { Connect(st); });

    return 0;
}

void SocketManager::Connect(std::stop_token st)
{
    while (!st.stop_requested())
    {
        clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET)
            continue;

        connected = true;

        receiverThread = std::jthread([this](std::stop_token st) { Receive(st); });
        receiverThread.join();

        connected = false;
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
    }
}

void SocketManager::Receive(std::stop_token st)
{
    while (!st.stop_requested() && connected)
    {
        MsgHeader msgHeader;
        int bytes = recv(clientSocket, reinterpret_cast<char*>(&msgHeader), sizeof(msgHeader), MSG_WAITALL);
        if (bytes <= 0)
            break;

        if (msgHeader.type == MsgType::Position && msgHeader.size == sizeof(Position))
        {
            Position position;
            bytes = recv(clientSocket, reinterpret_cast<char*>(&position), sizeof(Position), MSG_WAITALL);
            if (bytes <= 0)
                break;

            m_positionSender.send(position);
        }
        else if (msgHeader.type == MsgType::Controller && msgHeader.size == sizeof(ControllerInput))
        {
            ControllerInput input;
            bytes = recv(clientSocket, reinterpret_cast<char*>(&input), sizeof(ControllerInput), MSG_WAITALL);
            if (bytes <= 0)
                break;

            m_leftControllerSender.send(input);
            m_rightControllerSender.send(input);
        }
        else if (msgHeader.type == MsgType::BodyPose && msgHeader.size == sizeof(BodyPose))
        {
            BodyPose bodyPose;
            bytes = recv(clientSocket, reinterpret_cast<char*>(&bodyPose), sizeof(BodyPose), MSG_WAITALL);
            if (bytes <= 0)
                break;

            m_trackerSenders.waist.send(bodyPose.waist);
            m_trackerSenders.chest.send(bodyPose.chest);
            m_trackerSenders.leftFoot.send(bodyPose.leftFoot);
            m_trackerSenders.rightFoot.send(bodyPose.rightFoot);
            m_trackerSenders.leftKnee.send(bodyPose.leftKnee);
            m_trackerSenders.rightKnee.send(bodyPose.rightKnee);
            m_trackerSenders.leftElbow.send(bodyPose.leftElbow);
            m_trackerSenders.rightElbow.send(bodyPose.rightElbow);
            m_trackerSenders.leftShoulder.send(bodyPose.leftShoulder);
            m_trackerSenders.rightShoulder.send(bodyPose.rightShoulder);
        }
    }
}

bool SocketManager::SendFrame(const Frame& frame)
{
    if (!connected)
        return false;

    std::lock_guard<std::mutex> lock(sendMtx);

    uint32_t pixelDataSize = frame.width * frame.height * 4;

    MsgHeader msgHeader { MsgType::Frame, static_cast<uint32_t>(12 + pixelDataSize) };
    if (send(clientSocket, reinterpret_cast<const char*>(&msgHeader), sizeof(msgHeader), 0) == SOCKET_ERROR)
    {
        return false;
    }

    uint32_t frameInfo[3] = { frame.width, frame.height, frame.eye };
    if (send(clientSocket, reinterpret_cast<const char*>(frameInfo), sizeof(frameInfo), 0) == SOCKET_ERROR)
    {
        return false;
    }

    if (send(clientSocket, reinterpret_cast<const char*>(frame.data), pixelDataSize, 0) == SOCKET_ERROR)
    {
        return false;
    }

    return true;
}
