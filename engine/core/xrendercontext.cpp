#include "core/xrendercontext.hpp"
#include <fstream>
#include <set>
#include <array>
#include <algorithm>
#include <iostream>
#include <cstring>

#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION_LAYERS = false;
#else
constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif

static const std::vector<const char*> VALIDATION_LAYERS = { "VK_LAYER_KHRONOS_validation" };

static const std::vector<const char*> DEVICE_EXTENSIONS = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME,
                                                            VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME };

static const std::vector<const char*> REQUIRED_FEATURES = { "shaderBufferFloat32Atomics", "shaderBufferFloat32AtomicAdd", "shaderSharedFloat32Atomics",
                                                            "shaderSharedFloat32AtomicAdd" };

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    (void)messageType;
    (void)pUserData;

    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "VALIDATION: " << pCallbackData->pMessage << std::endl;
    }

    return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                      VkDebugUtilsMessengerEXT* pDebugMessenger) {

    const auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }

    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {

    const auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");

    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

XRenderContext::~XRenderContext() {
    Cleanup();
}

void XRenderContext::Init(const uint32_t width, const uint32_t height, const char* title) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("Failed to create GLFW window");
    }

    CreateInstance();
    SetupDebugMessenger();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;

    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator");
    }

    CreateSwapchain();
    CreateDepthResources();
    CreateSwapchainRenderPass();
    CreateFramebuffers();
    CreateCommandPools();
    CreateCommandBuffers();
    CreateSyncObjects();
    deferredFrameBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    CreateDescriptorPool();
}

void XRenderContext::Cleanup() {
    if (device) {
        vkDeviceWaitIdle(device);
    }

    for (uint32_t ii = 0u; ii < deferredFrameBuffers.size(); ii++) {
        ReleaseDeferredFrameBuffers(ii);
    }
    deferredFrameBuffers.clear();

    for (size_t ii = 0; ii < imageAvailableSemaphores.size(); ii++) {
        if (imageAvailableSemaphores[ii]) {
            vkDestroySemaphore(device, imageAvailableSemaphores[ii], nullptr);
        }
    }
    imageAvailableSemaphores.clear();

    for (size_t ii = 0; ii < inFlightFences.size(); ii++) {
        if (inFlightFences[ii]) {
            vkDestroyFence(device, inFlightFences[ii], nullptr);
        }
    }
    inFlightFences.clear();

    if (commandPool) {
        vkDestroyCommandPool(device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }
    commandBuffers.clear();

    if (descriptorPool) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    if (device) {
        CleanupSwapchain();
    }

    DestroyRenderPass(renderPass);
    renderPassAttachmentCounts.clear();

    if (allocator) {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }

    if (device) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (ENABLE_VALIDATION_LAYERS && debugMessenger) {
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }

    if (surface) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    if (instance) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }

    if (window) {
        glfwDestroyWindow(window);
        glfwTerminate();
        window = nullptr;
    }

    physicalDevice = VK_NULL_HANDLE;
    graphicsQueue = VK_NULL_HANDLE;
    presentQueue = VK_NULL_HANDLE;
    frameStarted = false;
    inRenderPass = false;
    currentGraphicsPipeline = nullptr;
    currentComputePipeline = nullptr;
}

void XRenderContext::WaitDeviceIdle() {
    vkDeviceWaitIdle(device);
}

void XRenderContext::CreateInstance() {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "XRender";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "XEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (ENABLE_VALIDATION_LAYERS) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

#ifdef __APPLE__
    {
        uint32_t availableCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
        std::vector<VkExtensionProperties> availableInstanceExtensions(availableCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, availableInstanceExtensions.data());

        bool hasPortabilityEnumeration = false;
        for (const auto& ext : availableInstanceExtensions) {
            if (strcmp(ext.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
                hasPortabilityEnumeration = true;
                break;
            }
        }

        if (hasPortabilityEnumeration) {
            extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
    }
#endif

    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    std::cout << "XRenderContext: available vulkan layers:\n";
    for (const auto& layer : availableLayers) {
        std::cout << " " << layer.layerName << " - " << layer.description << "\n";
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
    if (ENABLE_VALIDATION_LAYERS) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
        createInfo.ppEnabledLayerNames = VALIDATION_LAYERS.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = DebugCallback;
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    const VkResult instanceResult = vkCreateInstance(&createInfo, nullptr, &instance);
    if (instanceResult != VK_SUCCESS) {
        std::cerr << "XRenderContext: vkCreateInstance failed with VkResult=" << instanceResult << "\n";
        std::cerr << "XRenderContext: requested extensions:\n";
        for (const auto* ext : extensions) {
            std::cerr << "  " << ext << "\n";
        }
        throw std::runtime_error("failed to create vk instance");
    }
}

void XRenderContext::SetupDebugMessenger() {
    if (!ENABLE_VALIDATION_LAYERS) {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger");
    }
}

void XRenderContext::CreateSurface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface");
    }
}

void XRenderContext::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPU with vk support");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        bool foundGraphics = false;
        bool foundPresent = false;

        for (uint32_t ii = 0; ii < queueFamilies.size(); ii++) {
            if (queueFamilies[ii].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsQueueFamily = ii;
                foundGraphics = true;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, ii, surface, &presentSupport);
            if (presentSupport) {
                presentQueueFamily = ii;
                foundPresent = true;
            }

            if (foundGraphics && foundPresent) {
                break;
            }
        }

        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        bool hasSwapchain = false;
        for (const auto& extension : availableExtensions) {
            if (strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                hasSwapchain = true;
                break;
            }
        }

        if (foundGraphics && foundPresent && hasSwapchain) {
            physicalDevice = device;
            return;
        }
    }

    throw std::runtime_error("failed to find a suitable GPU");
}

VkFormat XRenderContext::FindDepthFormat() {
    const std::vector<VkFormat> candidates = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };

    for (VkFormat const format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported depth format");
}

