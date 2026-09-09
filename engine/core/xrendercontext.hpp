#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <functional>

struct XBuffer;
struct XImage;
struct XGraphicsPipeline;
struct XComputePipeline;

struct XBuffer {
    VkBuffer buffer { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    VkDeviceSize size { 0 };
    void* mappedData { nullptr };
};

struct XImage {
    VkImage image { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    VkImageView view { VK_NULL_HANDLE };
    VkSampler sampler { VK_NULL_HANDLE };
    VkFormat format { VK_FORMAT_UNDEFINED };
    uint32_t width { 0 };
    uint32_t height { 0 };
    uint32_t layers { 1 };
    uint32_t mipLevels { 1 };
    VkImageViewType viewType { VK_IMAGE_VIEW_TYPE_2D };
    VkImageLayout layout { VK_IMAGE_LAYOUT_UNDEFINED };
};

struct XGraphicsPipeline {
    VkPipeline pipeline { VK_NULL_HANDLE };
    VkPipelineLayout layout { VK_NULL_HANDLE };
    VkDescriptorSetLayout descriptorSetLayout { VK_NULL_HANDLE };
    std::vector<VkDescriptorSet> descriptorSets;
};

struct XComputePipeline {
    VkPipeline pipeline { VK_NULL_HANDLE };
    VkPipelineLayout layout { VK_NULL_HANDLE };
    VkDescriptorSetLayout descriptorSetLayout { VK_NULL_HANDLE };
    std::vector<VkDescriptorSet> descriptorSets;
};

struct XDescriptorBinding {
    uint32_t binding;
    VkDescriptorType type;
    VkShaderStageFlags stages;

    XBuffer* buffer = nullptr;
    XImage* image = nullptr;

    XDescriptorBinding(const uint32_t bindingIndex, const VkDescriptorType descriptorType, const VkShaderStageFlags shaderStages, XBuffer* buf)
        : binding(bindingIndex), type(descriptorType), stages(shaderStages), buffer(buf), image(nullptr) {
    }

    XDescriptorBinding(const uint32_t bindingIndex, const VkDescriptorType descriptorType, const VkShaderStageFlags shaderStages, XImage* img)
        : binding(bindingIndex), type(descriptorType), stages(shaderStages), buffer(nullptr), image(img) {
    }
};

struct XPushConstantRange {
    VkShaderStageFlags stages;
    uint32_t offset;
    uint32_t size;

    XPushConstantRange(const VkShaderStageFlags shaderStages, const uint32_t sz, const uint32_t off = 0) : stages(shaderStages), offset(off), size(sz) {
    }
};

struct XVertexInputBinding {
    uint32_t binding { 0 };
    uint32_t stride { 0 };
    VkVertexInputRate inputRate { VK_VERTEX_INPUT_RATE_VERTEX };

    XVertexInputBinding() = default;

    XVertexInputBinding(const uint32_t bindingIndex, const uint32_t vertexStride, const VkVertexInputRate rate = VK_VERTEX_INPUT_RATE_VERTEX)
        : binding(bindingIndex), stride(vertexStride), inputRate(rate) {
    }
};

struct XVertexInputAttribute {
    uint32_t location { 0 };
    uint32_t binding { 0 };
    VkFormat format { VK_FORMAT_UNDEFINED };
    uint32_t offset { 0 };

    XVertexInputAttribute() = default;

    XVertexInputAttribute(const uint32_t attributeLocation, const uint32_t bindingIndex, const VkFormat attributeFormat, const uint32_t attributeOffset)
        : location(attributeLocation), binding(bindingIndex), format(attributeFormat), offset(attributeOffset) {
    }
};

struct XGraphicsPipelineOptions {
    std::vector<XVertexInputBinding> vertexBindings;
    std::vector<XVertexInputAttribute> vertexAttributes;

