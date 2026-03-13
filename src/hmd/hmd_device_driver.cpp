#include "hmd_device_driver.h"
#include <cstring>
#include <cmath>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <cstdarg>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#endif

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

static Pose HmdMatrix34ToPose(const vr::HmdMatrix34_t& mat)
{
    Pose p;
    p.posX = mat.m[0][3];
    p.posY = mat.m[1][3];
    p.posZ = mat.m[2][3];

    float trace = mat.m[0][0] + mat.m[1][1] + mat.m[2][2];

    if (trace > 0.0f)
    {
        float s = 0.5f / sqrtf(trace + 1.0f);
        p.rotW = 0.25f / s;
        p.rotX = (mat.m[2][1] - mat.m[1][2]) * s;
        p.rotY = (mat.m[0][2] - mat.m[2][0]) * s;
        p.rotZ = (mat.m[1][0] - mat.m[0][1]) * s;
    }
    else if (mat.m[0][0] > mat.m[1][1] && mat.m[0][0] > mat.m[2][2])
    {
        float s = 2.0f * sqrtf(1.0f + mat.m[0][0] - mat.m[1][1] - mat.m[2][2]);
        p.rotW = (mat.m[2][1] - mat.m[1][2]) / s;
        p.rotX = 0.25f * s;
        p.rotY = (mat.m[0][1] + mat.m[1][0]) / s;
        p.rotZ = (mat.m[0][2] + mat.m[2][0]) / s;
    }
    else if (mat.m[1][1] > mat.m[2][2])
    {
        float s = 2.0f * sqrtf(1.0f + mat.m[1][1] - mat.m[0][0] - mat.m[2][2]);
        p.rotW = (mat.m[0][2] - mat.m[2][0]) / s;
        p.rotX = (mat.m[0][1] + mat.m[1][0]) / s;
        p.rotY = 0.25f * s;
        p.rotZ = (mat.m[1][2] + mat.m[2][1]) / s;
    }
    else
    {
        float s = 2.0f * sqrtf(1.0f + mat.m[2][2] - mat.m[0][0] - mat.m[1][1]);
        p.rotW = (mat.m[1][0] - mat.m[0][1]) / s;
        p.rotX = (mat.m[0][2] + mat.m[2][0]) / s;
        p.rotY = (mat.m[1][2] + mat.m[2][1]) / s;
        p.rotZ = 0.25f * s;
    }

    return p;
}

Driver::Driver(mpsc::Receiver<Pose> poseReceiver, SocketManager* socketManager)
    : m_poseReceiver(std::move(poseReceiver))
    , m_pSocketManager(socketManager)
{
#ifdef _WIN32
    InitD3D11();
#else
    InitVulkan();
#endif
}

Driver::~Driver()
{
#ifdef _WIN32
    CleanupD3D11();
#else
    CleanupVulkan();
#endif
}

// ============================================================================
// Platform-specific GPU init/cleanup
// ============================================================================

#ifdef _WIN32

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
    m_pSyncTexture.Reset();
    m_syncTextureHandle = 0;
    m_pStagingTexture.Reset();
    m_swapTextureSets.clear();
    m_pD3DContext.Reset();
    m_pD3DDevice.Reset();
}

#else // Linux Vulkan