void XRenderContext::CreateLogicalDevice() {
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> const uniqueQueueFamilies = { graphicsQueueFamily, presentQueueFamily };

    float const queuePriority = 1.0f;
    for (const uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures supportedFeatures = {};
    vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);

    VkPhysicalDeviceProperties deviceProperties = {};
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

    samplerAnisotropySupported = supportedFeatures.samplerAnisotropy == VK_TRUE;
    maxSamplerAnisotropy = samplerAnisotropySupported ? deviceProperties.limits.maxSamplerAnisotropy : 1.0f;

    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.samplerAnisotropy = samplerAnisotropySupported ? VK_TRUE : VK_FALSE;

    std::vector<const char*> enabledDeviceExtensions;
    enabledDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    bool hasAtomicFloat = false;
    bool hasAtomicFloat2 = false;
    bool hasAtomicInt64 = false;
    bool hasPortabilitySubset = false;
    {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());
        for (const auto& ext : availableExtensions) {
            if (strcmp(ext.extensionName, VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME) == 0) {
                hasAtomicFloat = true;
            } else if (strcmp(ext.extensionName, VK_EXT_SHADER_ATOMIC_FLOAT_2_EXTENSION_NAME) == 0) {
                hasAtomicFloat2 = true;
            } else if (strcmp(ext.extensionName, VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME) == 0) {
                hasAtomicInt64 = true;
            } else if (strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0) {
                hasPortabilitySubset = true;
            }
        }
    }
    if (hasAtomicFloat) {
        enabledDeviceExtensions.push_back(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
    } else {
        std::cerr << "XRenderContext: warning: " << VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME << " not available\n";
    }
    if (hasAtomicInt64) {
        enabledDeviceExtensions.push_back(VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME);
    } else {
        std::cerr << "XRenderContext: warning: " << VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME << " not available\n";
    }
    if (hasPortabilitySubset) {
        enabledDeviceExtensions.push_back("VK_KHR_portability_subset");
    }

    void* pNextChain = nullptr;
    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloatFeatures = {};
    VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT atomicFloat2Features = {};
    VkPhysicalDeviceShaderAtomicInt64Features atomicInt64Features = {};

    bool needsAtomicFloat = false;
    bool needsAtomicFloat2 = false;

    for (const auto& featureName : REQUIRED_FEATURES) {
        if (strcmp(featureName, "shaderBufferFloat32Atomics") == 0) {
            atomicFloatFeatures.shaderBufferFloat32Atomics = VK_TRUE;
            needsAtomicFloat = true;
        } else if (strcmp(featureName, "shaderBufferFloat32AtomicAdd") == 0) {
            atomicFloatFeatures.shaderBufferFloat32AtomicAdd = VK_TRUE;
            needsAtomicFloat = true;
        } else if (strcmp(featureName, "shaderBufferFloat64Atomics") == 0) {
            atomicFloatFeatures.shaderBufferFloat64Atomics = VK_TRUE;
            needsAtomicFloat = true;
        } else if (strcmp(featureName, "shaderBufferFloat64AtomicAdd") == 0) {
            atomicFloatFeatures.shaderBufferFloat64AtomicAdd = VK_TRUE;
            needsAtomicFloat = true;
        } else if (strcmp(featureName, "shaderSharedFloat32Atomics") == 0) {
            atomicFloatFeatures.shaderSharedFloat32Atomics = VK_TRUE;
            needsAtomicFloat = true;
        } else if (strcmp(featureName, "shaderSharedFloat32AtomicAdd") == 0) {
            atomicFloatFeatures.shaderSharedFloat32AtomicAdd = VK_TRUE;
            needsAtomicFloat = true;
        } else if (strcmp(featureName, "shaderSharedFloat64Atomics") == 0) {
            atomicFloatFeatures.shaderSharedFloat64Atomics = VK_TRUE;
            needsAtomicFloat = true;
        } else if (strcmp(featureName, "shaderSharedFloat64AtomicAdd") == 0) {
            atomicFloatFeatures.shaderSharedFloat64AtomicAdd = VK_TRUE;
            needsAtomicFloat = true;
        } else if (strcmp(featureName, "shaderImageFloat32Atomics") == 0) {
            atomicFloatFeatures.shaderImageFloat32Atomics = VK_TRUE;
            needsAtomicFloat = true;
        } else if (strcmp(featureName, "shaderImageFloat32AtomicAdd") == 0) {
            atomicFloatFeatures.shaderImageFloat32AtomicAdd = VK_TRUE;
            needsAtomicFloat = true;
        } else if (strcmp(featureName, "shaderBufferFloat16Atomics") == 0) {
            atomicFloat2Features.shaderBufferFloat16Atomics = VK_TRUE;
            needsAtomicFloat2 = true;
        } else if (strcmp(featureName, "shaderBufferFloat16AtomicAdd") == 0) {
            atomicFloat2Features.shaderBufferFloat16AtomicAdd = VK_TRUE;
            needsAtomicFloat2 = true;
        } else if (strcmp(featureName, "shaderBufferFloat16AtomicMinMax") == 0) {
            atomicFloat2Features.shaderBufferFloat16AtomicMinMax = VK_TRUE;
            needsAtomicFloat2 = true;
        } else if (strcmp(featureName, "shaderBufferFloat32AtomicMinMax") == 0) {
            atomicFloat2Features.shaderBufferFloat32AtomicMinMax = VK_TRUE;
            needsAtomicFloat2 = true;
        } else if (strcmp(featureName, "shaderBufferFloat64AtomicMinMax") == 0) {
            atomicFloat2Features.shaderBufferFloat64AtomicMinMax = VK_TRUE;
            needsAtomicFloat2 = true;
        } else if (strcmp(featureName, "shaderSharedFloat16Atomics") == 0) {
            atomicFloat2Features.shaderSharedFloat16Atomics = VK_TRUE;
            needsAtomicFloat2 = true;
        } else if (strcmp(featureName, "shaderSharedFloat16AtomicAdd") == 0) {
            atomicFloat2Features.shaderSharedFloat16AtomicAdd = VK_TRUE;
            needsAtomicFloat2 = true;
        } else if (strcmp(featureName, "shaderSharedFloat16AtomicMinMax") == 0) {
            atomicFloat2Features.shaderSharedFloat16AtomicMinMax = VK_TRUE;
            needsAtomicFloat2 = true;
        } else if (strcmp(featureName, "shaderSharedFloat32AtomicMinMax") == 0) {
            atomicFloat2Features.shaderSharedFloat32AtomicMinMax = VK_TRUE;
            needsAtomicFloat2 = true;
        } else if (strcmp(featureName, "shaderSharedFloat64AtomicMinMax") == 0) {
            atomicFloat2Features.shaderSharedFloat64AtomicMinMax = VK_TRUE;
            needsAtomicFloat2 = true;
        } else if (strcmp(featureName, "shaderImageFloat32AtomicMinMax") == 0) {
            atomicFloat2Features.shaderImageFloat32AtomicMinMax = VK_TRUE;
            needsAtomicFloat2 = true;
        } else if (strcmp(featureName, "sparseImageFloat32AtomicMinMax") == 0) {
            atomicFloat2Features.sparseImageFloat32AtomicMinMax = VK_TRUE;
            needsAtomicFloat2 = true;
        }
    }

    if (needsAtomicFloat && hasAtomicFloat) {
        atomicFloatFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT;
        atomicFloatFeatures.pNext = pNextChain;
        pNextChain = &atomicFloatFeatures;
    }

    if (needsAtomicFloat2 && hasAtomicFloat2) {
        atomicFloat2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT;
        atomicFloat2Features.pNext = pNextChain;
        pNextChain = &atomicFloat2Features;
    }

    if (hasAtomicInt64) {
        atomicInt64Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;
        atomicInt64Features.shaderBufferInt64Atomics = VK_TRUE;
        atomicInt64Features.pNext = pNextChain;
        pNextChain = &atomicInt64Features;
    }

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = pNextChain;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledDeviceExtensions.data();

    if (ENABLE_VALIDATION_LAYERS) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
        createInfo.ppEnabledLayerNames = VALIDATION_LAYERS.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device");
    }

    vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentQueueFamily, 0, &presentQueue);
}

