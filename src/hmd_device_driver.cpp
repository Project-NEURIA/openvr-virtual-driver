#include "hmd_device_driver.h"
#include <cstring>
#include <cmath>
#include <chrono>
#include <fstream>

#pragma comment(lib, "ws2_32.lib")

// Simple file logger for debugging
static void DebugLog(const char* msg)
{
    static std::ofstream logFile("C:\\Users\\4396k\\Desktop\\driver\\ai_vr_debug.log", std::ios::app);
    logFile << msg << std::endl;
    logFile.flush();
}

constexpr const char* TCP_HOST = "127.0.0.1";
constexpr uint16_t TCP_PORT = 8080;

AIVRHmdDriver::AIVRHmdDriver()
{
    InitD3D11();
    InitTCP();
}

AIVRHmdDriver::~AIVRHmdDriver()
{
    CleanupTCP();
    CleanupD3D11();
}

bool AIVRHmdDriver::InitD3D11()
{
    D3D_FEATURE_LEVEL featureLevel;
    UINT createDeviceFlags = 0;

#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &m_pD3DDevice,
        &featureLevel,
        &m_pD3DContext);

    return SUCCEEDED(hr);
}

void AIVRHmdDriver::CleanupD3D11()
{
    m_pStagingTexture.Reset();
    m_swapTextureSets.clear();
    m_pD3DContext.Reset();
    m_pD3DDevice.Reset();
}

bool AIVRHmdDriver::InitTCP()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return false;

    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET)
        return false;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_PORT);
    inet_pton(AF_INET, TCP_HOST, &addr.sin_addr);

    if (connect(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    m_tcpConnected = true;
    return true;
}

void AIVRHmdDriver::CleanupTCP()
{
    m_tcpConnected = false;
    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    WSACleanup();
}

void AIVRHmdDriver::SendFrameData(ID3D11Texture2D* pTexture, uint32_t eye, const vr::VRTextureBounds_t& bounds)
{
    if (!m_tcpConnected || !pTexture || !m_pD3DContext)
        return;

    // Get texture description
    D3D11_TEXTURE2D_DESC desc;
    pTexture->GetDesc(&desc);

    if (m_frameCount % 100 == 1)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "  SendFrameData eye=%d: %dx%d, format=%d, arraySize=%d, mipLevels=%d",
            eye, desc.Width, desc.Height, desc.Format, desc.ArraySize, desc.MipLevels);
        DebugLog(buf);
    }

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

    if (m_frameCount % 100 == 1)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "  Mapped: RowPitch=%u, crop=(%u,%u) %ux%u from %ux%u",
            mapped.RowPitch, cropX, cropY, cropW, cropH, m_stagingWidth, m_stagingHeight);
        DebugLog(buf);
    }

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

vr::EVRInitError AIVRHmdDriver::Activate(uint32_t unObjectId)
{
    m_unObjectId = unObjectId;

    // Get properties handle
    vr::PropertyContainerHandle_t props = vr::VRProperties()->TrackedDeviceToPropertyContainer(unObjectId);

    // Set HMD properties
    vr::VRProperties()->SetStringProperty(props, vr::Prop_ModelNumber_String, "AI VR HMD");
    vr::VRProperties()->SetStringProperty(props, vr::Prop_ManufacturerName_String, "AI VR");
    vr::VRProperties()->SetStringProperty(props, vr::Prop_TrackingSystemName_String, "AI VR Tracking");
    vr::VRProperties()->SetStringProperty(props, vr::Prop_SerialNumber_String, m_serialNumber.c_str());

    // Display properties
    vr::VRProperties()->SetFloatProperty(props, vr::Prop_DisplayFrequency_Float, m_displayFrequency);
    vr::VRProperties()->SetFloatProperty(props, vr::Prop_SecondsFromVsyncToPhotons_Float, 0.011f);
    vr::VRProperties()->SetFloatProperty(props, vr::Prop_UserIpdMeters_Float, m_ipd);
    vr::VRProperties()->SetUint64Property(props, vr::Prop_CurrentUniverseId_Uint64, 2);

    // Render target size (per eye)
    vr::VRProperties()->SetInt32Property(props, vr::Prop_DisplayMCImageWidth_Int32, m_renderWidth);
    vr::VRProperties()->SetInt32Property(props, vr::Prop_DisplayMCImageHeight_Int32, m_renderHeight);

    // Mark as having direct mode component
    vr::VRProperties()->SetBoolProperty(props, vr::Prop_HasDriverDirectModeComponent_Bool, true);

    // Indicate this is not a real display
    vr::VRProperties()->SetBoolProperty(props, vr::Prop_IsOnDesktop_Bool, false);

    return vr::VRInitError_None;
}