bool Driver::InitVulkan()
{
    // Create Vulkan instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "OVD Driver";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "OVD";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    const char* instanceExtensions[] = {
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = 2;
    instanceInfo.ppEnabledExtensionNames = instanceExtensions;

    if (vkCreateInstance(&instanceInfo, nullptr, &m_vkInstance) != VK_SUCCESS)
    {
        Log("[OVD] Failed to create Vulkan instance\n");
        return false;
    }

    // Pick physical device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        Log("[OVD] No Vulkan physical devices found\n");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, devices.data());
    m_vkPhysicalDevice = devices[0];

    // Find a graphics queue family
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            m_vkQueueFamily = i;
            break;
        }
    }

    // Create logical device with external memory FD extension
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = m_vkQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = {
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    };

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = 2;
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;

    if (vkCreateDevice(m_vkPhysicalDevice, &deviceCreateInfo, nullptr, &m_vkDevice) != VK_SUCCESS)
    {
        Log("[OVD] Failed to create Vulkan device\n");
        return false;
    }

    vkGetDeviceQueue(m_vkDevice, m_vkQueueFamily, 0, &m_vkQueue);

    // Create command pool and buffer
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_vkQueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_vkDevice, &poolInfo, nullptr, &m_vkCommandPool) != VK_SUCCESS)
    {
        Log("[OVD] Failed to create Vulkan command pool\n");
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_vkCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_vkDevice, &allocInfo, &m_vkCommandBuffer) != VK_SUCCESS)
    {
        Log("[OVD] Failed to allocate Vulkan command buffer\n");
        return false;
    }

    Log("[OVD] Vulkan initialized for frame readback\n");
    return true;
}

void Driver::CleanupVulkan()
{
    if (m_vkDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_vkDevice);

        // Clean up imported textures
        for (auto& set : m_swapTextureSets)
        {
            for (int i = 0; i < 3; i++)
            {
                if (set.imported[i].image != VK_NULL_HANDLE)
                    vkDestroyImage(m_vkDevice, set.imported[i].image, nullptr);
                if (set.imported[i].memory != VK_NULL_HANDLE)
                    vkFreeMemory(m_vkDevice, set.imported[i].memory, nullptr);
                if (set.imported[i].fd >= 0)
                    ::close(set.imported[i].fd);
            }
        }
        m_swapTextureSets.clear();

        if (m_stagingBuffer != VK_NULL_HANDLE)
            vkDestroyBuffer(m_vkDevice, m_stagingBuffer, nullptr);
        if (m_stagingMemory != VK_NULL_HANDLE)
            vkFreeMemory(m_vkDevice, m_stagingMemory, nullptr);

        if (m_vkCommandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(m_vkDevice, m_vkCommandPool, nullptr);

        vkDestroyDevice(m_vkDevice, nullptr);
    }
    if (m_vkInstance != VK_NULL_HANDLE)
        vkDestroyInstance(m_vkInstance, nullptr);
}

uint32_t Driver::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_vkPhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    return 0;
}

uint32_t Driver::DXGIFormatToVkFormat(uint32_t dxgiFormat)
{
    // Map common DXGI formats to VkFormat
    switch (dxgiFormat)
    {
        case 28: return 37;  // DXGI_FORMAT_R8G8B8A8_UNORM -> VK_FORMAT_R8G8B8A8_UNORM
        case 29: return 43;  // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB -> VK_FORMAT_R8G8B8A8_SRGB
        case 27: return 37;  // DXGI_FORMAT_R8G8B8A8_TYPELESS -> VK_FORMAT_R8G8B8A8_UNORM
        case 87: return 44;  // DXGI_FORMAT_B8G8R8A8_UNORM -> VK_FORMAT_B8G8R8A8_UNORM
        case 91: return 50;  // DXGI_FORMAT_B8G8R8A8_UNORM_SRGB -> VK_FORMAT_B8G8R8A8_SRGB
        case 90: return 44;  // DXGI_FORMAT_B8G8R8A8_TYPELESS -> VK_FORMAT_B8G8R8A8_UNORM
        case 24: return 64;  // DXGI_FORMAT_R10G10B10A2_UNORM -> VK_FORMAT_A2B10G10R10_UNORM_PACK32
        case 23: return 64;  // DXGI_FORMAT_R10G10B10A2_TYPELESS -> VK_FORMAT_A2B10G10R10_UNORM_PACK32
        case 10: return 97;  // DXGI_FORMAT_R16G16B16A16_FLOAT -> VK_FORMAT_R16G16B16A16_SFLOAT
        default: return 37;  // Fallback to R8G8B8A8_UNORM
    }
}