void XRenderContext::CreateSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = format;
            break;
        }
    }

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& mode : presentModes) {
        if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            presentMode = mode;
            break;
        }
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = mode;
        }
    }

    if (capabilities.currentExtent.width != UINT32_MAX) {
        swapchainExtent = capabilities.currentExtent;
    } else {
        int width;
        int height;
        glfwGetFramebufferSize(window, &width, &height);

        swapchainExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };

        swapchainExtent.width = std::clamp(swapchainExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        swapchainExtent.height = std::clamp(swapchainExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = { graphicsQueueFamily, presentQueueFamily };

    if (graphicsQueueFamily != presentQueueFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swapchain");
    }

    swapchainImageFormat = surfaceFormat.format;

    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
    swapchainImageLayouts.assign(imageCount, VK_IMAGE_LAYOUT_UNDEFINED);

    swapchainImageViews.resize(swapchainImages.size());
    for (size_t ii = 0; ii < swapchainImages.size(); ii++) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages[ii];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainImageFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &swapchainImageViews[ii]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image views");
        }
    }
}

void XRenderContext::CreateDepthResources() {
    depthFormat = FindDepthFormat();

    depthImages.resize(swapchainImages.size());
    for (size_t ii = 0; ii < depthImages.size(); ii++) {
        depthImages[ii] =
            CreateImage(swapchainExtent.width, swapchainExtent.height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
}

VkRenderPass XRenderContext::CreateRenderPass(const VkFormat colorFormat, const bool enableDepth, const VkImageLayout finalLayout) {
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = finalLayout;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = enableDepth ? &depthAttachmentRef : nullptr;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency outgoing = {};
    outgoing.srcSubpass = 0;
    outgoing.dstSubpass = VK_SUBPASS_EXTERNAL;
    outgoing.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    outgoing.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    if (finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        outgoing.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        outgoing.dstAccessMask = 0;
    } else {
        outgoing.dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                VK_PIPELINE_STAGE_TRANSFER_BIT;
        outgoing.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    }

    const std::array<VkSubpassDependency, 2> dependencies = { dependency, outgoing };

    const std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
    const uint32_t attachmentCount = enableDepth ? 2u : 1u;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = attachmentCount;
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    VkRenderPass created = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &created) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Render pass");
    }

    renderPassAttachmentCounts[created] = attachmentCount;

    return created;
}

void XRenderContext::DestroyRenderPass(VkRenderPass& targetRenderPass) {
    if (targetRenderPass) {
        renderPassAttachmentCounts.erase(targetRenderPass);
        vkDestroyRenderPass(device, targetRenderPass, nullptr);
        targetRenderPass = VK_NULL_HANDLE;
    }
}

void XRenderContext::CreateSwapchainRenderPass() {
    renderPass = CreateRenderPass(swapchainImageFormat, true, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

void XRenderContext::CreateFramebuffers() {
    swapchainFramebuffers.resize(swapchainImageViews.size());

    for (size_t ii = 0; ii < swapchainImageViews.size(); ii++) {
        const std::array<VkImageView, 2> attachments = { swapchainImageViews[ii], depthImages[ii].view };

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[ii]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer");
        }
    }
}

void XRenderContext::CreateCommandPools() {
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool");
    }
}

void XRenderContext::CreateCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers");
    }
}

void XRenderContext::CreateSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(swapchainImages.size());
    imagesInFlight.assign(swapchainImages.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t ii = 0; ii < MAX_FRAMES_IN_FLIGHT; ii++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[ii]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[ii]) != VK_SUCCESS) {

            throw std::runtime_error("failed to create sync objects");
        }
    }

    for (size_t ii = 0; ii < renderFinishedSemaphores.size(); ii++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[ii]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create sync objects");
        }
    }
}

void XRenderContext::CreateDescriptorPool() {
    std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8192 },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8192 },
                                                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192 },
                                                    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192 } };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 8192;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool");
    }
}

XBuffer XRenderContext::CreateBuffer(const VkDeviceSize size, const VkBufferUsageFlags usage, const VmaMemoryUsage memoryUsage) {
    XBuffer buffer;
    buffer.size = size;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;

    if (memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU || memoryUsage == VMA_MEMORY_USAGE_CPU_ONLY) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocationInfo allocationInfo;
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer.buffer, &buffer.allocation, &allocationInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer");
    }

    buffer.mappedData = allocationInfo.pMappedData;

    if (usage != VK_BUFFER_USAGE_TRANSFER_SRC_BIT && memoryUsage != VMA_MEMORY_USAGE_CPU_ONLY) {
        std::cout << "XRenderContext::CreateBuffer: create GPU buffer with size: " << size << " bytes" << std::endl;
    }

    return buffer;
}

static bool UsageSupportsImageView(const VkImageUsageFlags usage) {
    const VkImageUsageFlags viewCapable = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
                                          VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    return (usage & viewCapable) != 0;
}

XImage XRenderContext::CreateImage(const uint32_t width, const uint32_t height, const VkFormat format, const VkImageUsageFlags usage,
                                  const VkImageAspectFlags aspect, const uint32_t mipLevels) {
    XImage image;
    image.width = width;
    image.height = height;
    image.format = format;
    image.mipLevels = mipLevels < 1u ? 1u : mipLevels;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = image.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &image.image, &image.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image");
    }

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = image.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (UsageSupportsImageView(usage)) {
        if (vkCreateImageView(device, &viewInfo, nullptr, &image.view) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image view");
        }
    }

    image.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    return image;
}

XImage XRenderContext::CreateImageArray(const uint32_t width, const uint32_t height, const uint32_t layers, const VkFormat format, const VkImageUsageFlags usage,
                                       const VkImageAspectFlags aspect) {
    XImage image;
    image.width = width;
    image.height = height;
    image.format = format;
    image.layers = layers;
    image.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layers;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &image.image, &image.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image array");
    }

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layers;

    if (UsageSupportsImageView(usage)) {
        if (vkCreateImageView(device, &viewInfo, nullptr, &image.view) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image array view");
        }
    }

    image.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    return image;
}

VkSampler XRenderContext::CreateSampler(const VkFilter minFilter, const VkFilter magFilter, const VkSamplerAddressMode addressMode, const float maxAnisotropy) {
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = magFilter;
    samplerInfo.minFilter = minFilter;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    const bool useAnisotropy = samplerAnisotropySupported && maxAnisotropy > 1.0f;
    samplerInfo.anisotropyEnable = useAnisotropy ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = useAnisotropy ? std::min(maxAnisotropy, maxSamplerAnisotropy) : 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

    VkSampler sampler;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create sampler");
    }

    return sampler;
}

