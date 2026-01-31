#pragma once

#include <openvr_driver.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <thread>
#include <atomic>
#include "../socket/socket_manager.h"
#include "../mpsc/channel.h"

using Microsoft::WRL::ComPtr;

class Driver : public vr::ITrackedDeviceServerDriver,
               public vr::IVRDisplayComponent,
               public vr::IVRDriverDirectModeComponent
{
public:
    Driver(mpsc::Receiver<Position> positionReceiver, SocketManager* socketManager);
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
    bool InitD3D11();
    void CleanupD3D11();
    void PoseUpdateThreadFunc(std::stop_token st);

    uint32_t m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
    std::string m_serialNumber = "OVD-HMD-001";

    // Display properties
    int32_t m_renderWidth    = 1920;
    int32_t m_renderHeight   = 1080;
    float m_displayFrequency = 90.0f;
    float m_ipd = 0.063f; // 63mm

    // D3D11 resources
    ComPtr<ID3D11Device> m_pD3DDevice;
    ComPtr<ID3D11DeviceContext> m_pD3DContext;

    // Staging texture for CPU readback
    ComPtr<ID3D11Texture2D> m_pStagingTexture;
    uint32_t m_stagingWidth = 0;
    uint32_t m_stagingHeight = 0;
    DXGI_FORMAT m_stagingFormat = DXGI_FORMAT_UNKNOWN;

    // Texture management
    struct SwapTextureSetData
    {
        uint32_t pid;
        ComPtr<ID3D11Texture2D> textures[3];
        HANDLE sharedHandles[3];
        uint32_t currentIndex;
    };
    std::vector<SwapTextureSetData> m_swapTextureSets;

    // Networking
    SocketManager* m_pSocketManager;

    // Position channel
    mpsc::Receiver<Position> m_positionReceiver;
    std::jthread m_poseThread;

    // Frame counter
    std::atomic<uint64_t> m_frameCount{0};
};