    VkPrimitiveTopology topology { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    bool primitiveRestart { false };

    VkCullModeFlags cullMode { VK_CULL_MODE_NONE };
    VkFrontFace frontFace { VK_FRONT_FACE_COUNTER_CLOCKWISE };

    bool depthTest { false };
    bool depthWrite { true };
    VkCompareOp depthCompareOp { VK_COMPARE_OP_LESS };

    bool blendEnable { true };
    VkBlendFactor srcColorBlendFactor { VK_BLEND_FACTOR_ONE };
    VkBlendFactor dstColorBlendFactor { VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA };
    VkBlendOp colorBlendOp { VK_BLEND_OP_ADD };
    VkBlendFactor srcAlphaBlendFactor { VK_BLEND_FACTOR_ONE };
    VkBlendFactor dstAlphaBlendFactor { VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA };
    VkBlendOp alphaBlendOp { VK_BLEND_OP_ADD };
    VkColorComponentFlags colorWriteMask { VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };

    VkRenderPass renderPass { VK_NULL_HANDLE };
};

class XRenderContext {
public:
    XRenderContext() = default;
    ~XRenderContext();

    void Init(const uint32_t width, const uint32_t height, const char* title);
    void Cleanup();

    XBuffer CreateBuffer(const VkDeviceSize size, const VkBufferUsageFlags usage, const VmaMemoryUsage memoryUsage);
    XImage CreateImage(const uint32_t width, const uint32_t height, const VkFormat format, const VkImageUsageFlags usage, const VkImageAspectFlags aspect,
                      const uint32_t mipLevels = 1);
    uint32_t MaxMipLevels(const uint32_t width, const uint32_t height) const;
    void GenerateMipmaps(XImage& image);
    XImage CreateImageArray(const uint32_t width, const uint32_t height, const uint32_t layers, const VkFormat format, const VkImageUsageFlags usage,
                           const VkImageAspectFlags aspect);
    VkSampler CreateSampler(const VkFilter minFilter = VK_FILTER_LINEAR, const VkFilter magFilter = VK_FILTER_LINEAR,
                            const VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT, const float maxAnisotropy = 1.0f);

    void DestroyBuffer(XBuffer& buffer);
    void DestroyImage(XImage& image);
    void DestroySampler(VkSampler& sampler);

    void UploadBuffer(XBuffer& buffer, const void* data, const VkDeviceSize size, const VkDeviceSize offset = 0);
    void DownloadBuffer(XBuffer& buffer, void* data, const VkDeviceSize size, const VkDeviceSize offset = 0);
    void UploadImage(XImage& image, const void* data, const VkDeviceSize dataSize);
    void DownloadImage(XImage& image, void* data, const VkDeviceSize dataSize);
    void FillBuffer(XBuffer& buffer, const VkDeviceSize offset, const VkDeviceSize size, const uint32_t data);

    void* MapBuffer(XBuffer& buffer);
    void UnmapBuffer(XBuffer& buffer);

    void CopyBuffer(XBuffer& from, XBuffer& to, const uint32_t size);
    void CopyBufferToImage(XBuffer& stagingBuffer, XImage& targetImage, const uint32_t width, const uint32_t height);
    void CopyImageToImage(XImage& srcImage, XImage& dstImage);
    void CopyImageToImageArrayLayer(XImage& srcImage, XImage& dstArrayImage, const uint32_t layerIndex);
    void CopyImageArrayLayerToImage(XImage& srcArrayImage, const uint32_t layerIndex, XImage& dstImage);
    void CopyBufferToImageArray(XBuffer& stagingBuffer, XImage& targetArrayImage, const VkDeviceSize layerStride);

    XGraphicsPipeline CreateGraphicsPipeline(const std::string& vertShaderPath, const std::string& fragShaderPath,
                                            const std::vector<XDescriptorBinding>& bindings, const std::vector<XPushConstantRange>& pushConstantRanges = {},
                                            const XGraphicsPipelineOptions& options = {});

    XComputePipeline CreateComputePipeline(const std::string& compShaderPath, const std::vector<XDescriptorBinding>& bindings,
                                          const std::vector<XPushConstantRange>& pushConstantRanges = {});

    void DestroyGraphicsPipeline(XGraphicsPipeline& pipeline);
    void DestroyComputePipeline(XComputePipeline& pipeline);