void XRenderContext::DestroySampler(VkSampler& sampler) {
    if (sampler) {
        vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
}

uint32_t XRenderContext::MaxMipLevels(const uint32_t width, const uint32_t height) const {
    uint32_t levels = 1;
    uint32_t size = std::max(width, height);
    while (size > 1) {
        size /= 2;
        levels++;
    }
    return levels;
}

void XRenderContext::GenerateMipmaps(XImage& image) {
    if (image.mipLevels <= 1) {
        return;
    }

    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, image.format, &formatProperties);
    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("image format does not support linear blitting for mipmap generation");
    }

    VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = image.layers;
    barrier.subresourceRange.levelCount = 1;

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.oldLayout = image.layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    int32_t mipWidth = static_cast<int32_t>(image.width);
    int32_t mipHeight = static_cast<int32_t>(image.height);

    for (uint32_t level = 1; level < image.mipLevels; level++) {
        barrier.subresourceRange.baseMipLevel = level;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        const int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        const int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        VkImageBlit blit = {};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = level - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = image.layers;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = level;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = image.layers;

        vkCmdBlitImage(cmdBuf, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image.mipLevels;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (!frameStarted) {
        EndSingleTimeCommands(cmdBuf);
    }

    image.layout = VK_IMAGE_LAYOUT_GENERAL;
}

void XRenderContext::DestroyBuffer(XBuffer& buffer) {
    if (buffer.buffer) {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
        buffer.buffer = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
        buffer.mappedData = nullptr;
    }
}

void XRenderContext::ReleaseDeferredFrameBuffers(const uint32_t frameIndex) {
    if (frameIndex >= deferredFrameBuffers.size()) {
        return;
    }

    for (XBuffer& buffer : deferredFrameBuffers[frameIndex]) {
        DestroyBuffer(buffer);
    }
    deferredFrameBuffers[frameIndex].clear();
}

void XRenderContext::DestroyImage(XImage& image) {
    if (image.sampler) {
        vkDestroySampler(device, image.sampler, nullptr);
        image.sampler = VK_NULL_HANDLE;
    }
    if (image.view) {
        vkDestroyImageView(device, image.view, nullptr);
        image.view = VK_NULL_HANDLE;
    }
    if (image.image) {
        vmaDestroyImage(allocator, image.image, image.allocation);
        image.image = VK_NULL_HANDLE;
        image.allocation = VK_NULL_HANDLE;
    }
}

void XRenderContext::UploadBuffer(XBuffer& buffer, const void* data, const VkDeviceSize size, const VkDeviceSize offset) {
    if (buffer.mappedData) {
        memcpy(static_cast<char*>(buffer.mappedData) + offset, data, size);
    } else {
        XBuffer stagingBuffer = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        memcpy(stagingBuffer.mappedData, data, size);

        VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

        VkBufferCopy copyRegion = {};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = offset;
        copyRegion.size = size;
        vkCmdCopyBuffer(cmdBuf, stagingBuffer.buffer, buffer.buffer, 1, &copyRegion);

        if (frameStarted) {
            deferredFrameBuffers[currentFrame].push_back(stagingBuffer);
        } else {
            EndSingleTimeCommands(cmdBuf);
            DestroyBuffer(stagingBuffer);
        }
    }
}

void XRenderContext::DownloadBuffer(XBuffer& buffer, void* data, const VkDeviceSize size, const VkDeviceSize offset) {
    if (buffer.mappedData) {
        memcpy(data, static_cast<char*>(buffer.mappedData) + offset, size);
    } else {
        XBuffer stagingBuffer = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

        VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

        VkBufferCopy copyRegion = {};
        copyRegion.srcOffset = offset;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(cmdBuf, buffer.buffer, stagingBuffer.buffer, 1, &copyRegion);

        if (!frameStarted) {
            EndSingleTimeCommands(cmdBuf);
        }

        memcpy(data, stagingBuffer.mappedData, size);
        DestroyBuffer(stagingBuffer);
    }
}

void XRenderContext::UploadImage(XImage& image, const void* data, const VkDeviceSize dataSize) {
    XBuffer stagingBuffer = CreateBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    memcpy(stagingBuffer.mappedData, data, dataSize);

    VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

    TransitionImageLayout(image.image, image.format, image.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmdBuf);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { image.width, image.height, 1 };

    vkCmdCopyBufferToImage(cmdBuf, stagingBuffer.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    TransitionImageLayout(image.image, image.format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, cmdBuf);

    image.layout = VK_IMAGE_LAYOUT_GENERAL;
    if (frameStarted) {
        deferredFrameBuffers[currentFrame].push_back(stagingBuffer);
    } else {
        EndSingleTimeCommands(cmdBuf);
        DestroyBuffer(stagingBuffer);
    }
}

void XRenderContext::DownloadImage(XImage& image, void* data, const VkDeviceSize dataSize) {
    XBuffer stagingBuffer = CreateBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

    TransitionImageLayout(image.image, image.format, image.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmdBuf);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { image.width, image.height, 1 };

    vkCmdCopyImageToBuffer(cmdBuf, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer.buffer, 1, &region);

    TransitionImageLayout(image.image, image.format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, cmdBuf);
    image.layout = VK_IMAGE_LAYOUT_GENERAL;

    if (!frameStarted) {
        EndSingleTimeCommands(cmdBuf);
    }

    memcpy(data, stagingBuffer.mappedData, dataSize);
    DestroyBuffer(stagingBuffer);
}

void XRenderContext::FillBuffer(XBuffer& buffer, const VkDeviceSize offset, const VkDeviceSize size, const uint32_t data) {
    VkDeviceSize fillSize = size;
    if (size == VK_WHOLE_SIZE) {
        fillSize = buffer.size - offset;
    }

    VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

    vkCmdFillBuffer(cmdBuf, buffer.buffer, offset, fillSize, data);

    if (!frameStarted) {
        EndSingleTimeCommands(cmdBuf);
    }
}

void* XRenderContext::MapBuffer(XBuffer& buffer) {
    if (buffer.mappedData) {
        return buffer.mappedData;
    } else {
        void* data;
        vmaMapMemory(allocator, buffer.allocation, &data);
        return data;
    }
}

void XRenderContext::UnmapBuffer(XBuffer& buffer) {
    if (!buffer.mappedData) {
        vmaUnmapMemory(allocator, buffer.allocation);
    }
}

void XRenderContext::CopyBuffer(XBuffer& from, XBuffer& to, const uint32_t size) {
    VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;

    vkCmdCopyBuffer(cmdBuf, from.buffer, to.buffer, 1, &copyRegion);

    if (!frameStarted) {
        EndSingleTimeCommands(cmdBuf);
    }
}

void XRenderContext::CopyBufferToImage(XBuffer& stagingBuffer, XImage& targetImage, const uint32_t width, const uint32_t height) {
    VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

    TransitionImageLayout(targetImage.image, targetImage.format, targetImage.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmdBuf);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(cmdBuf, stagingBuffer.buffer, targetImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    TransitionImageLayout(targetImage.image, targetImage.format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, cmdBuf);
    targetImage.layout = VK_IMAGE_LAYOUT_GENERAL;

    if (!frameStarted) {
        EndSingleTimeCommands(cmdBuf);
    }
}

void XRenderContext::CopyImageToImage(XImage& srcImage, XImage& dstImage) {
    VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

    TransitionImageLayout(srcImage.image, srcImage.format, srcImage.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmdBuf);

    TransitionImageLayout(dstImage.image, dstImage.format, dstImage.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmdBuf);

    VkImageCopy region = {};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = { 0, 0, 0 };
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel = 0;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = { 0, 0, 0 };
    region.extent = { srcImage.width, srcImage.height, 1 };

    vkCmdCopyImage(cmdBuf, srcImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    TransitionImageLayout(srcImage.image, srcImage.format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, cmdBuf);

    TransitionImageLayout(dstImage.image, dstImage.format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, cmdBuf);
    srcImage.layout = VK_IMAGE_LAYOUT_GENERAL;
    dstImage.layout = VK_IMAGE_LAYOUT_GENERAL;

    if (!frameStarted) {
        EndSingleTimeCommands(cmdBuf);
    }
}

void XRenderContext::CopyImageToImageArrayLayer(XImage& srcImage, XImage& dstArrayImage, const uint32_t layerIndex) {
    VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

    VkImageMemoryBarrier srcBarrier = {};
    srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    srcBarrier.oldLayout = srcImage.layout;
    srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.image = srcImage.image;
    srcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    srcBarrier.subresourceRange.baseMipLevel = 0;
    srcBarrier.subresourceRange.levelCount = 1;
    srcBarrier.subresourceRange.baseArrayLayer = 0;
    srcBarrier.subresourceRange.layerCount = 1;
    srcBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &srcBarrier);

    if (dstArrayImage.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        VkImageMemoryBarrier initBarrier = {};
        initBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        initBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        initBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        initBarrier.image = dstArrayImage.image;
        initBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        initBarrier.subresourceRange.baseMipLevel = 0;
        initBarrier.subresourceRange.levelCount = 1;
        initBarrier.subresourceRange.baseArrayLayer = 0;
        initBarrier.subresourceRange.layerCount = dstArrayImage.layers;
        initBarrier.srcAccessMask = 0;
        initBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &initBarrier);
        dstArrayImage.layout = VK_IMAGE_LAYOUT_GENERAL;
    }

    VkImageMemoryBarrier dstBarrier = {};
    dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dstBarrier.oldLayout = dstArrayImage.layout;
    dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.image = dstArrayImage.image;
    dstBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    dstBarrier.subresourceRange.baseMipLevel = 0;
    dstBarrier.subresourceRange.levelCount = 1;
    dstBarrier.subresourceRange.baseArrayLayer = layerIndex;
    dstBarrier.subresourceRange.layerCount = 1;
    dstBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

    VkImageCopy copyRegion = {};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.srcOffset = { 0, 0, 0 };
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = layerIndex;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.dstOffset = { 0, 0, 0 };
    copyRegion.extent = { srcImage.width, srcImage.height, 1 };

    vkCmdCopyImage(cmdBuf, srcImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstArrayImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    srcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    srcBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    srcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    dstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dstBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dstBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &srcBarrier);
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

    srcImage.layout = VK_IMAGE_LAYOUT_GENERAL;
    dstArrayImage.layout = VK_IMAGE_LAYOUT_GENERAL;

    if (!frameStarted) {
        EndSingleTimeCommands(cmdBuf);
    }
}

void XRenderContext::CopyImageArrayLayerToImage(XImage& srcArrayImage, const uint32_t layerIndex, XImage& dstImage) {
    VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

    VkImageMemoryBarrier srcBarrier = {};
    srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    srcBarrier.oldLayout = srcArrayImage.layout;
    srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.image = srcArrayImage.image;
    srcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    srcBarrier.subresourceRange.baseMipLevel = 0;
    srcBarrier.subresourceRange.levelCount = 1;
    srcBarrier.subresourceRange.baseArrayLayer = layerIndex;
    srcBarrier.subresourceRange.layerCount = 1;
    srcBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &srcBarrier);

    VkImageMemoryBarrier dstBarrier = {};
    dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dstBarrier.oldLayout = dstImage.layout;
    dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.image = dstImage.image;
    dstBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    dstBarrier.subresourceRange.baseMipLevel = 0;
    dstBarrier.subresourceRange.levelCount = 1;
    dstBarrier.subresourceRange.baseArrayLayer = 0;
    dstBarrier.subresourceRange.layerCount = 1;
    dstBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

    VkImageCopy copyRegion = {};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = layerIndex;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.srcOffset = { 0, 0, 0 };
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.dstOffset = { 0, 0, 0 };
    copyRegion.extent = { dstImage.width, dstImage.height, 1 };

    vkCmdCopyImage(cmdBuf, srcArrayImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    srcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    srcBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    srcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    dstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dstBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dstBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &srcBarrier);
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

    srcArrayImage.layout = VK_IMAGE_LAYOUT_GENERAL;
    dstImage.layout = VK_IMAGE_LAYOUT_GENERAL;

    if (!frameStarted) {
        EndSingleTimeCommands(cmdBuf);
    }
}

void XRenderContext::CopyBufferToImageArray(XBuffer& stagingBuffer, XImage& targetArrayImage, const VkDeviceSize layerStride) {
    VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

    VkImageMemoryBarrier dstBarrier = {};
    dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dstBarrier.oldLayout = targetArrayImage.layout;
    dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.image = targetArrayImage.image;
    dstBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    dstBarrier.subresourceRange.baseMipLevel = 0;
    dstBarrier.subresourceRange.levelCount = 1;
    dstBarrier.subresourceRange.baseArrayLayer = 0;
    dstBarrier.subresourceRange.layerCount = targetArrayImage.layers;
    dstBarrier.srcAccessMask = targetArrayImage.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

    std::vector<VkBufferImageCopy> regions(targetArrayImage.layers);
    for (uint32_t layer = 0; layer < targetArrayImage.layers; layer++) {
        VkBufferImageCopy region = {};
        region.bufferOffset = layerStride * layer;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = layer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { targetArrayImage.width, targetArrayImage.height, 1 };
        regions[layer] = region;
    }

    vkCmdCopyBufferToImage(cmdBuf, stagingBuffer.buffer, targetArrayImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()),
                           regions.data());

    dstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dstBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dstBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

    targetArrayImage.layout = VK_IMAGE_LAYOUT_GENERAL;

    if (!frameStarted) {
        EndSingleTimeCommands(cmdBuf);
    }
}