#endif // platform

// ============================================================================
// ITrackedDeviceServerDriver
// ============================================================================

vr::EVRInitError Driver::Activate(uint32_t unObjectId)
{
    m_unObjectId = unObjectId;

    vr::PropertyContainerHandle_t props = vr::VRProperties()->TrackedDeviceToPropertyContainer(unObjectId);

    vr::VRProperties()->SetStringProperty(props, vr::Prop_ModelNumber_String, "AI VR HMD");
    vr::VRProperties()->SetStringProperty(props, vr::Prop_ManufacturerName_String, "AI VR");
    vr::VRProperties()->SetStringProperty(props, vr::Prop_TrackingSystemName_String, "AI VR Tracking");
    vr::VRProperties()->SetStringProperty(props, vr::Prop_SerialNumber_String, m_serialNumber.c_str());

    vr::VRProperties()->SetFloatProperty(props, vr::Prop_DisplayFrequency_Float, m_displayFrequency);
    vr::VRProperties()->SetFloatProperty(props, vr::Prop_SecondsFromVsyncToPhotons_Float, 0.011f);
    vr::VRProperties()->SetFloatProperty(props, vr::Prop_UserIpdMeters_Float, m_ipd);
    vr::VRProperties()->SetUint64Property(props, vr::Prop_CurrentUniverseId_Uint64, 2);

    vr::VRProperties()->SetInt32Property(props, vr::Prop_DisplayMCImageWidth_Int32, m_renderWidth);
    vr::VRProperties()->SetInt32Property(props, vr::Prop_DisplayMCImageHeight_Int32, m_renderHeight);

    vr::VRProperties()->SetBoolProperty(props, vr::Prop_HasDriverDirectModeComponent_Bool, true);
    vr::VRProperties()->SetBoolProperty(props, vr::Prop_IsOnDesktop_Bool, false);

    vr::VRProperties()->SetBoolProperty(props, vr::Prop_ContainsProximitySensor_Bool, true);
    vr::VRDriverInput()->CreateBooleanComponent(props, "/proximity", &m_proximityHandle);
    vr::VRDriverInput()->UpdateBooleanComponent(m_proximityHandle, true, 0.0);

    m_poseThread = std::jthread([this](std::stop_token st) { PoseUpdateThreadFunc(st); });

    return vr::VRInitError_None;
}

void Driver::PoseUpdateThreadFunc(std::stop_token st)
{
    vr::DriverPose_t pose = {};
    pose.poseIsValid = true;
    pose.result = vr::TrackingResult_Running_OK;
    pose.deviceIsConnected = true;
    pose.qWorldFromDriverRotation.w = 1.0;
    pose.qDriverFromHeadRotation.w = 1.0;
    pose.vecPosition[0] = 0.0;
    pose.vecPosition[1] = 1.7;
    pose.vecPosition[2] = 0.0;
    pose.qRotation.w = 1.0;

    while (!st.stop_requested())
    {
        if (auto p = m_poseReceiver.try_recv())
        {
            pose.vecPosition[0] = p->posX;
            pose.vecPosition[1] = p->posY;
            pose.vecPosition[2] = p->posZ;
            pose.qRotation.w = p->rotW;
            pose.qRotation.x = p->rotX;
            pose.qRotation.y = p->rotY;
            pose.qRotation.z = p->rotZ;
        }

        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_unObjectId, pose, sizeof(vr::DriverPose_t));
        std::this_thread::sleep_for(std::chrono::milliseconds(11));
    }
}

void Driver::Deactivate()
{
    if (m_poseThread.joinable())
    {
        m_poseThread.request_stop();
        m_poseThread.join();
    }
    m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
}

void Driver::EnterStandby() {}

