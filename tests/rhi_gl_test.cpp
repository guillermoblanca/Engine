// T71 (render M1): the RHI's API of M1 on the GL backend, with a hidden window. The device's limits; BC7 (and its sRGB twin) and BC5
// textures filled with fixed blocks, read back bit for bit and decoded; a 2D array filled layer by layer; and one draw that uses
// everything else at once: its constants come from a uniform block and a storage buffer bound by range from the upload ring, and the
// draw picks its triangle with first index and base vertex and its colour with base instance. Then the ring goes round its regions (the
// fences). Nothing here is used by the renderer yet: the image of the game does not change.
#include "GB/Log.h"
#include "GB/Platform/Window.h"
#include "GB/Renderer/RHI/RHI.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
		else std::fprintf(stderr, "ok: %s\n", what);
	}
	bool PowerOfTwo(uint32_t v) { return v != 0 && (v & (v - 1)) == 0; }

	using namespace GB::RHI;

	// A BC7 block in mode 6 (one subset, 7-bit RGBA endpoints with a p-bit each, 4-bit indices) whose texels are all endpoint 0: with every
	// index 0 the weight is 0 and each texel decodes to (c << 1 | p) exactly, on any conforming decoder.
	std::array<uint8_t, 16> Bc7Mode6(uint32_t r, uint32_t g, uint32_t b, uint32_t a, uint32_t p)
	{
		std::array<uint8_t, 16> block{};
		uint32_t bit = 0;
		auto put = [&](uint32_t value, uint32_t bits)
		{
			for (uint32_t i = 0; i < bits; ++i, ++bit)
				if ((value >> i) & 1u)
					block[bit / 8] = static_cast<uint8_t>(block[bit / 8] | (1u << (bit % 8)));
		};
		put(1u << 6, 7);                     // mode 6: six 0 bits, then a 1
		for (uint32_t c : { r, g, b, a })    // R0 R1 G0 G1 B0 B1 A0 A1: both endpoints the same
		{
			put(c, 7);
			put(c, 7);
		}
		put(p, 1);
		put(p, 1);                           // the indices (63 bits) stay 0
		return block;
	}
}