VkShaderModule XRenderContext::LoadShaderModule(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open shader file: " + path);
    }

    size_t const fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module");
    }

    return shaderModule;
}

XGraphicsPipeline XRenderContext::CreateGraphicsPipeline(const std::string& vertShaderPath, const std::string& fragShaderPath,
                                                        const std::vector<XDescriptorBinding>& bindings,
                                                        const std::vector<XPushConstantRange>& pushConstantRanges, const XGraphicsPipelineOptions& options) {

    XGraphicsPipeline pipeline;

    VkShaderModule vertShaderModule = LoadShaderModule(vertShaderPath);
    VkShaderModule fragShaderModule = LoadShaderModule(fragShaderPath);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    for (const auto& binding : bindings) {
        VkDescriptorSetLayoutBinding layoutBinding = {};
        layoutBinding.binding = binding.binding;
        layoutBinding.descriptorType = binding.type;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = binding.stages;
        layoutBindings.push_back(layoutBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &pipeline.descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout");
    }

    std::vector<VkPushConstantRange> vkPushConstantRanges;
    for (const auto& pc : pushConstantRanges) {
        VkPushConstantRange range = {};
        range.stageFlags = pc.stages;
        range.offset = pc.offset;
        range.size = pc.size;
        vkPushConstantRanges.push_back(range);
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &pipeline.descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(vkPushConstantRanges.size());
    pipelineLayoutInfo.pPushConstantRanges = vkPushConstantRanges.empty() ? nullptr : vkPushConstantRanges.data();

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipeline.layout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout");
    }

    std::vector<VkVertexInputBindingDescription> vkVertexBindings;
    for (const auto& vb : options.vertexBindings) {
        VkVertexInputBindingDescription description = {};
        description.binding = vb.binding;
        description.stride = vb.stride;
        description.inputRate = vb.inputRate;
        vkVertexBindings.push_back(description);
    }

    std::vector<VkVertexInputAttributeDescription> vkVertexAttributes;
    for (const auto& va : options.vertexAttributes) {
        VkVertexInputAttributeDescription description = {};
        description.location = va.location;
        description.binding = va.binding;
        description.format = va.format;
        description.offset = va.offset;
        vkVertexAttributes.push_back(description);
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vkVertexBindings.size());
    vertexInputInfo.pVertexBindingDescriptions = vkVertexBindings.empty() ? nullptr : vkVertexBindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vkVertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vkVertexAttributes.empty() ? nullptr : vkVertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = options.topology;
    inputAssembly.primitiveRestartEnable = options.primitiveRestart ? VK_TRUE : VK_FALSE;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = options.cullMode;
    rasterizer.frontFace = options.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = options.colorWriteMask;
    colorBlendAttachment.blendEnable = options.blendEnable ? VK_TRUE : VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = options.srcColorBlendFactor;
    colorBlendAttachment.dstColorBlendFactor = options.dstColorBlendFactor;
    colorBlendAttachment.colorBlendOp = options.colorBlendOp;
    colorBlendAttachment.srcAlphaBlendFactor = options.srcAlphaBlendFactor;
    colorBlendAttachment.dstAlphaBlendFactor = options.dstAlphaBlendFactor;
    colorBlendAttachment.alphaBlendOp = options.alphaBlendOp;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = options.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = (options.depthTest && options.depthWrite) ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = options.depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipeline.layout;
    pipelineInfo.renderPass = options.renderPass ? options.renderPass : renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline");
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    pipeline.descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, pipeline.descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, pipeline.descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets");
    }

    return pipeline;
}