    void UpdateDescriptors(XGraphicsPipeline& pipeline, const std::vector<XDescriptorBinding>& bindings);
    void UpdateDescriptors(XComputePipeline& pipeline, const std::vector<XDescriptorBinding>& bindings);

    template <typename T> void PushConstants(XGraphicsPipeline& pipeline, VkShaderStageFlags stages, const T& data, const uint32_t offset = 0) {
        PushConstantsRaw(pipeline, stages, &data, sizeof(T), offset);
    }

    template <typename T> void PushConstants(XComputePipeline& pipeline, VkShaderStageFlags stages, const T& data, const uint32_t offset = 0) {
        PushConstantsRaw(pipeline, stages, &data, sizeof(T), offset);
    }

    void PushConstantsRaw(XGraphicsPipeline& pipeline, const VkShaderStageFlags stages, const void* data, const uint32_t size, const uint32_t offset = 0);
    void PushConstantsRaw(XComputePipeline& pipeline, const VkShaderStageFlags stages, const void* data, const uint32_t size, const uint32_t offset = 0);

    void BeginFrame();
    void EndFrame();

    void BeginRenderPass(const VkFramebuffer framebuffer, const uint32_t width, const uint32_t height,
                         const VkRenderPass targetRenderPass = VK_NULL_HANDLE);
    void EndRenderPass();

    void ComputeToGraphicsBarrier();
    void GraphicsToComputeBarrier();
    void ComputeToComputeBarrier();
    void ComputeToTransferBarrier();
    void TransferToComputeBarrier();
    void FullPipelineBarrier();
    void ImageBarrier(XImage& image, const VkImageLayout oldLayout, const VkImageLayout newLayout, const VkAccessFlags srcAccess, const VkAccessFlags dstAccess,
                      const VkPipelineStageFlags srcStage, const VkPipelineStageFlags dstStage);

    void BindGraphicsPipeline(XGraphicsPipeline& pipeline);
    void BindComputePipeline(XComputePipeline& pipeline);

    void BindVertexBuffer(XBuffer& buffer, const uint32_t binding = 0, const VkDeviceSize offset = 0);
    void BindVertexBuffers(const std::vector<XBuffer*>& buffers, const uint32_t firstBinding = 0, const std::vector<VkDeviceSize>& offsets = {});
    void BindIndexBuffer(XBuffer& buffer, const VkIndexType indexType = VK_INDEX_TYPE_UINT32, const VkDeviceSize offset = 0);

    void SetViewport(const float x, const float y, const float width, const float height, const float minDepth = 0.0f, const float maxDepth = 1.0f);
    void SetScissor(const int32_t x, const int32_t y, const uint32_t width, const uint32_t height);

    void Draw(const uint32_t vertexCount, const uint32_t instanceCount = 1, const uint32_t firstVertex = 0, const uint32_t firstInstance = 0);
    void DrawIndexed(const uint32_t indexCount, const uint32_t instanceCount = 1, const uint32_t firstIndex = 0, const int32_t vertexOffset = 0,
                     const uint32_t firstInstance = 0);
    void DrawIndirect(XBuffer& argsBuffer, const VkDeviceSize offset = 0, const uint32_t drawCount = 1,
                      const uint32_t stride = sizeof(VkDrawIndirectCommand));
    void DrawIndexedIndirect(XBuffer& argsBuffer, const VkDeviceSize offset = 0, const uint32_t drawCount = 1,
                             const uint32_t stride = sizeof(VkDrawIndexedIndirectCommand));
    void DrawFullscreenTriangle();

    void Dispatch(const uint32_t groupCountX, const uint32_t groupCountY, const uint32_t groupCountZ);
    void DispatchIndirect(XBuffer& argsBuffer, const uint32_t offset);

    VkCommandBuffer GetCurrentCommandBuffer() const {
        return commandBuffers.empty() ? VK_NULL_HANDLE : commandBuffers[currentFrame];
    }

    VkRenderPass CreateRenderPass(const VkFormat colorFormat, const bool enableDepth = true,
                                  const VkImageLayout finalLayout = VK_IMAGE_LAYOUT_GENERAL);
    void DestroyRenderPass(VkRenderPass& targetRenderPass);