int GBTest_rhi_gl(int, char**)
{
	GB::Log::Init();
	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP: no OpenGL 4.5 context\n");
		return 77;
	}
	Device& device = GetDevice();
	Check(std::strcmp(device.Name(), "GL") == 0, "the default device is GL");

	// ---- Limits ----
	const DeviceLimits& limits = device.Limits();
	std::fprintf(stderr, "limits: uniform align %u, storage align %u, uniform block %u B, array layers %u\n", limits.UniformBufferAlignment,
		limits.StorageBufferAlignment, limits.MaxUniformBlockBytes, limits.MaxArrayLayers);
	Check(PowerOfTwo(limits.UniformBufferAlignment) && limits.UniformBufferAlignment <= 256 && PowerOfTwo(limits.StorageBufferAlignment)
		&& limits.StorageBufferAlignment <= 256, "limits: the buffer offset alignments, read from GL");
	Check(limits.MaxUniformBlockBytes >= 16384 && limits.MaxArrayLayers >= 256, "limits: at least GL 4.5's minimums");

	// ---- BC7, BC7 sRGB and BC5: fixed blocks read back as stored and decoded ----
	const std::array<uint8_t, 16> bc7 = Bc7Mode6(127, 0, 64, 127, 1);   // (255, 1, 129, 255)
	TextureDesc desc;
	desc.Width = desc.Height = 4;
	desc.Fmt = Format::BC7;
	const TextureHandle t7 = device.CreateTexture(desc);
	device.UploadCompressed(t7, 0, 0, 0, 0, 4, 4, bc7.size(), bc7.data());
	std::array<uint8_t, 16> back{};
	device.ReadCompressed(t7, 0, 0, 0, 0, 4, 4, back.size(), back.data());
	Check(back == bc7, "BC7: the block reads back bit for bit");
	std::vector<uint8_t> rgba(4 * 4 * 4, 0);
	device.ReadTextureLayer(t7, 0, 0, 0, 0, 4, 4, PixelData::RGBA8, rgba.size(), rgba.data());
	bool decoded = true;
	for (size_t i = 0; i < 16; ++i)
		decoded = decoded && rgba[i * 4] == 255 && rgba[i * 4 + 1] == 1 && rgba[i * 4 + 2] == 129 && rgba[i * 4 + 3] == 255;
	std::fprintf(stderr, "BC7 texel 0: %u %u %u %u\n", rgba[0], rgba[1], rgba[2], rgba[3]);
	Check(decoded, "BC7: the 16 texels decode to the endpoint (255, 1, 129, 255)");

	desc.Fmt = Format::BC7SRGB;
	const TextureHandle t7s = device.CreateTexture(desc);
	device.UploadCompressed(t7s, 0, 0, 0, 0, 4, 4, bc7.size(), bc7.data());
	back.fill(0);
	device.ReadCompressed(t7s, 0, 0, 0, 0, 4, 4, back.size(), back.data());
	Check(back == bc7, "BC7 sRGB: the block reads back bit for bit");

	// BC5: two BC4 blocks (red, green); red0 > red1 is the 8-value mode, and index 0 is red0.
	std::array<uint8_t, 16> bc5{};
	bc5[0] = 200;
	bc5[8] = 50;
	desc.Fmt = Format::BC5;
	const TextureHandle t5 = device.CreateTexture(desc);
	device.UploadCompressed(t5, 0, 0, 0, 0, 4, 4, bc5.size(), bc5.data());
	back.fill(0);
	device.ReadCompressed(t5, 0, 0, 0, 0, 4, 4, back.size(), back.data());
	Check(back == bc5, "BC5: the block reads back bit for bit");
	device.ReadTextureLayer(t5, 0, 0, 0, 0, 4, 4, PixelData::RGBA8, rgba.size(), rgba.data());
	std::fprintf(stderr, "BC5 texel 0: %u %u %u %u\n", rgba[0], rgba[1], rgba[2], rgba[3]);
	Check(rgba[0] == 200 && rgba[1] == 50 && rgba[60] == 200 && rgba[61] == 50, "BC5: red and green decode to 200 and 50");

	// A BC7 array: the block into layer 2 of 3.
	desc.Fmt = Format::BC7;
	desc.Layers = 3;
	const TextureHandle t7a = device.CreateTexture(desc);
	device.UploadCompressed(t7a, 0, 2, 0, 0, 4, 4, bc7.size(), bc7.data());
	back.fill(0);
	device.ReadCompressed(t7a, 0, 2, 0, 0, 4, 4, back.size(), back.data());
	Check(back == bc7, "BC7 array: the block into layer 2 reads back from layer 2");

	// ---- An RGBA8 array, layer by layer ----
	TextureDesc arrayDesc;
	arrayDesc.Width = arrayDesc.Height = 2;
	arrayDesc.Layers = 3;
	const TextureHandle layers = device.CreateTexture(arrayDesc);
	for (uint32_t layer = 0; layer < 3; ++layer)
	{
		const std::vector<uint8_t> px(2 * 2 * 4, static_cast<uint8_t>(40 + 70 * layer));
		device.UploadTextureLayer(layers, 0, layer, 0, 0, 2, 2, PixelData::RGBA8, px.data());
	}
	bool layersOk = true;
	for (uint32_t layer = 0; layer < 3; ++layer)
	{
		std::vector<uint8_t> px(2 * 2 * 4, 0);
		device.ReadTextureLayer(layers, 0, layer, 0, 0, 2, 2, PixelData::RGBA8, px.size(), px.data());
		for (uint8_t v : px)
			layersOk = layersOk && v == 40 + 70 * layer;
	}
	Check(layersOk, "RGBA8 array: each of 3 layers keeps its own pixels");

	// ---- One draw: UBO and SSBO ranges from the ring, first index + base vertex + base instance ----
	const char* vs = R"(#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;   // per instance
out vec4 vColor;
void main() { vColor = aColor; gl_Position = vec4(aPos, 0.0, 1.0); }
)";
	const char* fs = R"(#version 450 core