XComputePipeline XRenderContext::CreateComputePipeline(const std::string& compShaderPath, const std::vector<XDescriptorBinding>& bindings,
                                                      const std::vector<XPushConstantRange>& pushConstantRanges) {

    XComputePipeline pipeline;

    VkShaderModule compShaderModule = LoadShaderModule(compShaderPath);

    VkPipelineShaderStageCreateInfo compShaderStageInfo = {};
    compShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compShaderStageInfo.module = compShaderModule;
    compShaderStageInfo.pName = "main";

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    for (const auto& binding : bindings) {
        VkDescriptorSetLayoutBinding layoutBinding = {};
        layoutBinding.binding = binding.binding;
        layoutBinding.descriptorType = binding.type;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = binding.stages;
        layoutBindings.push_back(layoutBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &pipeline.descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout");
    }

    std::vector<VkPushConstantRange> vkPushConstantRanges;
    for (const auto& pc : pushConstantRanges) {
        VkPushConstantRange range = {};
        range.stageFlags = pc.stages;
        range.offset = pc.offset;
        range.size = pc.size;
        vkPushConstantRanges.push_back(range);
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &pipeline.descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(vkPushConstantRanges.size());
    pipelineLayoutInfo.pPushConstantRanges = vkPushConstantRanges.empty() ? nullptr : vkPushConstantRanges.data();

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipeline.layout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout");
    }

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = compShaderStageInfo;
    pipelineInfo.layout = pipeline.layout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline");
    }

    vkDestroyShaderModule(device, compShaderModule, nullptr);

    pipeline.descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, pipeline.descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, pipeline.descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets");
    }

    return pipeline;
}

void XRenderContext::DestroyGraphicsPipeline(XGraphicsPipeline& pipeline) {
    if (!pipeline.descriptorSets.empty()) {
        vkFreeDescriptorSets(device, descriptorPool, static_cast<uint32_t>(pipeline.descriptorSets.size()), pipeline.descriptorSets.data());
    }
    if (pipeline.pipeline) {
        vkDestroyPipeline(device, pipeline.pipeline, nullptr);
    }
    if (pipeline.layout) {
        vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
    }
    if (pipeline.descriptorSetLayout) {
        vkDestroyDescriptorSetLayout(device, pipeline.descriptorSetLayout, nullptr);
    }

    pipeline.descriptorSets.clear();

    pipeline.pipeline = VK_NULL_HANDLE;
}

void XRenderContext::DestroyComputePipeline(XComputePipeline& pipeline) {
    if (!pipeline.descriptorSets.empty()) {
        vkFreeDescriptorSets(device, descriptorPool, static_cast<uint32_t>(pipeline.descriptorSets.size()), pipeline.descriptorSets.data());
    }
    if (pipeline.pipeline) {
        vkDestroyPipeline(device, pipeline.pipeline, nullptr);
    }
    if (pipeline.layout) {
        vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
    }
    if (pipeline.descriptorSetLayout) {
        vkDestroyDescriptorSetLayout(device, pipeline.descriptorSetLayout, nullptr);
    }

    pipeline.descriptorSets.clear();
}

