#include "hmd_device_driver.h"
#include <cstring>
#include <cmath>
#include <chrono>
#include <fstream>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

Driver::Driver()
{
    InitD3D11();
    m_socketManager.Init();
}

Driver::~Driver()
{
    CleanupD3D11();
}

bool Driver::InitD3D11()
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

void Driver::CleanupD3D11()
{
    m_pStagingTexture.Reset();
    m_swapTextureSets.clear();
    m_pD3DContext.Reset();
    m_pD3DDevice.Reset();
}

vr::EVRInitError Driver::Activate(uint32_t unObjectId)
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

void Driver::Deactivate()
{
    m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
}

void Driver::EnterStandby()
{
}

void* Driver::GetComponent(const char* pchComponentNameAndVersion)
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

void Driver::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize > 0)
    {
        pchResponseBuffer[0] = '\0';
    }
}

vr::DriverPose_t Driver::GetPose()
{
    // Update with latest position from socket if available
    if (auto newPos = m_socketManager.GetNextPosition())
    {
        m_lastPosition = *newPos;
    }

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

    // Position from socket
    pose.vecPosition[0] = m_lastPosition.x;
    pose.vecPosition[1] = m_lastPosition.y;
    pose.vecPosition[2] = m_lastPosition.z;

    // Rotation from socket (quaternion)
    pose.qRotation.w = m_lastPosition.qw;
    pose.qRotation.x = m_lastPosition.qx;
    pose.qRotation.y = m_lastPosition.qy;
    pose.qRotation.z = m_lastPosition.qz;

    return pose;
}

// IVRDisplayComponent implementation

void Driver::GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight)
{
    *pnX = 0;
    *pnY = 0;
    *pnWidth = m_renderWidth * 2;  // Both eyes side by side
    *pnHeight = m_renderHeight;
}

bool Driver::IsDisplayOnDesktop()
{
    return false;
}

bool Driver::IsDisplayRealDisplay()
{
    return false;  // Virtual display
}

void Driver::GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight)
{
    *pnWidth = m_renderWidth;
    *pnHeight = m_renderHeight;
}

void Driver::GetEyeOutputViewport(vr::EVREye eEye, uint32_t* pnX, uint32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight)
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

void Driver::GetProjectionRaw(vr::EVREye eEye, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom)
{
    // Standard ~110 degree FOV
    *pfLeft = -1.0f;
    *pfRight = 1.0f;
    *pfTop = -1.0f;
    *pfBottom = 1.0f;
}

vr::DistortionCoordinates_t Driver::ComputeDistortion(vr::EVREye eEye, float fU, float fV)
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

bool Driver::ComputeInverseDistortion(vr::HmdVector2_t* pResult, vr::EVREye eEye, uint32_t unChannel, float fU, float fV)
{
    // No distortion - inverse is same as input
    pResult->v[0] = fU;
    pResult->v[1] = fV;
    return true;
}

// IVRDriverDirectModeComponent implementation

void Driver::CreateSwapTextureSet(uint32_t unPid, const SwapTextureSetDesc_t* pSwapTextureSetDesc, SwapTextureSet_t* pOutSwapTextureSet)
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

void Driver::DestroySwapTextureSet(vr::SharedTextureHandle_t sharedTextureHandle)
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

void Driver::DestroyAllSwapTextureSets(uint32_t unPid)
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

void Driver::GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t sharedTextureHandles[2], uint32_t (*pIndices)[2])
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

void Driver::SubmitLayer(const SubmitLayerPerEye_t (&perEye)[2])
{
    // Store texture handles and bounds for sending in Present()
    s_lastSubmittedTextures[0] = perEye[0].hTexture;
    s_lastSubmittedTextures[1] = perEye[1].hTexture;
    s_lastSubmittedBounds[0] = perEye[0].bounds;
    s_lastSubmittedBounds[1] = perEye[1].bounds;
}

void Driver::Present(vr::SharedTextureHandle_t syncTexture)
{
    m_frameCount++;

    if (!m_pD3DDevice)
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

        if (FAILED(hr) || !pTexture)
            continue;

        // Get texture description
        D3D11_TEXTURE2D_DESC desc;
        pTexture->GetDesc(&desc);

        // Create or recreate staging texture if needed
        if (!m_pStagingTexture || m_stagingWidth != desc.Width || m_stagingHeight != desc.Height || m_stagingFormat != desc.Format)
        {
            m_pStagingTexture.Reset();

            D3D11_TEXTURE2D_DESC stagingDesc = {};
            stagingDesc.Width = desc.Width;
            stagingDesc.Height = desc.Height;
            stagingDesc.MipLevels = 1;
            stagingDesc.ArraySize = 1;
            if (desc.Format == DXGI_FORMAT_R10G10B10A2_TYPELESS)
                stagingDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
            else if (desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
                stagingDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            else
                stagingDesc.Format = desc.Format;
            stagingDesc.SampleDesc.Count = 1;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

            if (FAILED(m_pD3DDevice->CreateTexture2D(&stagingDesc, nullptr, &m_pStagingTexture)))
                continue;

            m_stagingWidth = desc.Width;
            m_stagingHeight = desc.Height;
            m_stagingFormat = desc.Format;
        }

        // Copy texture to staging
        m_pD3DContext->CopyResource(m_pStagingTexture.Get(), pTexture.Get());

        // Map staging texture
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(m_pD3DContext->Map(m_pStagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            continue;

        // Calculate cropped region from bounds
        const auto& bounds = s_lastSubmittedBounds[eye];
        uint32_t cropX = static_cast<uint32_t>(bounds.uMin * m_stagingWidth);
        uint32_t cropY = static_cast<uint32_t>(bounds.vMin * m_stagingHeight);
        uint32_t cropW = static_cast<uint32_t>((bounds.uMax - bounds.uMin) * m_stagingWidth);
        uint32_t cropH = static_cast<uint32_t>((bounds.vMax - bounds.vMin) * m_stagingHeight);

        if (cropW == 0) cropW = m_stagingWidth;
        if (cropH == 0) cropH = m_stagingHeight;
        if (cropX + cropW > m_stagingWidth) cropW = m_stagingWidth - cropX;
        if (cropY + cropH > m_stagingHeight) cropH = m_stagingHeight - cropY;

        // Copy cropped pixels into contiguous buffer
        uint8_t* srcData = static_cast<uint8_t*>(mapped.pData);
        std::vector<uint8_t> buffer(cropW * cropH * 4);

        for (uint32_t y = 0; y < cropH; y++)
        {
            const uint8_t* srcRow = srcData + (cropY + y) * mapped.RowPitch + cropX * 4;
            uint8_t* dstRow = buffer.data() + y * cropW * 4;
            std::copy(srcRow, srcRow + cropW * 4, dstRow);
        }

        m_pD3DContext->Unmap(m_pStagingTexture.Get(), 0);

        // Send frame via socket manager
        Frame frame { buffer.data(), cropW, cropH, static_cast<uint32_t>(eye) };
        m_socketManager.SendFrame(frame);
    }
}

void Driver::PostPresent(const Throttling_t* pThrottling)
{
    // Called after Present, can do additional work here
}

void Driver::GetFrameTiming(vr::DriverDirectMode_FrameTiming* pFrameTiming)
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

void Driver::RunFrame()
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

void Driver::ProcessEvent(const vr::VREvent_t& event)
{
    // Handle events if needed
}

std::optional<ControllerInput> Driver::GetNextControllerInput()
{
    return m_socketManager.GetNextControllerInput();
}