void AIVRHmdDriver::Deactivate()
{
    m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
}

void AIVRHmdDriver::EnterStandby()
{
}

void* AIVRHmdDriver::GetComponent(const char* pchComponentNameAndVersion)
{
    if (strcmp(pchComponentNameAndVersion, vr::IVRDisplayComponent_Version) == 0)
    {
        return static_cast<vr::IVRDisplayComponent*>(this);
    }

    if (strcmp(pchComponentNameAndVersion, vr::IVRDriverDirectModeComponent_Version) == 0)
    {
        return static_cast<vr::IVRDriverDirectModeComponent*>(this);
    }

    return nullptr;
}

void AIVRHmdDriver::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize > 0)
    {
        pchResponseBuffer[0] = '\0';
    }
}

vr::DriverPose_t AIVRHmdDriver::GetPose()
{
    vr::DriverPose_t pose = {};

    pose.poseIsValid = true;
    pose.result = vr::TrackingResult_Running_OK;
    pose.deviceIsConnected = true;

    // Identity quaternion (no rotation)
    pose.qWorldFromDriverRotation.w = 1.0;
    pose.qWorldFromDriverRotation.x = 0.0;
    pose.qWorldFromDriverRotation.y = 0.0;
    pose.qWorldFromDriverRotation.z = 0.0;

    pose.qDriverFromHeadRotation.w = 1.0;
    pose.qDriverFromHeadRotation.x = 0.0;
    pose.qDriverFromHeadRotation.y = 0.0;
    pose.qDriverFromHeadRotation.z = 0.0;

    // Slow oscillating rotation (yaw)
    double time = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    double angle = std::sin(time * 0.5) * 0.3;  // ~17 degrees back and forth

    pose.vecPosition[0] = 0.0;
    pose.vecPosition[1] = 1.6;
    pose.vecPosition[2] = 0.0;

    // Quaternion for Y-axis rotation
    pose.qRotation.w = std::cos(angle / 2.0);
    pose.qRotation.x = 0.0;
    pose.qRotation.y = std::sin(angle / 2.0);
    pose.qRotation.z = 0.0;

    return pose;
}

// IVRDisplayComponent implementation

void AIVRHmdDriver::GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight)
{
    *pnX = 0;
    *pnY = 0;
    *pnWidth = m_renderWidth * 2;  // Both eyes side by side
    *pnHeight = m_renderHeight;
}

bool AIVRHmdDriver::IsDisplayOnDesktop()
{
    return false;
}

bool AIVRHmdDriver::IsDisplayRealDisplay()
{
    return false;  // Virtual display
}

void AIVRHmdDriver::GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight)
{
    *pnWidth = m_renderWidth;
    *pnHeight = m_renderHeight;
}

void AIVRHmdDriver::GetEyeOutputViewport(vr::EVREye eEye, uint32_t* pnX, uint32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight)
{
    *pnY = 0;
    *pnWidth = m_renderWidth;
    *pnHeight = m_renderHeight;

    if (eEye == vr::Eye_Left)
    {
        *pnX = 0;
    }
    else
    {
        *pnX = m_renderWidth;
    }
}

