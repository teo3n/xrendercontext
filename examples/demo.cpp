#include "core/xrendercontext.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <vector>

struct Vertex {
    float x, y;
    float r, g, b;
};

struct PushData {
    float angle;
    float aspect;
};

int main(int argc, char** argv) {
    uint32_t frameLimit = 0;
    for (int ii = 1; ii < argc; ii++) {
        const std::string arg = argv[ii];
        if (arg == "--frames" && ii + 1 < argc) {
            frameLimit = static_cast<uint32_t>(std::atoi(argv[++ii]));
        }
    }

    try {
        XRenderContext ctx;
        ctx.Init(960, 540, "XRender Demo");

        const std::vector<Vertex> vertices = {
            { 0.0f, -0.6f, 1.0f, 0.2f, 0.2f },
            { 0.6f, 0.4f, 0.2f, 1.0f, 0.3f },
            { -0.6f, 0.4f, 0.3f, 0.4f, 1.0f },
        };
        const std::vector<uint32_t> indices = { 0, 1, 2 };

        XBuffer vertexBuffer = ctx.CreateBuffer(sizeof(Vertex) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        XBuffer indexBuffer = ctx.CreateBuffer(sizeof(uint32_t) * indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

        ctx.UploadBuffer(vertexBuffer, vertices.data(), sizeof(Vertex) * vertices.size());
        ctx.UploadBuffer(indexBuffer, indices.data(), sizeof(uint32_t) * indices.size());

        XGraphicsPipelineOptions options;
        options.vertexBindings = { { 0, sizeof(Vertex) } };
        options.vertexAttributes = {
            { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, x) },
            { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, r) },
        };
        options.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        options.depthTest = true;
        options.blendEnable = false;

        XGraphicsPipeline pipeline = ctx.CreateGraphicsPipeline("demo_vert.spv", "demo_frag.spv", {}, { { VK_SHADER_STAGE_VERTEX_BIT, sizeof(PushData) } }, options);

        const auto start = std::chrono::steady_clock::now();
        uint32_t frame = 0;

        while (!ctx.ShouldClose()) {
            ctx.PollEvents();

            if (ctx.IsKeyPressed(GLFW_KEY_ESCAPE)) {
                break;
            }
            if (frameLimit != 0 && frame >= frameLimit) {
                break;
            }

            PushData push {};
            push.angle = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
            push.aspect = static_cast<float>(ctx.GetWidth()) / static_cast<float>(ctx.GetHeight());

            ctx.BeginFrame();

            ctx.BeginRenderPass(ctx.GetCurrentSwapchainFramebuffer(), ctx.GetWidth(), ctx.GetHeight());
            ctx.BindGraphicsPipeline(pipeline);
            ctx.PushConstants(pipeline, VK_SHADER_STAGE_VERTEX_BIT, push);
            ctx.BindVertexBuffer(vertexBuffer);
            ctx.BindIndexBuffer(indexBuffer, VK_INDEX_TYPE_UINT32);
            ctx.DrawIndexed(static_cast<uint32_t>(indices.size()));
            ctx.EndRenderPass();

            ctx.EndFrame();

            frame++;
        }

        ctx.WaitDeviceIdle();
        ctx.DestroyGraphicsPipeline(pipeline);
        ctx.DestroyBuffer(indexBuffer);
        ctx.DestroyBuffer(vertexBuffer);
        ctx.Cleanup();
        
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }

    return 0;
}