void XRenderContext::UpdateDescriptors(XGraphicsPipeline& pipeline, const std::vector<XDescriptorBinding>& bindings) {
    for (uint32_t ii = 0; ii < pipeline.descriptorSets.size(); ii++) {
        std::vector<VkWriteDescriptorSet> descriptorWrites;

        for (const auto& binding : bindings) {
            VkWriteDescriptorSet descriptorWrite = {};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = pipeline.descriptorSets[ii];
            descriptorWrite.dstBinding = binding.binding;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = binding.type;
            descriptorWrite.descriptorCount = 1;

            if (binding.buffer) {
                VkDescriptorBufferInfo* bufferInfo = new VkDescriptorBufferInfo();
                bufferInfo->buffer = binding.buffer->buffer;
                bufferInfo->offset = 0;
                bufferInfo->range = binding.buffer->size;
                descriptorWrite.pBufferInfo = bufferInfo;
            } else if (binding.image) {
                VkDescriptorImageInfo* imageInfo = new VkDescriptorImageInfo();
                imageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                imageInfo->imageView = binding.image->view;
                imageInfo->sampler = binding.image->sampler ? binding.image->sampler : VK_NULL_HANDLE;
                descriptorWrite.pImageInfo = imageInfo;
            }

            descriptorWrites.push_back(descriptorWrite);
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

        for (auto& write : descriptorWrites) {
            if (write.pBufferInfo) {
                delete write.pBufferInfo;
            }
            if (write.pImageInfo) {
                delete write.pImageInfo;
            }
        }
    }
}

void XRenderContext::UpdateDescriptors(XComputePipeline& pipeline, const std::vector<XDescriptorBinding>& bindings) {
    for (uint32_t ii = 0; ii < pipeline.descriptorSets.size(); ii++) {
        std::vector<VkWriteDescriptorSet> descriptorWrites;

        for (const auto& binding : bindings) {
            VkWriteDescriptorSet descriptorWrite = {};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = pipeline.descriptorSets[ii];
            descriptorWrite.dstBinding = binding.binding;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = binding.type;
            descriptorWrite.descriptorCount = 1;

            if (binding.buffer) {
                VkDescriptorBufferInfo* bufferInfo = new VkDescriptorBufferInfo();
                bufferInfo->buffer = binding.buffer->buffer;
                bufferInfo->offset = 0;
                bufferInfo->range = binding.buffer->size;
                descriptorWrite.pBufferInfo = bufferInfo;
            } else if (binding.image) {
                VkDescriptorImageInfo* imageInfo = new VkDescriptorImageInfo();
                imageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                imageInfo->imageView = binding.image->view;
                imageInfo->sampler = binding.image->sampler ? binding.image->sampler : VK_NULL_HANDLE;
                descriptorWrite.pImageInfo = imageInfo;
            }

            descriptorWrites.push_back(descriptorWrite);
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

        for (auto& write : descriptorWrites) {
            if (write.pBufferInfo) {
                delete write.pBufferInfo;
            }
            if (write.pImageInfo) {
                delete write.pImageInfo;
            }
        }
    }
}

void XRenderContext::PushConstantsRaw(XGraphicsPipeline& pipeline, const VkShaderStageFlags stages, const void* data, const uint32_t size,
                                      const uint32_t offset) {
    vkCmdPushConstants(FrameCommandBuffer(), pipeline.layout, stages, offset, size, data);
}

void XRenderContext::PushConstantsRaw(XComputePipeline& pipeline, const VkShaderStageFlags stages, const void* data, const uint32_t size,
                                      const uint32_t offset) {
    vkCmdPushConstants(FrameCommandBuffer(), pipeline.layout, stages, offset, size, data);
}

VkCommandBuffer XRenderContext::FrameCommandBuffer() const {
    if (!frameStarted) {
        throw std::runtime_error("command recording requires an active frame; call BeginFrame first");
    }
    return commandBuffers[currentFrame];
}

void XRenderContext::CleanupSwapchain() {
    for (const auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    swapchainFramebuffers.clear();

    for (auto& image : depthImages) {
        DestroyImage(image);
    }
    depthImages.clear();

    for (const auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    swapchainImageViews.clear();
    swapchainImages.clear();
    swapchainImageLayouts.clear();

    for (const auto semaphore : renderFinishedSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    renderFinishedSemaphores.clear();
    imagesInFlight.clear();

    if (swapchain) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

void XRenderContext::RecreateSwapchain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        if (glfwWindowShouldClose(window)) {
            return;
        }
        glfwWaitEvents();
        glfwGetFramebufferSize(window, &width, &height);
    }

    vkDeviceWaitIdle(device);

    CleanupSwapchain();

    CreateSwapchain();
    CreateDepthResources();
    CreateFramebuffers();

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    renderFinishedSemaphores.resize(swapchainImages.size());
    for (size_t ii = 0; ii < renderFinishedSemaphores.size(); ii++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[ii]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create sync objects");
        }
    }
    imagesInFlight.assign(swapchainImages.size(), VK_NULL_HANDLE);

    currentImageIndex = 0;
}

void XRenderContext::BeginFrame() {
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    ReleaseDeferredFrameBuffers(currentFrame);

    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &currentImageIndex);
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swapchain image");
    }

    if (imagesInFlight[currentImageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(device, 1, &imagesInFlight[currentImageIndex], VK_TRUE, UINT64_MAX);
    }
    imagesInFlight[currentImageIndex] = inFlightFences[currentFrame];

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    vkResetCommandBuffer(commandBuffers[currentFrame], 0);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer");
    }

    frameStarted = true;
    inRenderPass = false;
    swapchainImageUsedThisFrame = false;
}

void XRenderContext::EndFrame() {
    if (!frameStarted) {
        return;
    }

    if (inRenderPass) {
        vkCmdEndRenderPass(commandBuffers[currentFrame]);
        inRenderPass = false;
        swapchainImageLayouts[currentImageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    if (!swapchainImageUsedThisFrame && swapchainImageLayouts[currentImageIndex] != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = swapchainImageLayouts[currentImageIndex];
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapchainImages[currentImageIndex];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;

        vkCmdPipelineBarrier(commandBuffers[currentFrame], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &barrier);

        swapchainImageLayouts[currentImageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    if (vkEndCommandBuffer(commandBuffers[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer");
    }

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentImageIndex] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit Draw command buffer");
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = { swapchain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &currentImageIndex;

    const VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    frameStarted = false;
    swapchainImageUsedThisFrame = false;
    currentGraphicsPipeline = nullptr;
    currentComputePipeline = nullptr;

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        RecreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("failed to present swapchain image");
    }
}

void XRenderContext::BeginRenderPass(const VkFramebuffer framebuffer, const uint32_t width, const uint32_t height,
                                     const VkRenderPass targetRenderPass) {
    if (inRenderPass) {
        throw std::runtime_error("already in a Render pass");
    }

    const VkRenderPass pass = targetRenderPass ? targetRenderPass : renderPass;

    const auto found = renderPassAttachmentCounts.find(pass);
    const uint32_t attachmentCount = (found != renderPassAttachmentCounts.end()) ? found->second : 2u;

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = pass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = { width, height };

    std::array<VkClearValue, 2> clearValues = {};
    clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };

    renderPassInfo.clearValueCount = attachmentCount;
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(FrameCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    inRenderPass = true;

    SetViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    SetScissor(0, 0, width, height);

    if (!swapchainFramebuffers.empty() && framebuffer == swapchainFramebuffers[currentImageIndex]) {
        swapchainImageUsedThisFrame = true;
    }
}

void XRenderContext::EndRenderPass() {
    if (!inRenderPass) {
        throw std::runtime_error("not in a Render pass");
    }

    vkCmdEndRenderPass(FrameCommandBuffer());
    inRenderPass = false;
}

void XRenderContext::ComputeToGraphicsBarrier() {
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;

    vkCmdPipelineBarrier(FrameCommandBuffer(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void XRenderContext::GraphicsToComputeBarrier() {
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(FrameCommandBuffer(), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void XRenderContext::ComputeToComputeBarrier() {
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(FrameCommandBuffer(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr,
                         0, nullptr);
}

void XRenderContext::ComputeToTransferBarrier() {
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(FrameCommandBuffer(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0,
                         nullptr);
}

void XRenderContext::TransferToComputeBarrier() {
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(FrameCommandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void XRenderContext::FullPipelineBarrier() {
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    vkCmdPipelineBarrier(FrameCommandBuffer(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0,
                         nullptr);
}

VkImageAspectFlags XRenderContext::AspectMaskForFormat(const VkFormat format) const {
    if (format == VK_FORMAT_D32_SFLOAT) {
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    if (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_X8_D24_UNORM_PACK32) {
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

void XRenderContext::ImageBarrier(XImage& image, const VkImageLayout oldLayout, const VkImageLayout newLayout, const VkAccessFlags srcAccess,
                                  const VkAccessFlags dstAccess, const VkPipelineStageFlags srcStage, const VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.image;
    barrier.subresourceRange.aspectMask = AspectMaskForFormat(image.format);
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image.mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = image.layers;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    VkCommandBuffer cmdBuf = frameStarted ? commandBuffers[currentFrame] : BeginSingleTimeCommands();

    vkCmdPipelineBarrier(cmdBuf, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (!frameStarted) {
        EndSingleTimeCommands(cmdBuf);
    }

    image.layout = newLayout;
}

VkFramebuffer XRenderContext::CreateFramebuffer(const std::vector<VkImageView>& attachments, const uint32_t width, const uint32_t height,
                                                const VkRenderPass targetRenderPass) {
    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = targetRenderPass ? targetRenderPass : renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;

    VkFramebuffer framebuffer;
    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create framebuffer");
    }

    return framebuffer;
}

void XRenderContext::DestroyFramebuffer(const VkFramebuffer framebuffer) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
}

void XRenderContext::BindGraphicsPipeline(XGraphicsPipeline& pipeline) {
    vkCmdBindPipeline(FrameCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    vkCmdBindDescriptorSets(FrameCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, &pipeline.descriptorSets[currentFrame], 0,
                            nullptr);
    currentGraphicsPipeline = &pipeline;
}

void XRenderContext::BindComputePipeline(XComputePipeline& pipeline) {
    vkCmdBindPipeline(FrameCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
    vkCmdBindDescriptorSets(FrameCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout, 0, 1, &pipeline.descriptorSets[currentFrame], 0,
                            nullptr);
    currentComputePipeline = &pipeline;
}

void XRenderContext::BindVertexBuffer(XBuffer& buffer, const uint32_t binding, const VkDeviceSize offset) {
    vkCmdBindVertexBuffers(FrameCommandBuffer(), binding, 1, &buffer.buffer, &offset);
}

void XRenderContext::BindVertexBuffers(const std::vector<XBuffer*>& buffers, const uint32_t firstBinding, const std::vector<VkDeviceSize>& offsets) {
    if (buffers.empty()) {
        return;
    }

    std::vector<VkBuffer> handles;
    handles.reserve(buffers.size());
    for (const auto* buffer : buffers) {
        handles.push_back(buffer->buffer);
    }

    std::vector<VkDeviceSize> bindOffsets = offsets;
    bindOffsets.resize(buffers.size(), 0);

    vkCmdBindVertexBuffers(FrameCommandBuffer(), firstBinding, static_cast<uint32_t>(handles.size()), handles.data(), bindOffsets.data());
}

void XRenderContext::BindIndexBuffer(XBuffer& buffer, const VkIndexType indexType, const VkDeviceSize offset) {
    vkCmdBindIndexBuffer(FrameCommandBuffer(), buffer.buffer, offset, indexType);
}

void XRenderContext::SetViewport(const float x, const float y, const float width, const float height, const float minDepth, const float maxDepth) {
    VkViewport viewport = {};
    viewport.x = x;
    viewport.y = y;
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = minDepth;
    viewport.maxDepth = maxDepth;

    vkCmdSetViewport(FrameCommandBuffer(), 0, 1, &viewport);
}

void XRenderContext::SetScissor(const int32_t x, const int32_t y, const uint32_t width, const uint32_t height) {
    VkRect2D scissor = {};
    scissor.offset = { x, y };
    scissor.extent = { width, height };

    vkCmdSetScissor(FrameCommandBuffer(), 0, 1, &scissor);
}

void XRenderContext::Draw(const uint32_t vertexCount, const uint32_t instanceCount, const uint32_t firstVertex, const uint32_t firstInstance) {
    vkCmdDraw(FrameCommandBuffer(), vertexCount, instanceCount, firstVertex, firstInstance);
}

void XRenderContext::DrawIndexed(const uint32_t indexCount, const uint32_t instanceCount, const uint32_t firstIndex, const int32_t vertexOffset,
                                 const uint32_t firstInstance) {
    vkCmdDrawIndexed(FrameCommandBuffer(), indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void XRenderContext::DrawIndirect(XBuffer& argsBuffer, const VkDeviceSize offset, const uint32_t drawCount, const uint32_t stride) {
    vkCmdDrawIndirect(FrameCommandBuffer(), argsBuffer.buffer, offset, drawCount, stride);
}

void XRenderContext::DrawIndexedIndirect(XBuffer& argsBuffer, const VkDeviceSize offset, const uint32_t drawCount, const uint32_t stride) {
    vkCmdDrawIndexedIndirect(FrameCommandBuffer(), argsBuffer.buffer, offset, drawCount, stride);
}

void XRenderContext::DrawFullscreenTriangle() {
    vkCmdDraw(FrameCommandBuffer(), 3, 1, 0, 0);
}

void XRenderContext::Dispatch(const uint32_t groupCountX, const uint32_t groupCountY, const uint32_t groupCountZ) {
    vkCmdDispatch(FrameCommandBuffer(), groupCountX, groupCountY, groupCountZ);
}

void XRenderContext::DispatchIndirect(XBuffer& argsBuffer, const uint32_t offset) {
    vkCmdDispatchIndirect(FrameCommandBuffer(), argsBuffer.buffer, offset);
}

void XRenderContext::TransitionImageLayout(const VkImage image, const VkFormat format, const VkImageLayout oldLayout, const VkImageLayout newLayout,
                                           VkCommandBuffer cmdBuf) {
    const bool ownCommandBuffer = (cmdBuf == VK_NULL_HANDLE);
    if (ownCommandBuffer) {
        cmdBuf = BeginSingleTimeCommands();
    }

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT) {
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else {
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }

    vkCmdPipelineBarrier(cmdBuf, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (ownCommandBuffer) {
        EndSingleTimeCommands(cmdBuf);
    }
}

VkCommandBuffer XRenderContext::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void XRenderContext::EndSingleTimeCommands(const VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

bool XRenderContext::IsKeyPressed(const int key) const {
    return glfwGetKey(window, key) == GLFW_PRESS;
}

bool XRenderContext::IsKeyUp(const int key) const {
    return glfwGetKey(window, key) == GLFW_PRESS;
}

bool XRenderContext::IsMouseButtonPressed(const int button) const {
    return glfwGetMouseButton(window, button) == GLFW_PRESS;
}

glm::vec2 XRenderContext::GetMousePos() const {
    double xpos;
    double ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    return glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));
}

bool XRenderContext::ShouldClose() const {
    return glfwWindowShouldClose(window);
}

void XRenderContext::PollEvents() {
    glfwPollEvents();
}
