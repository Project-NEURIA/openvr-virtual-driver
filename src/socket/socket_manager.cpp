#include "socket_manager.h"

SocketManager::SocketManager(
    mpsc::Sender<Pose> headPoseSender,
    mpsc::Sender<ControllerInput> leftControllerInputSender,
    mpsc::Sender<ControllerInput> rightControllerInputSender,
    mpsc::Sender<Pose> leftHandPoseSender,
    mpsc::Sender<Pose> rightHandPoseSender,
    TrackerSenders trackerSenders
) :
    m_headPoseSender(std::move(headPoseSender)),
    m_leftControllerInputSender(std::move(leftControllerInputSender)),
    m_rightControllerInputSender(std::move(rightControllerInputSender)),
    m_leftHandPoseSender(std::move(leftHandPoseSender)),
    m_rightHandPoseSender(std::move(rightHandPoseSender)),
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

        if (msgHeader.type == MsgType::BodyPosition && msgHeader.size == sizeof(BodyPosition))
        {
            BodyPosition bodyPos;
            bytes = recv(clientSocket, reinterpret_cast<char*>(&bodyPos), sizeof(BodyPosition), MSG_WAITALL);
            if (bytes <= 0)
                break;

            // Send poses only if not null (all zeros means skip update)
            if (!bodyPos.head.isNull())
                m_headPoseSender.send(bodyPos.head);
            if (!bodyPos.leftHand.isNull())
                m_leftHandPoseSender.send(bodyPos.leftHand);
            if (!bodyPos.rightHand.isNull())
                m_rightHandPoseSender.send(bodyPos.rightHand);
            if (!bodyPos.waist.isNull())
                m_trackerSenders.waist.send(bodyPos.waist);
            if (!bodyPos.chest.isNull())
                m_trackerSenders.chest.send(bodyPos.chest);
            if (!bodyPos.leftFoot.isNull())
                m_trackerSenders.leftFoot.send(bodyPos.leftFoot);
            if (!bodyPos.rightFoot.isNull())
                m_trackerSenders.rightFoot.send(bodyPos.rightFoot);
            if (!bodyPos.leftKnee.isNull())
                m_trackerSenders.leftKnee.send(bodyPos.leftKnee);
            if (!bodyPos.rightKnee.isNull())
                m_trackerSenders.rightKnee.send(bodyPos.rightKnee);
            if (!bodyPos.leftElbow.isNull())
                m_trackerSenders.leftElbow.send(bodyPos.leftElbow);
            if (!bodyPos.rightElbow.isNull())
                m_trackerSenders.rightElbow.send(bodyPos.rightElbow);
            if (!bodyPos.leftShoulder.isNull())
                m_trackerSenders.leftShoulder.send(bodyPos.leftShoulder);
            if (!bodyPos.rightShoulder.isNull())
                m_trackerSenders.rightShoulder.send(bodyPos.rightShoulder);
        }
        else if (msgHeader.type == MsgType::Controller && msgHeader.size == sizeof(ControllerInput))
        {
            ControllerInput input;
            bytes = recv(clientSocket, reinterpret_cast<char*>(&input), sizeof(ControllerInput), MSG_WAITALL);
            if (bytes <= 0)
                break;

            m_leftControllerInputSender.send(input);
            m_rightControllerInputSender.send(input);
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