    VkFramebuffer CreateFramebuffer(const std::vector<VkImageView>& attachments, const uint32_t width, const uint32_t height,
                                    const VkRenderPass targetRenderPass = VK_NULL_HANDLE);
    void DestroyFramebuffer(const VkFramebuffer framebuffer);

    bool IsKeyPressed(const int key) const;
    bool IsKeyUp(const int key) const;
    bool IsMouseButtonPressed(const int button) const;
    glm::vec2 GetMousePos() const;

    bool ShouldClose() const;
    void PollEvents();

    GLFWwindow* GetWindow() const {
        return window;
    }
    uint32_t GetWidth() const {
        return swapchainExtent.width;
    }
    uint32_t GetHeight() const {
        return swapchainExtent.height;
    }
    VkFramebuffer GetCurrentSwapchainFramebuffer() const {
        return swapchainFramebuffers[currentImageIndex];
    }
    VkRenderPass GetRenderPass() const {
        return renderPass;
    }
    bool IsFrameStarted() const {
        return frameStarted;
    }
    uint32_t GetCurrentFrameIndex() const {
        return currentFrame;
    }
    uint32_t GetCurrentSwapchainImageIndex() const {
        return currentImageIndex;
    }
    static constexpr uint32_t GetMaxFramesInFlight() {
        return MAX_FRAMES_IN_FLIGHT;
    }

    void WaitDeviceIdle();

private:
    void ReleaseDeferredFrameBuffers(const uint32_t frameIndex);
    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapchain();
    void CreateSwapchainRenderPass();
    void RecreateSwapchain();
    void CleanupSwapchain();
    VkCommandBuffer FrameCommandBuffer() const;
    VkImageAspectFlags AspectMaskForFormat(const VkFormat format) const;
    void CreateFramebuffers();
    void CreateCommandPools();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void CreateDescriptorPool();
    void CreateDepthResources();

    VkShaderModule LoadShaderModule(const std::string& path);
    void TransitionImageLayout(const VkImage image, const VkFormat format, const VkImageLayout oldLayout, const VkImageLayout newLayout,
                               VkCommandBuffer cmdBuf = VK_NULL_HANDLE);
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(const VkCommandBuffer commandBuffer);
    VkFormat FindDepthFormat();

    VkInstance instance { VK_NULL_HANDLE };
    VkDebugUtilsMessengerEXT debugMessenger { VK_NULL_HANDLE };
    VkSurfaceKHR surface { VK_NULL_HANDLE };
    VkPhysicalDevice physicalDevice { VK_NULL_HANDLE };
    VkDevice device { VK_NULL_HANDLE };
    VkQueue graphicsQueue { VK_NULL_HANDLE };
    VkQueue presentQueue { VK_NULL_HANDLE };

    VkSwapchainKHR swapchain { VK_NULL_HANDLE };
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    std::vector<VkImageLayout> swapchainImageLayouts;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;

    VkFormat depthFormat;
    std::vector<XImage> depthImages;

    VkRenderPass renderPass { VK_NULL_HANDLE };
    std::unordered_map<VkRenderPass, uint32_t> renderPassAttachmentCounts;

    VkCommandPool commandPool { VK_NULL_HANDLE };
    std::vector<VkCommandBuffer> commandBuffers;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT { 1 };
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;
    std::vector<std::vector<XBuffer>> deferredFrameBuffers;
    uint32_t currentFrame { 0 };
    uint32_t currentImageIndex { 0 };

    VmaAllocator allocator { VK_NULL_HANDLE };

    VkDescriptorPool descriptorPool { VK_NULL_HANDLE };

    GLFWwindow* window { nullptr };

    bool samplerAnisotropySupported { false };
    float maxSamplerAnisotropy { 1.0f };

    uint32_t graphicsQueueFamily { 0 };
    uint32_t presentQueueFamily { 0 };

    bool frameStarted { false };
    bool inRenderPass { false };
    bool swapchainImageUsedThisFrame { false };
    XGraphicsPipeline* currentGraphicsPipeline { nullptr };
    XComputePipeline* currentComputePipeline { nullptr };
};