layout(std140, binding = 0) uniform Frame { vec4 uTint; };
layout(std430, binding = 3) readonly buffer Table { vec4 uAdd[]; };
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor * uTint + uAdd[1]; }
)";
	const ProgramHandle program = device.CreateProgram(vs, fs);
	Check(static_cast<bool>(program), "draw: the test program compiles (a uniform block and a storage buffer)");

	// Vertices 0-2: a triangle off the screen; 3-5: one that covers it. Indices 0-2 degenerate; 3-5 the triangle. Only first index 3 with
	// base vertex 3 draws the big one. Instance colours: red, then green; base instance 1 picks green.
	const float positions[] = { -5, -5, -4, -5, -5, -4, -1, -1, 3, -1, -1, 3 };
	const uint32_t indices[] = { 0, 0, 0, 0, 1, 2 };
	const float colors[] = { 1, 0, 0, 1, 0, 1, 0, 1 };
	const BufferHandle vb = device.CreateBuffer(sizeof positions, positions, false);
	const BufferHandle ib = device.CreateBuffer(sizeof indices, indices, false);
	const BufferHandle cb = device.CreateBuffer(sizeof colors, colors, false);
	VertexLayoutDesc layoutDesc;
	layoutDesc.Attribs = { { 0, 2, AttribType::Float, 0, 0 }, { 1, 4, AttribType::Float, 0, 1 } };
	layoutDesc.Bindings = { { 0, vb, 0, 8u, 0 }, { 1, cb, 0, 16u, 1 } };
	layoutDesc.Indices = ib;
	const VertexLayoutHandle layout = device.CreateVertexLayout(layoutDesc);
	PipelineDesc pd;
	pd.Program = program;
	pd.DepthTest = false;
	pd.DepthWrite = false;
	pd.CullMode = Cull::None;
	pd.Blend = BlendState{};
	pd.Scissor = false;
	const PipelineHandle pipeline = device.CreatePipeline(pd);
	RenderTargetDesc rtDesc;
	rtDesc.Width = rtDesc.Height = 4;
	rtDesc.Depth = false;
	RenderTarget target = device.CreateRenderTarget(rtDesc);
	Check(target.Complete, "draw: a 4x4 RGBA8 target");

	std::unique_ptr<UploadRing> ring = device.CreateUploadRing(4096);
	Check(ring && ring->Regions() == 3, "ring: a persistent mapped ring of 3 regions");
	if (!ring)
		return failures;
	const FrameCounters before = Counters::Frame;
	ring->BeginFrame();
	// Junk first, so the blocks are at offsets past 0 (a bind that ignored the offset would read it): white, which would break every check.
	const RingAllocation junk = ring->Allocate(64);
	for (size_t i = 0; i < 16; ++i) static_cast<float*>(junk.Ptr)[i] = 1.0f;
	const RingAllocation ubo = ring->Allocate(16, limits.UniformBufferAlignment);
	const float tint[4] = { 0, 1, 1, 1 };
	std::memcpy(ubo.Ptr, tint, sizeof tint);
	const RingAllocation ssbo = ring->Allocate(32, limits.StorageBufferAlignment);
	const float table[8] = { 1, 1, 1, 0, 0, 0, 1, 0 };   // uAdd[0] would add red: only uAdd[1] (blue) is right
	std::memcpy(ssbo.Ptr, table, sizeof table);
	Check(ubo && ssbo && ubo.Offset > 0 && ubo.Offset % limits.UniformBufferAlignment == 0 && ssbo.Offset % limits.StorageBufferAlignment == 0,
		"ring: the blocks at aligned offsets past 0");

	CommandList& cmd = device.Commands();
	PassDesc pass;
	pass.Target = target.Handle;
	pass.Width = pass.Height = 4;
	pass.ClearColor = glm::vec4(0.0f);
	pass.Name = "rhi_gl";
	cmd.BeginPass(pass);
	cmd.BindPipeline(pipeline);
	cmd.BindUniformBuffer(0, ubo.Buffer, ubo.Offset, 16);
	cmd.BindStorageBufferRange(3, ssbo.Buffer, ssbo.Offset, 0);   // (0: from the offset to the end of the buffer)
	cmd.BindVertexLayout(layout);
	cmd.DrawIndexedInstanced(IndexedDraw{ 3, 1, 3, 3, 1 });
	cmd.EndPass();
	ring->EndFrame();
	std::vector<uint8_t> pixels(4 * 4 * 4, 0);
	device.ReadPixels(target.Handle, 4, 4, pixels.data());
	bool drawn = true;
	for (size_t i = 0; i < 16; ++i)
		drawn = drawn && pixels[i * 4] == 0 && pixels[i * 4 + 1] == 255 && pixels[i * 4 + 2] == 255 && pixels[i * 4 + 3] == 255;
	std::fprintf(stderr, "draw pixel 0: %u %u %u %u\n", pixels[0], pixels[1], pixels[2], pixels[3]);
	Check(drawn, "draw: green instance (base instance 1) x tint from the UBO range (16 B) + blue from the SSBO range (offset to the end), on the whole target");

	const FrameCounters d = Counters::Frame.Since(before);
	Check(d.UniformBufferBinds == 1 && d.StorageRangeBinds == 1 && d.RingBytes >= 64 + 16 + 32, "counters: the range binds and the ring's bytes");

	// ---- The ring goes round: each BeginFrame waits for the fence of the region it reuses ----
	bool rounds = true;
	for (uint32_t frame = 1; frame <= 6; ++frame)
	{
		ring->BeginFrame();
		const RingAllocation a = ring->Allocate(256);
		rounds = rounds && a && ring->Region() == frame % 3 && a.Offset == static_cast<size_t>(frame % 3) * 4096;
		std::memset(a.Ptr, static_cast<int>(frame), 256);
		cmd.BindUniformBuffer(0, a.Buffer, a.Offset, 256);
		ring->EndFrame();
	}
	Check(rounds, "ring: 6 more frames go round the 3 regions, each from its region's start");

	ring.reset();
	device.DestroyRenderTarget(target);
	device.DestroyPipeline(pipeline);
	device.DestroyVertexLayout(layout);
	device.DestroyBuffer(vb);
	device.DestroyBuffer(ib);
	device.DestroyBuffer(cb);
	device.DestroyProgram(program);
	for (TextureHandle t : { t7, t7s, t5, t7a, layers })
		device.DestroyTexture(t);
	return failures;
}
