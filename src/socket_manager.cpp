#include "socket_manager.h"
#include <expected>

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

void Driver::SendFrameData(ID3D11Texture2D* pTexture, uint32_t eye, const vr::VRTextureBounds_t& bounds)
{
    if (!m_tcpConnected || !pTexture)
        return;

    // Get texture description
    D3D11_TEXTURE2D_DESC desc;
    pTexture->GetDesc(&desc);

    // Create or recreate staging texture if needed (must match source format)
    if (!m_pStagingTexture || m_stagingWidth != desc.Width || m_stagingHeight != desc.Height || m_stagingFormat != desc.Format)
    {
        m_pStagingTexture.Reset();

        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = desc.Width;
        stagingDesc.Height = desc.Height;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        // Use a compatible non-typeless format for staging
        if (desc.Format == DXGI_FORMAT_R10G10B10A2_TYPELESS)
            stagingDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        else if (desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
            stagingDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        else
            stagingDesc.Format = desc.Format;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        HRESULT hr = m_pD3DDevice->CreateTexture2D(&stagingDesc, nullptr, &m_pStagingTexture);
        if (FAILED(hr))
            return;

        m_stagingWidth = desc.Width;
        m_stagingHeight = desc.Height;
        m_stagingFormat = desc.Format;
    }

    // Copy texture to staging
    m_pD3DContext->CopyResource(m_pStagingTexture.Get(), pTexture);

    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_pD3DContext->Map(m_pStagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
        return;

    // Calculate cropped region from bounds (UV coordinates to pixels)
    uint32_t cropX = static_cast<uint32_t>(bounds.uMin * m_stagingWidth);
    uint32_t cropY = static_cast<uint32_t>(bounds.vMin * m_stagingHeight);
    uint32_t cropW = static_cast<uint32_t>((bounds.uMax - bounds.uMin) * m_stagingWidth);
    uint32_t cropH = static_cast<uint32_t>((bounds.vMax - bounds.vMin) * m_stagingHeight);

    // Ensure minimum size and clamp to texture dimensions
    if (cropW == 0) cropW = m_stagingWidth;
    if (cropH == 0) cropH = m_stagingHeight;
    if (cropX + cropW > m_stagingWidth) cropW = m_stagingWidth - cropX;
    if (cropY + cropH > m_stagingHeight) cropH = m_stagingHeight - cropY;

    // Send header with cropped dimensions
    uint32_t header[3] = { cropW, cropH, eye };

    std::lock_guard<std::mutex> lock(m_socketMutex);

    int sent = send(m_socket, reinterpret_cast<char*>(header), sizeof(header), 0);
    if (sent == SOCKET_ERROR)
    {
        m_pD3DContext->Unmap(m_pStagingTexture.Get(), 0);
        m_tcpConnected = false;
        return;
    }

    // Send cropped pixel data row by row
    uint32_t rowSize = cropW * 4; // 4 bytes per pixel
    uint8_t* srcData = static_cast<uint8_t*>(mapped.pData);

    for (uint32_t y = 0; y < cropH; y++)
    {
        uint8_t* rowStart = srcData + (cropY + y) * mapped.RowPitch + cropX * 4;
        sent = send(m_socket, reinterpret_cast<char*>(rowStart), rowSize, 0);
        if (sent == SOCKET_ERROR)
        {
            m_tcpConnected = false;
            break;
        }
    }

    m_pD3DContext->Unmap(m_pStagingTexture.Get(), 0);
}