void AIVRHmdDriver::GetProjectionRaw(vr::EVREye eEye, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom)
{
    // Standard ~110 degree FOV
    *pfLeft = -1.0f;
    *pfRight = 1.0f;
    *pfTop = -1.0f;
    *pfBottom = 1.0f;
}

vr::DistortionCoordinates_t AIVRHmdDriver::ComputeDistortion(vr::EVREye eEye, float fU, float fV)
{
    // No distortion - passthrough
    vr::DistortionCoordinates_t coords;
    coords.rfRed[0] = fU;
    coords.rfRed[1] = fV;
    coords.rfGreen[0] = fU;
    coords.rfGreen[1] = fV;
    coords.rfBlue[0] = fU;
    coords.rfBlue[1] = fV;
    return coords;
}

bool AIVRHmdDriver::ComputeInverseDistortion(vr::HmdVector2_t* pResult, vr::EVREye eEye, uint32_t unChannel, float fU, float fV)
{
    // No distortion - inverse is same as input
    pResult->v[0] = fU;
    pResult->v[1] = fV;
    return true;
}

// IVRDriverDirectModeComponent implementation

void AIVRHmdDriver::CreateSwapTextureSet(uint32_t unPid, const SwapTextureSetDesc_t* pSwapTextureSetDesc, SwapTextureSet_t* pOutSwapTextureSet)
{
    if (!m_pD3DDevice || !pSwapTextureSetDesc || !pOutSwapTextureSet)
        return;

    SwapTextureSetData setData = {};
    setData.pid = unPid;
    setData.currentIndex = 0;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = pSwapTextureSetDesc->nWidth;
    desc.Height = pSwapTextureSetDesc->nHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = static_cast<DXGI_FORMAT>(pSwapTextureSetDesc->nFormat);
    desc.SampleDesc.Count = pSwapTextureSetDesc->nSampleCount > 0 ? pSwapTextureSetDesc->nSampleCount : 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    for (int i = 0; i < 3; i++)
    {
        HRESULT hr = m_pD3DDevice->CreateTexture2D(&desc, nullptr, &setData.textures[i]);
        if (FAILED(hr))
            return;

        // Get shared handle
        ComPtr<IDXGIResource> dxgiResource;
        hr = setData.textures[i].As(&dxgiResource);
        if (SUCCEEDED(hr))
        {
            dxgiResource->GetSharedHandle(&setData.sharedHandles[i]);
            pOutSwapTextureSet->rSharedTextureHandles[i] = reinterpret_cast<vr::SharedTextureHandle_t>(setData.sharedHandles[i]);
        }
    }

    m_swapTextureSets.push_back(std::move(setData));
}

void AIVRHmdDriver::DestroySwapTextureSet(vr::SharedTextureHandle_t sharedTextureHandle)
{
    auto it = m_swapTextureSets.begin();
    while (it != m_swapTextureSets.end())
    {
        bool found = false;
        for (int i = 0; i < 3; i++)
        {
            if (reinterpret_cast<vr::SharedTextureHandle_t>(it->sharedHandles[i]) == sharedTextureHandle)
            {
                found = true;
                break;
            }
        }
        if (found)
        {
            it = m_swapTextureSets.erase(it);
            return;
        }
        else
        {
            ++it;
        }
    }
}