void* Driver::GetComponent(const char* pchComponentNameAndVersion)
{
    if (strcmp(pchComponentNameAndVersion, vr::IVRDisplayComponent_Version) == 0)
        return static_cast<vr::IVRDisplayComponent*>(this);

    if (strcmp(pchComponentNameAndVersion, vr::IVRDriverDirectModeComponent_Version) == 0)
        return static_cast<vr::IVRDriverDirectModeComponent*>(this);

    return nullptr;
}

void Driver::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize > 0)
        pchResponseBuffer[0] = '\0';
}

vr::DriverPose_t Driver::GetPose()
{
    vr::DriverPose_t pose = {};
    pose.poseIsValid = false;
    return pose;
}

// ============================================================================
// IVRDisplayComponent
// ============================================================================

void Driver::GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight)
{
    *pnX = 0;
    *pnY = 0;
    *pnWidth = m_renderWidth * 2;
    *pnHeight = m_renderHeight;
}

bool Driver::IsDisplayOnDesktop() { return false; }
bool Driver::IsDisplayRealDisplay() { return false; }

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
    *pnX = (eEye == vr::Eye_Left) ? 0 : m_renderWidth;
}

void Driver::GetProjectionRaw(vr::EVREye eEye, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom)
{
    *pfLeft = -1.0f;
    *pfRight = 1.0f;
    *pfTop = -1.0f;
    *pfBottom = 1.0f;
}

vr::DistortionCoordinates_t Driver::ComputeDistortion(vr::EVREye eEye, float fU, float fV)
{
    vr::DistortionCoordinates_t coords;
    coords.rfRed[0] = fU;   coords.rfRed[1] = fV;
    coords.rfGreen[0] = fU; coords.rfGreen[1] = fV;
    coords.rfBlue[0] = fU;  coords.rfBlue[1] = fV;
    return coords;
}

bool Driver::ComputeInverseDistortion(vr::HmdVector2_t* pResult, vr::EVREye eEye, uint32_t unChannel, float fU, float fV)
{
    pResult->v[0] = fU;
    pResult->v[1] = fV;
    return true;
}

// ============================================================================
// IVRDriverDirectModeComponent — shared methods
// ============================================================================

static vr::SharedTextureHandle_t s_lastSubmittedTextures[2] = { 0, 0 };
static vr::VRTextureBounds_t s_lastSubmittedBounds[2] = {};
static Pose s_lastSubmittedPose = {};

void Driver::SubmitLayer(const SubmitLayerPerEye_t (&perEye)[2])
{
    s_lastSubmittedTextures[0] = perEye[0].hTexture;
    s_lastSubmittedTextures[1] = perEye[1].hTexture;
    s_lastSubmittedBounds[0] = perEye[0].bounds;
    s_lastSubmittedBounds[1] = perEye[1].bounds;
    s_lastSubmittedPose = HmdMatrix34ToPose(perEye[0].mHmdPose);
}

void Driver::PostPresent(const Throttling_t* pThrottling) {}

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

void Driver::ProcessEvent(const vr::VREvent_t& event) {}

void Driver::GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t sharedTextureHandles[2], uint32_t (*pIndices)[2])
{
    for (auto& set : m_swapTextureSets)
    {
        for (int eye = 0; eye < 2; eye++)
        {
            for (int i = 0; i < 3; i++)
            {
#ifdef _WIN32
                if (reinterpret_cast<vr::SharedTextureHandle_t>(set.sharedHandles[i]) == sharedTextureHandles[eye])
#else
                if (set.handles[i] == sharedTextureHandles[eye])
#endif
                {
                    set.currentIndex = (set.currentIndex + 1) % 3;
                    (*pIndices)[eye] = set.currentIndex;
                    break;
                }
            }
        }
    }
}

// ============================================================================
// Platform-specific DirectMode texture management
// ============================================================================

#ifdef _WIN32

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
    for (auto it = m_swapTextureSets.begin(); it != m_swapTextureSets.end(); ++it)
    {
        for (int i = 0; i < 3; i++)
        {
            if (reinterpret_cast<vr::SharedTextureHandle_t>(it->sharedHandles[i]) == sharedTextureHandle)
            {
                m_swapTextureSets.erase(it);
                return;
            }
        }
    }
}

