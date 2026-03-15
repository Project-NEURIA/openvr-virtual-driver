#include "socket_manager.h"
#include <openvr_driver.h>
#include <cstdio>

static void Log(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (vr::VRDriverLog())
        vr::VRDriverLog()->Log(buf);
}

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
    listenSocket(INVALID_SOCKET)
{}

SocketManager::~SocketManager()
{
    // Close listen socket first so accept() stops blocking
    closesocket(listenSocket);

    // Close all client sockets to unblock recv() in Receive threads
    {
        std::lock_guard<std::mutex> lock(m_clientsMtx);
        for (auto& client : m_clients)
        {
            if (client->socket != INVALID_SOCKET)
                closesocket(client->socket);
            client->socket = INVALID_SOCKET;
        }
    }

    // Now safe to destroy clients — jthreads will join after recv returns
    m_clients.clear();
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
        SOCKET newSocket = accept(listenSocket, nullptr, nullptr);
        if (newSocket == INVALID_SOCKET)
            continue;

        Log("[OVD] Client connected\n");
        std::lock_guard<std::mutex> lock(m_clientsMtx);

        // Clean up dead clients (socket closed by their Receive thread)
        std::erase_if(m_clients, [](const std::unique_ptr<ClientConnection>& c) {
            return c->socket == INVALID_SOCKET;
        });

        auto client = std::make_unique<ClientConnection>();
        client->socket = newSocket;
        auto* clientPtr = client.get();
        client->receiverThread = std::jthread([this, clientPtr](std::stop_token st) {
            Receive(st, clientPtr);
        });
        m_clients.push_back(std::move(client));
    }
}

void SocketManager::Receive(std::stop_token st, ClientConnection* client)
{
    while (!st.stop_requested())
    {
        MsgHeader msgHeader;
        int bytes = recv(client->socket, reinterpret_cast<char*>(&msgHeader), sizeof(msgHeader), MSG_WAITALL);
        if (bytes <= 0)
        {
            int err = WSAGetLastError();
            Log("[OVD] Client recv header failed: bytes=%d err=%d\n", bytes, err);
            break;
        }

        if (msgHeader.type == MsgType::FrameStart && msgHeader.size == 0)
        {
            Log("[OVD] Client requested frame stream start\n");
            client->streaming = true;
        }
        else if (msgHeader.type == MsgType::FrameStop && msgHeader.size == 0)
        {
            Log("[OVD] Client requested frame stream stop\n");
            client->streaming = false;
        }
        else if (msgHeader.type == MsgType::BodyPosition && msgHeader.size == sizeof(BodyPosition))
        {
            BodyPosition bodyPos;
            bytes = recv(client->socket, reinterpret_cast<char*>(&bodyPos), sizeof(BodyPosition), MSG_WAITALL);
            if (bytes <= 0)
            {
                Log("[OVD] Client recv body failed: bytes=%d\n", bytes);
                break;
            }

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
            bytes = recv(client->socket, reinterpret_cast<char*>(&input), sizeof(ControllerInput), MSG_WAITALL);
            if (bytes <= 0)
            {
                Log("[OVD] Client recv controller failed: bytes=%d\n", bytes);
                break;
            }

            m_leftControllerInputSender.send(input);
            m_rightControllerInputSender.send(input);
        }
        else
        {
            Log("[OVD] Unknown msg type=%u size=%u, dropping client\n",
                static_cast<uint32_t>(msgHeader.type), msgHeader.size);
            break;
        }
    }

    // Client disconnected — close socket and mark for cleanup.
    // We can't erase ourselves from m_clients here because that would
    // destroy our own jthread (which tries to join the current thread = UB).
    Log("[OVD] Client disconnected\n");
    closesocket(client->socket);
    client->socket = INVALID_SOCKET;
    client->streaming = false;
}

bool SocketManager::SendFrame(const Frame& frame)
{
    std::lock_guard<std::mutex> lock(m_clientsMtx);

    // Clean up dead clients
    std::erase_if(m_clients, [](const std::unique_ptr<ClientConnection>& c) {
        return c->socket == INVALID_SOCKET;
    });

    if (m_clients.empty())
        return false;

    uint32_t pixelDataSize = frame.width * frame.height * 4;
    MsgHeader msgHeader { MsgType::Frame, static_cast<uint32_t>(8 + sizeof(Pose) + pixelDataSize * 2) };
    uint32_t frameInfo[2] = { frame.width, frame.height };

    bool anySent = false;

    for (auto it = m_clients.begin(); it != m_clients.end(); )
    {
        if (!(*it)->streaming)
        {
            ++it;
            continue;
        }

        SOCKET sock = (*it)->socket;
        bool failed = false;

        if (send(sock, reinterpret_cast<const char*>(&msgHeader), sizeof(msgHeader), 0) == SOCKET_ERROR)
            failed = true;

        if (!failed && send(sock, reinterpret_cast<const char*>(frameInfo), sizeof(frameInfo), 0) == SOCKET_ERROR)
            failed = true;

        if (!failed && send(sock, reinterpret_cast<const char*>(&frame.pose), sizeof(Pose), 0) == SOCKET_ERROR)
            failed = true;

        if (!failed && send(sock, reinterpret_cast<const char*>(frame.leftData), pixelDataSize, 0) == SOCKET_ERROR)
            failed = true;

        if (!failed && send(sock, reinterpret_cast<const char*>(frame.rightData), pixelDataSize, 0) == SOCKET_ERROR)
            failed = true;

        if (failed)
        {
            closesocket(sock);
            it = m_clients.erase(it);
        }
        else
        {
            anySent = true;
            ++it;
        }
    }

    return anySent;
}
