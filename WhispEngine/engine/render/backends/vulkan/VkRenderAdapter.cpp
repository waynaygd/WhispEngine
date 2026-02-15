#include "VkRenderAdapter.h"

#if defined(ENABLE_VULKAN)

#include "../../../core/Logger.h"
#include "../../../platform/GlfwWindow.h"

#include <GLFW/glfw3.h>

#include <vulkan/vulkan.h>

#include <cstdlib>     
#include <filesystem>
#include <sstream>

#include <vector>
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <array>
#include <optional>
#include <cstdint>

#ifdef _WIN32
#include <Windows.h>
#endif

#include <string>

#ifdef _WIN32
static int RunProcessW(const std::wstring& commandLine)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi{};

    std::wstring cmd = commandLine;

    if (!CreateProcessW(
        nullptr,
        cmd.data(),
        nullptr, nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi))
    {
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return static_cast<int>(exitCode);
}
#endif


static bool ReadFileBinary(const std::string& path, std::vector<uint32_t>& outWords, std::string& outErr)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        outErr = "Cannot open SPIR-V file: " + path;
        return false;
    }

    const std::streamsize size = file.tellg();
    if (size <= 0)
    {
        outErr = "Empty SPIR-V file: " + path;
        return false;
    }

    if (size % 4 != 0)
    {
        outErr = "SPIR-V size is not multiple of 4: " + path;
        return false;
    }

    file.seekg(0, std::ios::beg);
    outWords.resize(static_cast<size_t>(size / 4));
    if (!file.read(reinterpret_cast<char*>(outWords.data()), size))
    {
        outErr = "Failed to read SPIR-V file: " + path;
        return false;
    }

    return true;
}

static bool ResolveExistingPath(const std::string& path, std::string& outResolved)
{
    namespace fs = std::filesystem;

    if (fs::exists(path)) { outResolved = path; return true; }

    const std::string p1 = "../" + path;
    const std::string p2 = "../../" + path;
    const std::string p3 = "../../../" + path;
    const std::string p4 = "../../../../" + path;

    if (fs::exists(p1)) { outResolved = p1; return true; }
    if (fs::exists(p2)) { outResolved = p2; return true; }
    if (fs::exists(p3)) { outResolved = p3; return true; }
    if (fs::exists(p4)) { outResolved = p4; return true; }

    return false;
}


static bool TryReadSpv(const std::string& p, std::vector<uint32_t>& outWords, std::string& outErr)
{
    return ReadFileBinary(p, outWords, outErr);
}

static bool ResolveAndReadSpv(const std::string& path,
    std::vector<uint32_t>& outWords,
    std::string& outUsedPath,
    std::string& outErr)
{
    if (TryReadSpv(path, outWords, outErr)) { outUsedPath = path; return true; }

    const std::string p1 = "../" + path;
    const std::string p2 = "../../" + path;
    const std::string p3 = "../../../" + path;
    const std::string p4 = "../../../../" + path;

    if (TryReadSpv(p1, outWords, outErr)) { outUsedPath = p1; return true; }
    if (TryReadSpv(p2, outWords, outErr)) { outUsedPath = p2; return true; }
    if (TryReadSpv(p3, outWords, outErr)) { outUsedPath = p3; return true; }
    if (TryReadSpv(p4, outWords, outErr)) { outUsedPath = p4; return true; }

    outErr = "Cannot open SPIR-V file: " + path + " (also tried ../ ../../ ../../../ ../../../../)";
    return false;
}


static bool CreateShaderModule(VkDevice device, const std::vector<uint32_t>& words, VkShaderModule& outModule, std::string& outErr)
{
    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = words.size() * sizeof(uint32_t);
    ci.pCode = words.data();

    VkResult r = vkCreateShaderModule(device, &ci, nullptr, &outModule);
    if (r != VK_SUCCESS)
    {
        outErr = "vkCreateShaderModule failed";
        return false;
    }
    return true;
}

static void VK_ThrowIfFailed(VkResult r, const char* msg)
{
    if (r != VK_SUCCESS)
    {
        Logger::Get().Error(msg);
        throw std::runtime_error(msg);
    }
}

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;

    bool Complete() const { return graphics.has_value() && present.has_value(); }
};

static QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice phys, VkSurfaceKHR surface)
{
    QueueFamilyIndices idx;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, props.data());

    for (uint32_t i = 0; i < count; ++i)
    {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            idx.graphics = i;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &presentSupport);
        if (presentSupport)
            idx.present = i;

        if (idx.Complete())
            break;
    }

    return idx;
}

struct Vertex
{
    float pos[2];
    float color[3];
};

static VkVertexInputBindingDescription VertexBinding()
{
    VkVertexInputBindingDescription b{};
    b.binding = 0;
    b.stride = sizeof(Vertex);
    b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return b;
}

static std::array<VkVertexInputAttributeDescription, 2> VertexAttributes()
{
    std::array<VkVertexInputAttributeDescription, 2> a{};

    a[0].location = 0;
    a[0].binding = 0;
    a[0].format = VK_FORMAT_R32G32_SFLOAT;
    a[0].offset = offsetof(Vertex, pos);

    a[1].location = 1;
    a[1].binding = 0;
    a[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    a[1].offset = offsetof(Vertex, color);

    return a;
}

struct SwapchainSupport
{
    VkSurfaceCapabilitiesKHR caps{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

static SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice phys, VkSurfaceKHR surface)
{
    SwapchainSupport s;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &s.caps);

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &count, nullptr);
    s.formats.resize(count);
    if (count) vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &count, s.formats.data());

    count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &count, nullptr);
    s.presentModes.resize(count);
    if (count) vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &count, s.presentModes.data());

    return s;
}

static VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (auto& f : formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    }
    for (auto& f : formats)
    {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    }
    return formats[0];
}


static VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes)
{
    for (auto m : modes)
    {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t w, uint32_t h)
{
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;

    VkExtent2D e{};
    e.width = (std::max)(caps.minImageExtent.width, (std::min)(caps.maxImageExtent.width, w));
    e.height = (std::max)(caps.minImageExtent.height, (std::min)(caps.maxImageExtent.height, h));
    return e;
}

static uint32_t FindMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("Failed to find suitable Vulkan memory type");
}

VkShaderModule VkRenderAdapter::LoadShaderModule(const char* spvPath)
{
    std::vector<uint32_t> words;
    std::string err;
    std::string used;

    if (!ResolveAndReadSpv(spvPath, words, used, err))
    {
        Logger::Get().Error(err);
        return VK_NULL_HANDLE;
    }

    Logger::Get().Info("Vulkan: shader loaded: " + used);

    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = words.size() * sizeof(uint32_t);
    ci.pCode = words.data();

    VkShaderModule m = VK_NULL_HANDLE;
    VkResult r = vkCreateShaderModule(m_Device, &ci, nullptr, &m);
    if (r != VK_SUCCESS)
    {
        Logger::Get().Error(std::string("vkCreateShaderModule failed for: ") + used);
        return VK_NULL_HANDLE;
    }

    return m;
}