void AIVRHmdDriver::DestroyAllSwapTextureSets(uint32_t unPid)
{
    auto it = m_swapTextureSets.begin();
    while (it != m_swapTextureSets.end())
    {
        if (it->pid == unPid)
        {
            it = m_swapTextureSets.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void AIVRHmdDriver::GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t sharedTextureHandles[2], uint32_t (*pIndices)[2])
{
    for (auto& set : m_swapTextureSets)
    {
        for (int eye = 0; eye < 2; eye++)
        {
            for (int i = 0; i < 3; i++)
            {
                if (reinterpret_cast<vr::SharedTextureHandle_t>(set.sharedHandles[i]) == sharedTextureHandles[eye])
                {
                    set.currentIndex = (set.currentIndex + 1) % 3;
                    (*pIndices)[eye] = set.currentIndex;
                    break;
                }
            }
        }
    }
}

// Store the last submitted textures and bounds so we can send them in Present()
static vr::SharedTextureHandle_t s_lastSubmittedTextures[2] = { 0, 0 };
static vr::VRTextureBounds_t s_lastSubmittedBounds[2] = {};

void AIVRHmdDriver::SubmitLayer(const SubmitLayerPerEye_t (&perEye)[2])
{
    // Store texture handles and bounds for sending in Present()
    s_lastSubmittedTextures[0] = perEye[0].hTexture;
    s_lastSubmittedTextures[1] = perEye[1].hTexture;
    s_lastSubmittedBounds[0] = perEye[0].bounds;
    s_lastSubmittedBounds[1] = perEye[1].bounds;
}

void AIVRHmdDriver::Present(vr::SharedTextureHandle_t syncTexture)
{
    m_frameCount++;

    if (m_frameCount % 100 == 1)
    {
        char buf[512];
        snprintf(buf, sizeof(buf), "Present frame %llu, bounds[0]=(%.3f,%.3f)-(%.3f,%.3f), bounds[1]=(%.3f,%.3f)-(%.3f,%.3f)",
            m_frameCount.load(),
            s_lastSubmittedBounds[0].uMin, s_lastSubmittedBounds[0].vMin,
            s_lastSubmittedBounds[0].uMax, s_lastSubmittedBounds[0].vMax,
            s_lastSubmittedBounds[1].uMin, s_lastSubmittedBounds[1].vMin,
            s_lastSubmittedBounds[1].uMax, s_lastSubmittedBounds[1].vMax);
        DebugLog(buf);
    }

    if (!m_tcpConnected)
    {
        InitTCP();
    }

    if (!m_tcpConnected || !m_pD3DDevice)
        return;

    // Open and send textures for both eyes
    for (int eye = 0; eye < 2; eye++)
    {
        if (s_lastSubmittedTextures[eye] == 0)
            continue;

        // Open the shared texture directly from the handle
        ComPtr<ID3D11Texture2D> pTexture;
        HRESULT hr = m_pD3DDevice->OpenSharedResource(
            reinterpret_cast<HANDLE>(s_lastSubmittedTextures[eye]),
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(pTexture.GetAddressOf()));

        if (m_frameCount % 100 == 1)
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "  Eye %d: OpenSharedResource hr=0x%08X, pTexture=%p", eye, hr, pTexture.Get());
            DebugLog(buf);
        }

        if (SUCCEEDED(hr) && pTexture)
        {
            SendFrameData(pTexture.Get(), eye, s_lastSubmittedBounds[eye]);
        }
    }
}

void AIVRHmdDriver::PostPresent(const Throttling_t* pThrottling)
{
    // Called after Present, can do additional work here
}

void AIVRHmdDriver::GetFrameTiming(vr::DriverDirectMode_FrameTiming* pFrameTiming)
{
    if (pFrameTiming)
    {
        pFrameTiming->m_nSize = sizeof(vr::DriverDirectMode_FrameTiming);
        pFrameTiming->m_nNumFramePresents = 1;
        pFrameTiming->m_nNumMisPresented = 0;
        pFrameTiming->m_nNumDroppedFrames = 0;
        pFrameTiming->m_nReprojectionFlags = 0;
    }
}

void AIVRHmdDriver::RunFrame()
{
    // Update pose each frame
    if (m_unObjectId != vr::k_unTrackedDeviceIndexInvalid)
    {
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
            m_unObjectId,
            GetPose(),
            sizeof(vr::DriverPose_t));
    }
}

void AIVRHmdDriver::ProcessEvent(const vr::VREvent_t& event)
{
    // Handle events if needed
}
