#pragma once

#include <openvr_driver.h>
#include <vector>
#include <thread>
#include <atomic>
#include "../socket/socket_manager.h"
#include "../mpsc/channel.h"

#ifdef _WIN32
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#else
#include <vulkan/vulkan.h>
#endif

class Driver : public vr::ITrackedDeviceServerDriver,
               public vr::IVRDisplayComponent,
               public vr::IVRDriverDirectModeComponent
{
public:
    Driver(mpsc::Receiver<Pose> poseReceiver, SocketManager* socketManager);
    ~Driver();

    // ITrackedDeviceServerDriver interface
    vr::EVRInitError Activate(uint32_t unObjectId) override;
    void Deactivate() override;
    void EnterStandby() override;
    void* GetComponent(const char* pchComponentNameAndVersion) override;
    void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override;
    vr::DriverPose_t GetPose() override;

    // IVRDisplayComponent interface
    void GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight) override;
    bool IsDisplayOnDesktop() override;
    bool IsDisplayRealDisplay() override;
    void GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight) override;
    void GetEyeOutputViewport(vr::EVREye eEye, uint32_t* pnX, uint32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight) override;
    void GetProjectionRaw(vr::EVREye eEye, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom) override;
    vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye eEye, float fU, float fV) override;
    bool ComputeInverseDistortion(vr::HmdVector2_t* pResult, vr::EVREye eEye, uint32_t unChannel, float fU, float fV) override;

    // IVRDriverDirectModeComponent interface
    void CreateSwapTextureSet(uint32_t unPid, const SwapTextureSetDesc_t* pSwapTextureSetDesc, SwapTextureSet_t* pOutSwapTextureSet) override;
    void DestroySwapTextureSet(vr::SharedTextureHandle_t sharedTextureHandle) override;
    void DestroyAllSwapTextureSets(uint32_t unPid) override;
    void GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t sharedTextureHandles[2], uint32_t (*pIndices)[2]) override;
    void SubmitLayer(const SubmitLayerPerEye_t (&perEye)[2]) override;
    void Present(vr::SharedTextureHandle_t syncTexture) override;
    void PostPresent(const Throttling_t* pThrottling) override;
    void GetFrameTiming(vr::DriverDirectMode_FrameTiming* pFrameTiming) override;

    // Public methods
    const char* GetSerialNumber() const { return m_serialNumber.c_str(); }
    void ProcessEvent(const vr::VREvent_t& event);

private:
    void PoseUpdateThreadFunc(std::stop_token st);

    uint32_t m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
    std::string m_serialNumber = "OVD-HMD-001";

    // Display properties
    int32_t m_renderWidth    = 1080;
    int32_t m_renderHeight   = 1080;
    float m_displayFrequency = 90.0f;
    float m_ipd = 0.063f; // 63mm

#ifdef _WIN32
    // D3D11 resources
    bool InitD3D11();
    void CleanupD3D11();

    ComPtr<ID3D11Device> m_pD3DDevice;
    ComPtr<ID3D11DeviceContext> m_pD3DContext;

    ComPtr<ID3D11Texture2D> m_pStagingTexture;
    uint32_t m_stagingWidth = 0;
    uint32_t m_stagingHeight = 0;
    DXGI_FORMAT m_stagingFormat = DXGI_FORMAT_UNKNOWN;

    vr::SharedTextureHandle_t m_syncTextureHandle = 0;
    ComPtr<ID3D11Texture2D> m_pSyncTexture;

    struct SwapTextureSetData
    {
        uint32_t pid;
        ComPtr<ID3D11Texture2D> textures[3];
        HANDLE sharedHandles[3];
        uint32_t currentIndex;
    };
#else
    // Vulkan resources for importing and reading back shared textures
    bool InitVulkan();
    void CleanupVulkan();
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    static uint32_t DXGIFormatToVkFormat(uint32_t dxgiFormat);

    VkInstance m_vkInstance = VK_NULL_HANDLE;
    VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkQueue m_vkQueue = VK_NULL_HANDLE;
    uint32_t m_vkQueueFamily = 0;
    VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_vkCommandBuffer = VK_NULL_HANDLE;

    // Staging buffer for CPU readback
    VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize m_stagingSize = 0;

    struct ImportedTexture
    {
        int fd = -1;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    struct SwapTextureSetData
    {
        uint32_t pid;
        vr::SharedTextureHandle_t handles[3];
        ImportedTexture imported[3]; // Imported into our Vulkan device for readback
        uint32_t width, height;
        uint32_t currentIndex;
    };
#endif

    std::vector<SwapTextureSetData> m_swapTextureSets;

    // Networking
    SocketManager* m_pSocketManager;

    // Head pose channel
    mpsc::Receiver<Pose> m_poseReceiver;
    std::jthread m_poseThread;

    // Frame counter
    std::atomic<uint64_t> m_frameCount{0};

    // Proximity sensor (always on to prevent standby)
    vr::VRInputComponentHandle_t m_proximityHandle = vr::k_ulInvalidInputComponentHandle;
};