bool VkRenderAdapter::CreatePipelineFromModules(VkShaderModule vs, VkShaderModule fs, VkPipeline& outPipeline)
{
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    auto binding = VertexBinding();
    auto attrs = VertexAttributes();

    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ia.primitiveRestartEnable = VK_FALSE;

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = (float)m_SwapExtent.height;
    vp.width = (float)m_SwapExtent.width;
    vp.height = -(float)m_SwapExtent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = m_SwapExtent;

    VkPipelineViewportStateCreateInfo vpState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vpState.viewportCount = 1;
    vpState.pViewports = &vp;
    vpState.scissorCount = 1;
    vpState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.depthBiasEnable = VK_FALSE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAtt.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAtt;

    VkGraphicsPipelineCreateInfo gp{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vpState;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.layout = m_PipelineLayout;
    gp.renderPass = m_RenderPass;
    gp.subpass = 0;

    VkResult r = vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &gp, nullptr, &outPipeline);
    if (r != VK_SUCCESS)
    {
        Logger::Get().Error("vkCreateGraphicsPipelines failed");
        outPipeline = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VkRenderAdapter::RecreateGraphicsPipeline()
{
    VkShaderModule vs = LoadShaderModule(m_VertSpvPath.c_str());
    if (vs == VK_NULL_HANDLE)
        return false;

    VkShaderModule fs = LoadShaderModule(m_FragSpvPath.c_str());
    if (fs == VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(m_Device, vs, nullptr);
        return false;
    }

    VkPipeline newPipeline = VK_NULL_HANDLE;
    bool ok = CreatePipelineFromModules(vs, fs, newPipeline);

    vkDestroyShaderModule(m_Device, fs, nullptr);
    vkDestroyShaderModule(m_Device, vs, nullptr);

    if (!ok || newPipeline == VK_NULL_HANDLE)
        return false;

    if (m_Pipeline)
        vkDestroyPipeline(m_Device, m_Pipeline, nullptr);

    m_Pipeline = newPipeline;
    return true;
}

bool VkRenderAdapter::Initialize(IWindow* window)
{
    try
    {
        auto* glfwWin = static_cast<GlfwWindow*>(window);
        GLFWwindow* w = glfwWin->GetGlfwHandle();
        if (!w) throw std::runtime_error("GlfwWindow handle is null");


        VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.pApplicationName = "WhispEngine";
        app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app.pEngineName = "WhispEngine";
        app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app.apiVersion = VK_API_VERSION_1_2;

        uint32_t extCount = 0;
        const char** glfwExt = glfwGetRequiredInstanceExtensions(&extCount);
        std::vector<const char*> exts(glfwExt, glfwExt + extCount);

        VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        ici.pApplicationInfo = &app;
        ici.enabledExtensionCount = (uint32_t)exts.size();
        ici.ppEnabledExtensionNames = exts.data();

        VK_ThrowIfFailed(vkCreateInstance(&ici, nullptr, &m_Instance), "vkCreateInstance failed");

        VK_ThrowIfFailed((VkResult)glfwCreateWindowSurface(m_Instance, w, nullptr, &m_Surface),
            "glfwCreateWindowSurface failed");

        uint32_t pdCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &pdCount, nullptr);
        if (!pdCount) throw std::runtime_error("No Vulkan physical devices");

        std::vector<VkPhysicalDevice> pds(pdCount);
        vkEnumeratePhysicalDevices(m_Instance, &pdCount, pds.data());

        auto hasSwapchainExt = [](VkPhysicalDevice d) -> bool {
            uint32_t c = 0;
            vkEnumerateDeviceExtensionProperties(d, nullptr, &c, nullptr);
            std::vector<VkExtensionProperties> props(c);
            vkEnumerateDeviceExtensionProperties(d, nullptr, &c, props.data());
            for (auto& p : props)
                if (std::strcmp(p.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                    return true;
            return false;
            };

        for (auto d : pds)
        {
            if (!hasSwapchainExt(d)) continue;
            auto q = FindQueueFamilies(d, m_Surface);
            if (!q.Complete()) continue;

            auto sc = QuerySwapchainSupport(d, m_Surface);
            if (sc.formats.empty() || sc.presentModes.empty()) continue;

            m_Physical = d;
            m_GraphicsFamily = q.graphics.value();
            m_PresentFamily = q.present.value();
            break;
        }

        if (!m_Physical) throw std::runtime_error("No suitable Vulkan device found");

        float prio = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> qcis;

        std::vector<uint32_t> uniqueFamilies;
        uniqueFamilies.push_back(m_GraphicsFamily);
        if (m_PresentFamily != m_GraphicsFamily)
            uniqueFamilies.push_back(m_PresentFamily);

        for (uint32_t fam : uniqueFamilies)
        {
            VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            qci.queueFamilyIndex = fam;
            qci.queueCount = 1;
            qci.pQueuePriorities = &prio;
            qcis.push_back(qci);
        }

        const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        VkPhysicalDeviceFeatures feats{};
        VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        dci.queueCreateInfoCount = (uint32_t)qcis.size();
        dci.pQueueCreateInfos = qcis.data();
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = devExts;
        dci.pEnabledFeatures = &feats;

        VK_ThrowIfFailed(vkCreateDevice(m_Physical, &dci, nullptr, &m_Device), "vkCreateDevice failed");

        vkGetDeviceQueue(m_Device, m_GraphicsFamily, 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, m_PresentFamily, 0, &m_PresentQueue);

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(w, &fbW, &fbH);

        auto sc = QuerySwapchainSupport(m_Physical, m_Surface);
        auto fmt = ChooseSurfaceFormat(sc.formats);
        auto pm = ChoosePresentMode(sc.presentModes);
        auto extent = ChooseExtent(sc.caps, (uint32_t)fbW, (uint32_t)fbH);

        uint32_t imageCount = sc.caps.minImageCount + 1;
        if (sc.caps.maxImageCount > 0 && imageCount > sc.caps.maxImageCount)
            imageCount = sc.caps.maxImageCount;

        VkSwapchainCreateInfoKHR sci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        sci.surface = m_Surface;
        sci.minImageCount = imageCount;
        sci.imageFormat = fmt.format;
        sci.imageColorSpace = fmt.colorSpace;
        sci.imageExtent = extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t qidx[] = { m_GraphicsFamily, m_PresentFamily };
        if (m_GraphicsFamily != m_PresentFamily)
        {
            sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            sci.queueFamilyIndexCount = 2;
            sci.pQueueFamilyIndices = qidx;
        }
        else
        {
            sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        sci.preTransform = sc.caps.currentTransform;
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode = pm;
        sci.clipped = VK_TRUE;

        VK_ThrowIfFailed(vkCreateSwapchainKHR(m_Device, &sci, nullptr, &m_Swapchain),
            "vkCreateSwapchainKHR failed");

        m_SwapFormat = fmt.format;
        m_SwapExtent = extent;

        uint32_t imgCount2 = 0;
        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imgCount2, nullptr);
        m_SwapImages.resize(imgCount2);
        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imgCount2, m_SwapImages.data());

        m_SwapViews.resize(m_SwapImages.size());
        for (size_t i = 0; i < m_SwapImages.size(); ++i)
        {
            VkImageViewCreateInfo iv{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            iv.image = m_SwapImages[i];
            iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
            iv.format = m_SwapFormat;
            iv.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                              VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
            iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            iv.subresourceRange.baseMipLevel = 0;
            iv.subresourceRange.levelCount = 1;
            iv.subresourceRange.baseArrayLayer = 0;
            iv.subresourceRange.layerCount = 1;

            VK_ThrowIfFailed(vkCreateImageView(m_Device, &iv, nullptr, &m_SwapViews[i]),
                "vkCreateImageView failed");
        }

        VkAttachmentDescription color{};
        color.format = m_SwapFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &colorRef;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        rp.attachmentCount = 1;
        rp.pAttachments = &color;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;

        VK_ThrowIfFailed(vkCreateRenderPass(m_Device, &rp, nullptr, &m_RenderPass),
            "vkCreateRenderPass failed");

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) * 16;

        VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plci.setLayoutCount = 0;
        plci.pSetLayouts = nullptr;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcr;

        VK_ThrowIfFailed(vkCreatePipelineLayout(m_Device, &plci, nullptr, &m_PipelineLayout),
            "vkCreatePipelineLayout failed");

        VkShaderModule vs = LoadShaderModule(m_VertSpvPath.c_str());
        VkShaderModule fs = LoadShaderModule(m_FragSpvPath.c_str());
        if (vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE)
            throw std::runtime_error("Failed to load Vulkan shaders (.spv)");


        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";

        auto binding = VertexBinding();
        auto attrs = VertexAttributes();

        VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
        vi.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkViewport vp{};
        vp.x = 0.0f;
        vp.y = (float)m_SwapExtent.height;
        vp.width = (float)m_SwapExtent.width;
        vp.height = -(float)m_SwapExtent.height;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = m_SwapExtent;

        VkPipelineViewportStateCreateInfo vpState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        vpState.viewportCount = 1;
        vpState.pViewports = &vp;
        vpState.scissorCount = 1;
        vpState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.depthClampEnable = VK_FALSE;
        rs.rasterizerDiscardEnable = VK_FALSE;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_BACK_BIT;
        rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rs.depthBiasEnable = VK_FALSE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cbAtt{};
        cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cbAtt.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        cb.attachmentCount = 1;
        cb.pAttachments = &cbAtt;

        VkGraphicsPipelineCreateInfo gp{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vpState;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pColorBlendState = &cb;
        gp.layout = m_PipelineLayout;
        gp.renderPass = m_RenderPass;
        gp.subpass = 0;

        VK_ThrowIfFailed(vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &gp, nullptr, &m_Pipeline),
            "vkCreateGraphicsPipelines failed");

        vkDestroyShaderModule(m_Device, vs, nullptr);
        vkDestroyShaderModule(m_Device, fs, nullptr);

        m_Framebuffers.resize(m_SwapViews.size());
        for (size_t i = 0; i < m_SwapViews.size(); ++i)
        {
            VkImageView att[] = { m_SwapViews[i] };
            VkFramebufferCreateInfo fbi{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            fbi.renderPass = m_RenderPass;
            fbi.attachmentCount = 1;
            fbi.pAttachments = att;
            fbi.width = m_SwapExtent.width;
            fbi.height = m_SwapExtent.height;
            fbi.layers = 1;

            VK_ThrowIfFailed(vkCreateFramebuffer(m_Device, &fbi, nullptr, &m_Framebuffers[i]),
                "vkCreateFramebuffer failed");
        }

        VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pci.queueFamilyIndex = m_GraphicsFamily;
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_ThrowIfFailed(vkCreateCommandPool(m_Device, &pci, nullptr, &m_CmdPool),
            "vkCreateCommandPool failed");

        m_CmdBuffers.resize(m_Framebuffers.size());
        VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        ai.commandPool = m_CmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = (uint32_t)m_CmdBuffers.size();
        VK_ThrowIfFailed(vkAllocateCommandBuffers(m_Device, &ai, m_CmdBuffers.data()),
            "vkAllocateCommandBuffers failed");

        Vertex verts[3] = {
            { { 0.0f,  0.5f }, { 1.f, 0.f, 0.f } },
            { { 0.5f, -0.5f }, { 0.f, 1.f, 0.f } },
            { {-0.5f, -0.5f }, { 0.f, 0.f, 1.f } },
        };

        VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bci.size = sizeof(verts);
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_ThrowIfFailed(vkCreateBuffer(m_Device, &bci, nullptr, &m_VB), "vkCreateBuffer failed");

        VkMemoryRequirements mr{};
        vkGetBufferMemoryRequirements(m_Device, m_VB, &mr);

        VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = FindMemoryType(m_Physical, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VK_ThrowIfFailed(vkAllocateMemory(m_Device, &mai, nullptr, &m_VBMem), "vkAllocateMemory failed");
        VK_ThrowIfFailed(vkBindBufferMemory(m_Device, m_VB, m_VBMem, 0), "vkBindBufferMemory failed");

        void* mapped = nullptr;
        VK_ThrowIfFailed(vkMapMemory(m_Device, m_VBMem, 0, sizeof(verts), 0, &mapped), "vkMapMemory failed");
        std::memcpy(mapped, verts, sizeof(verts));
        vkUnmapMemory(m_Device, m_VBMem);

        VkSemaphoreCreateInfo sci2{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MaxFramesInFlight; ++i)
        {
            VK_ThrowIfFailed(vkCreateSemaphore(m_Device, &sci2, nullptr, &m_ImageAvailable[i]),
                "vkCreateSemaphore failed");
            VK_ThrowIfFailed(vkCreateSemaphore(m_Device, &sci2, nullptr, &m_RenderFinished[i]),
                "vkCreateSemaphore failed");
            VK_ThrowIfFailed(vkCreateFence(m_Device, &fci, nullptr, &m_InFlight[i]),
                "vkCreateFence failed");
        }

        m_Frame = 0;
        Logger::Get().Info("Vulkan adapter initialized");
        return true;
    }
    catch (const std::exception& e)
    {
        Logger::Get().Error(std::string("Vulkan init exception: ") + e.what());
        return false;
    }
}

void VkRenderAdapter::BeginFrame()
{
    vkWaitForFences(m_Device, 1, &m_InFlight[m_Frame], VK_TRUE, UINT64_MAX);
    vkResetFences(m_Device, 1, &m_InFlight[m_Frame]);

    VkResult r = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX,
        m_ImageAvailable[m_Frame], VK_NULL_HANDLE, &m_ImageIndex);

    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        VK_ThrowIfFailed(r, "vkAcquireNextImageKHR failed");

    vkResetCommandBuffer(m_CmdBuffers[m_ImageIndex], 0);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VK_ThrowIfFailed(vkBeginCommandBuffer(m_CmdBuffers[m_ImageIndex], &bi), "vkBeginCommandBuffer failed");
}

void VkRenderAdapter::Clear(float r, float g, float b, float a)
{
    m_Clear[0] = r; m_Clear[1] = g; m_Clear[2] = b; m_Clear[3] = a;

    VkClearValue cv{};
    cv.color.float32[0] = m_Clear[0];
    cv.color.float32[1] = m_Clear[1];
    cv.color.float32[2] = m_Clear[2];
    cv.color.float32[3] = m_Clear[3];

    VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rp.renderPass = m_RenderPass;
    rp.framebuffer = m_Framebuffers[m_ImageIndex];
    rp.renderArea.offset = { 0,0 };
    rp.renderArea.extent = m_SwapExtent;
    rp.clearValueCount = 1;
    rp.pClearValues = &cv;

    vkCmdBeginRenderPass(m_CmdBuffers[m_ImageIndex], &rp, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(m_CmdBuffers[m_ImageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
}

void VkRenderAdapter::DrawTestTriangle()
{
    vkCmdPushConstants(
        m_CmdBuffers[m_ImageIndex],
        m_PipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(float) * 16,
        m_PendingMVP
    );

    VkDeviceSize offs = 0;
    vkCmdBindVertexBuffers(m_CmdBuffers[m_ImageIndex], 0, 1, &m_VB, &offs);
    vkCmdDraw(m_CmdBuffers[m_ImageIndex], 3, 1, 0, 0);
}

void VkRenderAdapter::EndFrame()
{
    vkCmdEndRenderPass(m_CmdBuffers[m_ImageIndex]);
    VK_ThrowIfFailed(vkEndCommandBuffer(m_CmdBuffers[m_ImageIndex]), "vkEndCommandBuffer failed");

    VkSemaphore waitS[] = { m_ImageAvailable[m_Frame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalS[] = { m_RenderFinished[m_Frame] };

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = waitS;
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &m_CmdBuffers[m_ImageIndex];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = signalS;

    VK_ThrowIfFailed(vkQueueSubmit(m_GraphicsQueue, 1, &si, m_InFlight[m_Frame]), "vkQueueSubmit failed");
}

void VkRenderAdapter::Present()
{
    VkSemaphore signalS[] = { m_RenderFinished[m_Frame] };

    VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = signalS;
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_Swapchain;
    pi.pImageIndices = &m_ImageIndex;

    VkResult r = vkQueuePresentKHR(m_PresentQueue, &pi);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        VK_ThrowIfFailed(r, "vkQueuePresentKHR failed");

    m_Frame = (m_Frame + 1) % MaxFramesInFlight;
}

void VkRenderAdapter::Shutdown()
{
    if (!m_Device) return;

    vkDeviceWaitIdle(m_Device);

    for (int i = 0; i < MaxFramesInFlight; ++i)
    {
        if (m_ImageAvailable[i]) vkDestroySemaphore(m_Device, m_ImageAvailable[i], nullptr);
        if (m_RenderFinished[i]) vkDestroySemaphore(m_Device, m_RenderFinished[i], nullptr);
        if (m_InFlight[i]) vkDestroyFence(m_Device, m_InFlight[i], nullptr);
        m_ImageAvailable[i] = VK_NULL_HANDLE;
        m_RenderFinished[i] = VK_NULL_HANDLE;
        m_InFlight[i] = VK_NULL_HANDLE;
    }

    if (m_VB) vkDestroyBuffer(m_Device, m_VB, nullptr);
    if (m_VBMem) vkFreeMemory(m_Device, m_VBMem, nullptr);
    m_VB = VK_NULL_HANDLE;
    m_VBMem = VK_NULL_HANDLE;

    if (m_CmdPool) vkDestroyCommandPool(m_Device, m_CmdPool, nullptr);
    m_CmdPool = VK_NULL_HANDLE;

    for (auto fb : m_Framebuffers) vkDestroyFramebuffer(m_Device, fb, nullptr);
    m_Framebuffers.clear();

    if (m_Pipeline) vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
    if (m_PipelineLayout) vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
    if (m_RenderPass) vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);

    for (auto v : m_SwapViews) vkDestroyImageView(m_Device, v, nullptr);
    m_SwapViews.clear();

    if (m_Swapchain) vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);

    if (m_Surface) vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    if (m_Device) vkDestroyDevice(m_Device, nullptr);
    if (m_Instance) vkDestroyInstance(m_Instance, nullptr);

    m_Swapchain = VK_NULL_HANDLE;
    m_Surface = VK_NULL_HANDLE;
    m_Device = VK_NULL_HANDLE;
    m_Instance = VK_NULL_HANDLE;

    Logger::Get().Info("Vulkan adapter shutdown");
}

void VkRenderAdapter::SetTestTransform(const float* mvp16)
{
    memcpy(m_PendingMVP, mvp16, sizeof(float) * 16);
}

bool VkRenderAdapter::ReloadShaders()
{
    Logger::Get().Info("Vulkan: ReloadShaders requested");

    vkDeviceWaitIdle(m_Device);

    const bool ok = RecreateGraphicsPipeline();
    if (ok) Logger::Get().Info("Vulkan: ReloadShaders OK");
    else    Logger::Get().Error("Vulkan: ReloadShaders FAILED (old pipeline kept)");

    return ok;
}

bool VkRenderAdapter::HotReloadShaders()
{
    Logger::Get().Info("Vulkan: HotReloadShaders (compile GLSL -> SPV) requested");

    std::string err;

    if (!CompileGlslToSpv(m_VertSrcPath, m_VertSpvPath, "vert", err))
    {
        Logger::Get().Error("Vulkan: vertex shader compile failed: " + err);
        return false;
    }

    if (!CompileGlslToSpv(m_FragSrcPath, m_FragSpvPath, "frag", err))
    {
        Logger::Get().Error("Vulkan: fragment shader compile failed: " + err);
        return false;
    }

    return ReloadShaders();
}


bool VkRenderAdapter::CompileGlslToSpv(const std::string& srcPath,
    const std::string& outSpvPath,
    const char* stage, // "vert" / "frag"
    std::string& outErr)
{
    namespace fs = std::filesystem;

    std::string resolvedSrc;
    if (!ResolveExistingPath(srcPath, resolvedSrc))
    {
        outErr = "Shader source not found: " + srcPath;
        return false;
    }

#ifdef _WIN32
    const wchar_t* sdk = _wgetenv(L"VULKAN_SDK");
    if (!sdk || !sdk[0])
    {
        outErr = "VULKAN_SDK env var is not set";
        return false;
    }

    fs::path validator = fs::path(sdk) / "Bin" / "glslangValidator.exe";
    if (!fs::exists(validator))
    {
        outErr = "glslangValidator not found: " + validator.string();
        return false;
    }

    fs::path srcAbs = fs::absolute(fs::path(resolvedSrc));
    fs::path spvAbs = fs::absolute(fs::path(outSpvPath));
    fs::create_directories(spvAbs.parent_path());

    Logger::Get().Info("Vulkan: compiling shader: " + srcAbs.string());
    Logger::Get().Info("Vulkan: spv out: " + spvAbs.string());

    std::wstring cmd =
        L"\"" + validator.wstring() + L"\""
        L" -V -S " + std::wstring(stage, stage + strlen(stage)) +
        L" \"" + srcAbs.wstring() + L"\"" +
        L" -o \"" + spvAbs.wstring() + L"\"";

    int code = RunProcessW(cmd);
    if (code != 0)
    {
        outErr = "glslangValidator failed for: " + srcAbs.string() + " (exit code=" + std::to_string(code) + ")";
        return false;
    }

    if (!fs::exists(spvAbs))
    {
        outErr = "SPV was not produced: " + spvAbs.string();
        return false;
    }

    return true;
#else
    outErr = "HotReloadShaders is implemented for Windows only in this build.";
    return false;
#endif
}

std::string VkRenderAdapter::Quote(const std::string& s)
{
    std::string q = "\"";
    q += s;
    q += "\"";
    return q;
}

#endif