void Driver::DestroyAllSwapTextureSets(uint32_t unPid)
{
    std::erase_if(m_swapTextureSets, [unPid](const SwapTextureSetData& s) { return s.pid == unPid; });
}

void Driver::Present(vr::SharedTextureHandle_t syncTexture)
{
    m_frameCount++;

    if (!m_pD3DDevice || !m_pSocketManager)
        return;

    if (m_syncTextureHandle != syncTexture)
    {
        m_syncTextureHandle = syncTexture;
        m_pSyncTexture.Reset();
        if (syncTexture)
        {
            m_pD3DDevice->OpenSharedResource(
                reinterpret_cast<HANDLE>(syncTexture),
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(m_pSyncTexture.GetAddressOf()));
        }
    }

    ComPtr<IDXGIKeyedMutex> pSyncMutex;
    if (m_pSyncTexture && SUCCEEDED(m_pSyncTexture->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(pSyncMutex.GetAddressOf()))))
    {
        pSyncMutex->AcquireSync(0, 10);
    }

    for (int eye = 0; eye < 2; eye++)
    {
        if (s_lastSubmittedTextures[eye] == 0)
            continue;

        ComPtr<ID3D11Texture2D> pTexture;
        HRESULT hr = m_pD3DDevice->OpenSharedResource(
            reinterpret_cast<HANDLE>(s_lastSubmittedTextures[eye]),
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(pTexture.GetAddressOf()));

        if (FAILED(hr) || !pTexture)
            continue;

        D3D11_TEXTURE2D_DESC desc;
        pTexture->GetDesc(&desc);

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

        m_pD3DContext->CopyResource(m_pStagingTexture.Get(), pTexture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(m_pD3DContext->Map(m_pStagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            continue;

        const auto& bounds = s_lastSubmittedBounds[eye];
        uint32_t cropX = static_cast<uint32_t>(bounds.uMin * m_stagingWidth);
        uint32_t cropY = static_cast<uint32_t>(bounds.vMin * m_stagingHeight);
        uint32_t cropW = static_cast<uint32_t>((bounds.uMax - bounds.uMin) * m_stagingWidth);
        uint32_t cropH = static_cast<uint32_t>((bounds.vMax - bounds.vMin) * m_stagingHeight);

        if (cropW == 0) cropW = m_stagingWidth;
        if (cropH == 0) cropH = m_stagingHeight;
        if (cropX + cropW > m_stagingWidth) cropW = m_stagingWidth - cropX;
        if (cropY + cropH > m_stagingHeight) cropH = m_stagingHeight - cropY;

        uint8_t* srcData = static_cast<uint8_t*>(mapped.pData);
        std::vector<uint8_t> buffer(cropW * cropH * 4);

        for (uint32_t y = 0; y < cropH; y++)
        {
            const uint8_t* srcRow = srcData + (cropY + y) * mapped.RowPitch + cropX * 4;
            uint8_t* dstRow = buffer.data() + y * cropW * 4;
            std::copy(srcRow, srcRow + cropW * 4, dstRow);
        }

        m_pD3DContext->Unmap(m_pStagingTexture.Get(), 0);

        Frame frame { buffer.data(), cropW, cropH, static_cast<uint32_t>(eye), s_lastSubmittedPose };
        m_pSocketManager->SendFrame(frame);
    }

    if (pSyncMutex)
    {
        pSyncMutex->ReleaseSync(0);
    }
}

#else // Linux — use VRIPCResourceManager for shared Vulkan textures

void Driver::CreateSwapTextureSet(uint32_t unPid, const SwapTextureSetDesc_t* pSwapTextureSetDesc, SwapTextureSet_t* pOutSwapTextureSet)
{
    if (!pSwapTextureSetDesc || !pOutSwapTextureSet)
        return;

    auto* ipcMgr = vr::VRIPCResourceManager();
    if (!ipcMgr)
    {
        Log("[OVD] VRIPCResourceManager not available\n");
        return;
    }

    uint32_t vkFormat = DXGIFormatToVkFormat(pSwapTextureSetDesc->nFormat);

    SwapTextureSetData setData = {};
    setData.pid = unPid;
    setData.currentIndex = 0;
    setData.width = pSwapTextureSetDesc->nWidth;
    setData.height = pSwapTextureSetDesc->nHeight;

    Log("[OVD] CreateSwapTextureSet: %ux%u fmt=%u(vk=%u) pid=%u\n",
        pSwapTextureSetDesc->nWidth, pSwapTextureSetDesc->nHeight,
        pSwapTextureSetDesc->nFormat, vkFormat, unPid);

    for (int i = 0; i < 3; i++)
    {
        setData.handles[i] = 0;
        setData.imported[i] = {};

        vr::SharedTextureHandle_t handle = 0;
        bool ok = ipcMgr->NewSharedVulkanImage(
            vkFormat,
            pSwapTextureSetDesc->nWidth,
            pSwapTextureSetDesc->nHeight,
            true,  // bRenderable
            false, // bMappable
            false, // bComputeAccess
            1,     // mip levels
            1,     // array layers
            &handle);

        if (!ok || handle == 0)
        {
            Log("[OVD] NewSharedVulkanImage failed for texture %d\n", i);
            // Clean up already created textures
            for (int j = 0; j < i; j++)
            {
                if (setData.handles[j] != 0)
                    ipcMgr->UnrefResource(setData.handles[j]);
            }
            return;
        }

        setData.handles[i] = handle;
        pOutSwapTextureSet->rSharedTextureHandles[i] = handle;

        Log("[OVD] Created shared texture %d: handle=%llu\n", i, (unsigned long long)handle);

        // Import into our Vulkan device for readback:
        // 1. Get an IPC handle via RefResource
        // 2. Get a file descriptor via ReceiveSharedFd
        // 3. Import the FD as a VkImage
        uint64_t ipcHandle = 0;
        if (ipcMgr->RefResource(handle, &ipcHandle) && ipcHandle != 0)
        {
            int fd = -1;
            if (ipcMgr->ReceiveSharedFd(ipcHandle, &fd) && fd >= 0)
            {
                setData.imported[i].fd = fd;

                if (m_vkDevice != VK_NULL_HANDLE)
                {
                    // Create VkImage for import
                    VkExternalMemoryImageCreateInfo extMemInfo{};
                    extMemInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
                    extMemInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

                    VkImageCreateInfo imageInfo{};
                    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                    imageInfo.pNext = &extMemInfo;
                    imageInfo.imageType = VK_IMAGE_TYPE_2D;
                    imageInfo.format = static_cast<VkFormat>(vkFormat);
                    imageInfo.extent = { pSwapTextureSetDesc->nWidth, pSwapTextureSetDesc->nHeight, 1 };
                    imageInfo.mipLevels = 1;
                    imageInfo.arrayLayers = 1;
                    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                    if (vkCreateImage(m_vkDevice, &imageInfo, nullptr, &setData.imported[i].image) == VK_SUCCESS)
                    {
                        VkMemoryRequirements memReqs;
                        vkGetImageMemoryRequirements(m_vkDevice, setData.imported[i].image, &memReqs);

                        // Import external memory from FD
                        VkImportMemoryFdInfoKHR importFdInfo{};
                        importFdInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
                        importFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
                        importFdInfo.fd = fd;

                        VkMemoryAllocateInfo allocInfo{};
                        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                        allocInfo.pNext = &importFdInfo;
                        allocInfo.allocationSize = memReqs.size;
                        allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

                        if (vkAllocateMemory(m_vkDevice, &allocInfo, nullptr, &setData.imported[i].memory) == VK_SUCCESS)
                        {
                            vkBindImageMemory(m_vkDevice, setData.imported[i].image, setData.imported[i].memory, 0);
                            // FD ownership transferred to Vulkan — mark as consumed
                            setData.imported[i].fd = -1;
                            Log("[OVD] Imported texture %d into Vulkan device\n", i);
                        }
                        else
                        {
                            Log("[OVD] Failed to allocate imported memory for texture %d\n", i);
                            vkDestroyImage(m_vkDevice, setData.imported[i].image, nullptr);
                            setData.imported[i].image = VK_NULL_HANDLE;
                        }
                    }
                    else
                    {
                        Log("[OVD] Failed to create import VkImage for texture %d\n", i);
                    }
                }
            }
            else
            {
                Log("[OVD] ReceiveSharedFd failed for texture %d\n", i);
            }
        }
        else
        {
            Log("[OVD] RefResource failed for texture %d\n", i);
        }
    }

    pOutSwapTextureSet->unTextureFlags = 0;
    m_swapTextureSets.push_back(std::move(setData));
}

void Driver::DestroySwapTextureSet(vr::SharedTextureHandle_t sharedTextureHandle)
{
    auto* ipcMgr = vr::VRIPCResourceManager();

    for (auto it = m_swapTextureSets.begin(); it != m_swapTextureSets.end(); ++it)
    {
        for (int i = 0; i < 3; i++)
        {
            if (it->handles[i] == sharedTextureHandle)
            {
                // Clean up Vulkan imports
                for (int j = 0; j < 3; j++)
                {
                    if (it->imported[j].image != VK_NULL_HANDLE && m_vkDevice != VK_NULL_HANDLE)
                        vkDestroyImage(m_vkDevice, it->imported[j].image, nullptr);
                    if (it->imported[j].memory != VK_NULL_HANDLE && m_vkDevice != VK_NULL_HANDLE)
                        vkFreeMemory(m_vkDevice, it->imported[j].memory, nullptr);
                    if (it->imported[j].fd >= 0)
                        ::close(it->imported[j].fd);
                    if (ipcMgr && it->handles[j] != 0)
                        ipcMgr->UnrefResource(it->handles[j]);
                }
                m_swapTextureSets.erase(it);
                return;
            }
        }
    }
}

void Driver::DestroyAllSwapTextureSets(uint32_t unPid)
{
    auto* ipcMgr = vr::VRIPCResourceManager();

    auto it = m_swapTextureSets.begin();
    while (it != m_swapTextureSets.end())
    {
        if (it->pid == unPid)
        {
            for (int i = 0; i < 3; i++)
            {
                if (it->imported[i].image != VK_NULL_HANDLE && m_vkDevice != VK_NULL_HANDLE)
                    vkDestroyImage(m_vkDevice, it->imported[i].image, nullptr);
                if (it->imported[i].memory != VK_NULL_HANDLE && m_vkDevice != VK_NULL_HANDLE)
                    vkFreeMemory(m_vkDevice, it->imported[i].memory, nullptr);
                if (it->imported[i].fd >= 0)
                    ::close(it->imported[i].fd);
                if (ipcMgr && it->handles[i] != 0)
                    ipcMgr->UnrefResource(it->handles[i]);
            }
            it = m_swapTextureSets.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void Driver::Present(vr::SharedTextureHandle_t syncTexture)
{
    m_frameCount++;

    if (m_vkDevice == VK_NULL_HANDLE || !m_pSocketManager)
        return;

    for (int eye = 0; eye < 2; eye++)
    {
        if (s_lastSubmittedTextures[eye] == 0)
            continue;

        // Find the matching imported image
        VkImage srcImage = VK_NULL_HANDLE;
        uint32_t imgWidth = 0, imgHeight = 0;

        for (auto& set : m_swapTextureSets)
        {
            for (int i = 0; i < 3; i++)
            {
                if (set.handles[i] == s_lastSubmittedTextures[eye])
                {
                    srcImage = set.imported[i].image;
                    imgWidth = set.width;
                    imgHeight = set.height;
                    break;
                }
            }
            if (srcImage != VK_NULL_HANDLE) break;
        }

        if (srcImage == VK_NULL_HANDLE || imgWidth == 0 || imgHeight == 0)
            continue;

        // Apply bounds cropping
        const auto& bounds = s_lastSubmittedBounds[eye];
        uint32_t cropX = static_cast<uint32_t>(bounds.uMin * imgWidth);
        uint32_t cropY = static_cast<uint32_t>(bounds.vMin * imgHeight);
        uint32_t cropW = static_cast<uint32_t>((bounds.uMax - bounds.uMin) * imgWidth);
        uint32_t cropH = static_cast<uint32_t>((bounds.vMax - bounds.vMin) * imgHeight);

        if (cropW == 0) cropW = imgWidth;
        if (cropH == 0) cropH = imgHeight;
        if (cropX + cropW > imgWidth) cropW = imgWidth - cropX;
        if (cropY + cropH > imgHeight) cropH = imgHeight - cropY;

        VkDeviceSize requiredSize = static_cast<VkDeviceSize>(cropW) * cropH * 4;

        // Ensure staging buffer is large enough
        if (m_stagingSize < requiredSize)
        {
            if (m_stagingBuffer != VK_NULL_HANDLE)
                vkDestroyBuffer(m_vkDevice, m_stagingBuffer, nullptr);
            if (m_stagingMemory != VK_NULL_HANDLE)
                vkFreeMemory(m_vkDevice, m_stagingMemory, nullptr);
            m_stagingBuffer = VK_NULL_HANDLE;
            m_stagingMemory = VK_NULL_HANDLE;

            VkBufferCreateInfo bufInfo{};
            bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufInfo.size = requiredSize;
            bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(m_vkDevice, &bufInfo, nullptr, &m_stagingBuffer) != VK_SUCCESS)
                continue;

            VkMemoryRequirements memReqs;
            vkGetBufferMemoryRequirements(m_vkDevice, m_stagingBuffer, &memReqs);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReqs.size;
            allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            if (vkAllocateMemory(m_vkDevice, &allocInfo, nullptr, &m_stagingMemory) != VK_SUCCESS)
            {
                vkDestroyBuffer(m_vkDevice, m_stagingBuffer, nullptr);
                m_stagingBuffer = VK_NULL_HANDLE;
                continue;
            }

            vkBindBufferMemory(m_vkDevice, m_stagingBuffer, m_stagingMemory, 0);
            m_stagingSize = requiredSize;
        }

        // Record and submit copy commands
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkResetCommandBuffer(m_vkCommandBuffer, 0);
        vkBeginCommandBuffer(m_vkCommandBuffer, &beginInfo);

        // Transition image to transfer src layout
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = srcImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(m_vkCommandBuffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy image region to staging buffer
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { static_cast<int32_t>(cropX), static_cast<int32_t>(cropY), 0 };
        region.imageExtent = { cropW, cropH, 1 };

        vkCmdCopyImageToBuffer(m_vkCommandBuffer, srcImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_stagingBuffer, 1, &region);

        vkEndCommandBuffer(m_vkCommandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_vkCommandBuffer;

        vkQueueSubmit(m_vkQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_vkQueue);

        // Map and send frame data
        void* mapped = nullptr;
        if (vkMapMemory(m_vkDevice, m_stagingMemory, 0, requiredSize, 0, &mapped) == VK_SUCCESS)
        {
            Frame frame { static_cast<const uint8_t*>(mapped), cropW, cropH, static_cast<uint32_t>(eye), s_lastSubmittedPose };
            m_pSocketManager->SendFrame(frame);
            vkUnmapMemory(m_vkDevice, m_stagingMemory);
        }
    }
}

#endif // platform
