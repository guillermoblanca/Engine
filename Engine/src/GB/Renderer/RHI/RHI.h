#pragma once
// The render hardware interface (T25, ARQUITECTURA_REVISION §5.1 and PLAN_RENDER §4): a small explicit layer between the render frontend
// (scene, passes, materials) and the graphics API. The frontend creates buffers, textures, render targets, programs and pipelines through a
// Device and gets opaque handles back; it records its work into a CommandList (begin a pass with its attachments, bind a pipeline and its
// resources, draw), which the backend executes in order. No queues and no manual barriers: a backend that needs them adds them inside.
//
// Backends: GL (RHI/GL, the only real one: OpenGL 4.5, where commands run as they are recorded) and Null (RHI/NullDevice.h: accepts
// everything, draws nothing and logs the commands, for the CPU tests of the render without a GL context). Nothing in this header or in the
// frontend knows GL.
//
// Pipeline state is "light": a pipeline is a program plus the fixed-function state it sets; a state left unset keeps what the stream set
// before (GL semantics; a backend with full pipeline objects resolves it at record time). The constant blocks (T36) and the shader sources
// out of Shaders.h (T41) come later: uniforms are still set one by one, by location.
//
// T70: the backends count what is recorded (RHI/Counters.h) and the GL one can filter repeated uniform values and repeated state (Options).
//
// T71 (render M1, DESACOPLE_MATERIALES §1.5 and §2): what the constant blocks, the material and instance tables and the texture arrays of
// M3-M7 need, none of it used by the renderer yet: uniform and storage buffers bound by range, indexed instanced draws with first index,
// base vertex and base instance, 2D texture arrays, BC5/BC7 formats uploaded as compressed blocks, the device's limits and a persistent
// upload ring (UploadRing).
//
// Build targets (T65, ARQUITECTURA_REVISION §5.2): this header, Counters.h, UniformCache.h and the null backend are GB_RHI (only GB_Core);
// RHI/GL is GB_RHI_GL, the only one that links glad. GetDevice() and CreateGLDevice() are defined there: code that links GB_RHI alone (the
// CPU tests of the RHI) holds its own device and installs it with SetDevice.
#include "GB/Renderer/RHI/Counters.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GB::RHI
{
	// Opaque handles: 0 is "none". (In the GL backend the id is the GL object name, which the editor's ImGui images still use.)
	template <typename Tag>
	struct Handle
	{
		uint32_t Id = 0;
		constexpr explicit operator bool() const { return Id != 0; }
		constexpr bool operator==(const Handle&) const = default;
	};
	using BufferHandle = Handle<struct BufferTag>;
	using TextureHandle = Handle<struct TextureTag>;
	using RenderTargetHandle = Handle<struct RenderTargetTag>;   // a framebuffer: its attachments are textures (or a depth buffer)
	using ProgramHandle = Handle<struct ProgramTag>;
	using VertexLayoutHandle = Handle<struct VertexLayoutTag>;   // vertex attributes + the buffers bound to them (+ index buffer)
	using PipelineHandle = Handle<struct PipelineTag>;
	using QueryHandle = Handle<struct QueryTag>;                 // a GPU timestamp

	enum class Format : uint8_t
	{
		None,
		RGBA8, SRGB8A8, RGBA16F, RGB16F, R11G11B10F,   // color
		Depth24,                                       // depth
		// Block-compressed (T71): 4x4 texels in 16 bytes, uploaded with UploadCompressed, never rendered to. BC7 for albedo (sRGB) and ORM,
		// BC5 (two channels, red and green) for normals.
		BC7, BC7SRGB, BC5
	};

	constexpr bool IsCompressed(Format format) { return format == Format::BC7 || format == Format::BC7SRGB || format == Format::BC5; }
	// The bytes of a width x height block of a compressed format (whole 4x4 blocks, the edges rounded up); 0 for an uncompressed one.
	constexpr size_t CompressedBytes(Format format, uint32_t width, uint32_t height)
	{
		return IsCompressed(format) ? static_cast<size_t>((width + 3) / 4) * ((height + 3) / 4) * 16 : 0;
	}

	// The layout of pixels the CPU sends or reads.
	enum class PixelData : uint8_t { RGBA8, RGBFloat, RedFloat };

	enum class Filter : uint8_t { Nearest, Linear, LinearMipLinear };
	enum class Wrap : uint8_t { Repeat, ClampToEdge, ClampToBorder };

	struct SamplerDesc
	{
		Filter Min = Filter::Linear, Mag = Filter::Linear;
		Wrap WrapS = Wrap::Repeat, WrapT = Wrap::Repeat;
		float MaxAnisotropy = 1.0f;                     // > 1: anisotropic filtering, clamped to what the device allows
		std::optional<glm::vec4> BorderColor;           // with ClampToBorder
		bool DepthCompare = false;                      // comparison sampler (shadow maps): ref <= texel
	};

	struct TextureDesc
	{
		uint32_t Width = 1, Height = 1;
		uint32_t Levels = 1;      // mip levels
		uint32_t Samples = 1;     // > 1: multisampled (no mips, no sampler, no layers)
		Format Fmt = Format::RGBA8;
		// T71: a 2D array when Layers > 1 or Array is set (a one-layer array: Array). Otherwise a plain 2D texture, as before. Every layer has
		// the size, format and levels above; UploadTextureLayer and UploadCompressed fill one layer, GenerateMips does every layer.
		uint32_t Layers = 1;
		bool Array = false;
		constexpr bool IsArray() const { return Array || Layers > 1; }
	};

	// What the device allows, read when it is created (T71). Offsets of BindUniformBuffer and BindStorageBufferRange must be multiples of the
	// alignments; UploadRing::Allocate rounds up to them.
	struct DeviceLimits
	{
		uint32_t UniformBufferAlignment = 256;    // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT
		uint32_t StorageBufferAlignment = 256;    // GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT
		uint32_t MaxUniformBlockBytes = 16384;    // GL_MAX_UNIFORM_BLOCK_SIZE
		uint32_t MaxArrayLayers = 256;            // GL_MAX_ARRAY_TEXTURE_LAYERS
	};

	// A piece of an upload ring: write `Bytes` at `Ptr` (CPU memory mapped to `Buffer`), then bind Buffer at Offset. Empty (no buffer, null Ptr)
	// when the region of the frame is full.
	struct RingAllocation
	{
		BufferHandle Buffer;
		size_t Offset = 0;
		size_t Bytes = 0;
		void* Ptr = nullptr;
		constexpr explicit operator bool() const { return Ptr != nullptr; }
	};

	// A persistently mapped buffer split in N regions (3 by default), one per frame in flight (T71, DESACOPLE_MATERIALES §2.6): the frame writes
	// its blocks (constants, materials, instances) into its region and binds them by range; nothing is copied by the driver. BeginFrame moves to
	// the next region and, in GL, waits for the fence the GPU signals when it is done with that region's last use; EndFrame sets the fence.
	// Allocate never wraps into a region in use: when the frame's region is full it returns an empty allocation (the caller falls back or the
	// ring is created bigger).
	//
	// The bookkeeping (regions, offsets, alignment) is here and shared; a backend maps the buffer and waits for and sets its fences. Allocate
	// works between BeginFrame and EndFrame (before the first BeginFrame it returns empty).
	class UploadRing
	{
	public:
		virtual ~UploadRing() = default;
		UploadRing(const UploadRing&) = delete;
		UploadRing& operator=(const UploadRing&) = delete;

		void BeginFrame();
		void EndFrame();
		// `align` 0: the ring's default, the larger of the device's uniform and storage alignments. Counts in FrameCounters::RingBytes.
		RingAllocation Allocate(size_t bytes, size_t align = 0);

		BufferHandle Buffer() const { return m_Buffer; }
		size_t RegionBytes() const { return m_RegionBytes; }
		uint32_t Regions() const { return m_Regions; }
		uint32_t Region() const { return m_Region; }     // the region of the current frame
		size_t Used() const { return m_Used; }            // bytes allocated in it, with the alignment padding
		size_t DefaultAlignment() const { return m_DefaultAlign; }

	protected:
		// `mapped`: the CPU address of the whole buffer (regions x regionBytes), valid for the ring's life.
		UploadRing(BufferHandle buffer, std::byte* mapped, size_t regionBytes, uint32_t regions, size_t defaultAlign);
		virtual void WaitRegion(uint32_t region) = 0;    // before the region is written again: the GPU is done with it
		virtual void FenceRegion(uint32_t region) = 0;   // after the frame's commands that read it

	private:
		BufferHandle m_Buffer;
		std::byte* m_Mapped = nullptr;
		size_t m_RegionBytes = 0, m_Used = 0, m_DefaultAlign = 1;
		uint32_t m_Regions = 1, m_Region = 0;
		bool m_InFrame = false;
	};

	// One indexed, instanced draw (T71). Indices are 32-bit; BaseVertex is added to every index; BaseInstance offsets the per-instance attributes
	// (divisor 1), which is how a draw reads its range of the instance table in GL 4.5 (DESACOPLE_MATERIALES §2.3: gl_InstanceID starts at 0).
	struct IndexedDraw
	{
		uint32_t Count = 0;
		uint32_t Instances = 1;
		uint32_t FirstIndex = 0;
		int32_t BaseVertex = 0;
		uint32_t BaseInstance = 0;
	};

	// A render target with its attachments, created whole: color 0, an optional second color (fragment output 1) and depth, as a texture or a
	// buffer only the target uses.
	struct RenderTargetDesc
	{
		uint32_t Width = 1, Height = 1;
		uint32_t Samples = 1;
		Format Color = Format::RGBA8;      // None: depth only
		Format Extra = Format::None;       // second color attachment, same samples
		bool Depth = true;                 // 24-bit depth
		bool DepthAsTexture = false;       // depth in a texture (sampled later) instead of a buffer
		bool DepthCompare = true;          // with DepthAsTexture: comparison sampler (true) or a plain one with nearest filtering
	};
	struct RenderTarget
	{
		RenderTargetHandle Handle;
		TextureHandle Color, Extra, DepthTexture;
		bool Complete = false;
	};

	enum class AttribType : uint8_t { Float, UInt };
	struct VertexAttrib
	{
		uint32_t Location = 0;
		uint32_t Components = 4;
		AttribType Type = AttribType::Float;
		uint32_t Offset = 0;
		uint32_t Binding = 0;
	};
	struct VertexBinding
	{
		uint32_t Binding = 0;
		BufferHandle Buffer;       // may be none: set it later with CommandList::SetVertexBuffer
		uint32_t Offset = 0;
		uint32_t Stride = 0;
		uint32_t Divisor = 0;      // 1: per instance
	};
	struct VertexLayoutDesc
	{
		std::vector<VertexAttrib> Attribs;
		std::vector<VertexBinding> Bindings;
		BufferHandle Indices;      // 32-bit indices
	};

	enum class Topology : uint8_t { Triangles, TriangleStrip };
	enum class Cull : uint8_t { None, Back };
	enum class BlendFactor : uint8_t { Zero, One, SrcAlpha, OneMinusSrcAlpha, ConstantColor };
	enum class BlendOp : uint8_t { Add, Max };
	struct BlendState
	{
		bool Enabled = false;
		BlendFactor Src = BlendFactor::One, Dst = BlendFactor::Zero;
		std::optional<BlendOp> Op;   // unset: keep the equation
	};
	struct PolygonOffset
	{
		bool Enabled = false;
		float Factor = 0.0f, Units = 0.0f;
	};

	// A program and the fixed-function state it draws with. Unset fields keep what the command stream set before.
	struct PipelineDesc
	{
		ProgramHandle Program;
		std::optional<bool> DepthTest, DepthWrite, AlphaToCoverage, Multisample, Scissor;
		std::optional<Cull> CullMode;
		std::optional<BlendState> Blend;
		std::optional<PolygonOffset> Offset;
	};

	// The start of a pass: where it draws (none = the window's back buffer), the viewport (the target's size) and what it clears.
	struct PassDesc
	{
		RenderTargetHandle Target;
		uint32_t Width = 1, Height = 1;
		std::optional<glm::vec4> ClearColor;
		bool ClearDepth = false;            // clears to 1 (writes depth on: the pass starts with depth writes enabled)
		const char* Name = "";
	};

	enum class BlitFilter : uint8_t { Nearest, Linear };

	// The work of a frame, recorded in order. In the GL backend each call runs as it is recorded; Null logs it. All data passed in is copied
	// (or consumed) before the call returns: the caller may reuse its buffers.
	class CommandList
	{
	public:
		virtual ~CommandList() = default;

		// A pass ends at EndPass or at the next BeginPass (a pass may begin again on the same target after a blit, without clearing).
		virtual void BeginPass(const PassDesc& pass) = 0;
		virtual void EndPass() = 0;
		// Which color attachments of `target` draws write: 1 = only color 0, 2 = color 0 and the extra one.
		virtual void SetDrawAttachments(RenderTargetHandle target, uint32_t count) = 0;
		virtual void ClearColorAttachment(RenderTargetHandle target, uint32_t attachment, const glm::vec4& value) = 0;   // honors the scissor

		virtual void BindPipeline(PipelineHandle pipeline) = 0;
		// Dynamic state, changed between draws of one pipeline.
		virtual void SetCull(Cull cull) = 0;
		virtual void SetAlphaToCoverage(bool on) = 0;
		virtual void SetDepthWrite(bool on) = 0;
		virtual void SetBlend(const BlendState& blend) = 0;
		virtual void SetBlendColor(const glm::vec4& color) = 0;
		// Scissor test: on with the rectangle (x, y, width, height), off with none.
		virtual void SetScissor(const std::optional<glm::ivec4>& rect) = 0;

		// Uniforms of a program (it need not be bound). `location` < 0 is ignored.
		virtual void SetUniformInt(ProgramHandle program, int location, int value) = 0;
		virtual void SetUniformFloats(ProgramHandle program, int location, uint32_t components, uint32_t count, const float* values) = 0;   // float, vec2..4 arrays
		virtual void SetUniformMat4(ProgramHandle program, int location, const glm::mat4& value) = 0;

		virtual void BindTexture(uint32_t unit, TextureHandle texture) = 0;
		virtual void BindStorageBuffer(uint32_t slot, BufferHandle buffer) = 0;
		// T71: a range of a buffer as the uniform block / storage buffer at `slot` (`offset` a multiple of the device's alignment). `bytes` 0:
		// the whole buffer.
		virtual void BindUniformBuffer(uint32_t slot, BufferHandle buffer, size_t offset, size_t bytes) = 0;
		virtual void BindStorageBufferRange(uint32_t slot, BufferHandle buffer, size_t offset, size_t bytes) = 0;
		// Replaces the whole contents of a stream buffer (new storage each time: the draws before keep reading the old data).
		virtual void UploadStream(BufferHandle buffer, size_t bytes, const void* data) = 0;

		virtual void BindVertexLayout(VertexLayoutHandle layout) = 0;
		virtual void SetVertexBuffer(VertexLayoutHandle layout, uint32_t binding, BufferHandle buffer, size_t offset, uint32_t stride) = 0;
		virtual void Draw(Topology topology, uint32_t first, uint32_t count) = 0;
		virtual void DrawInstanced(Topology topology, uint32_t first, uint32_t count, uint32_t instances) = 0;
		virtual void DrawIndexed(uint32_t firstIndex, uint32_t count) = 0;
		virtual void DrawIndexedInstanced(const IndexedDraw& draw) = 0;
		// The draw of before T71: from the first index, base vertex and base instance 0.
		void DrawIndexedInstanced(uint32_t count, uint32_t instances) { DrawIndexedInstanced(IndexedDraw{ count, instances, 0, 0, 0 }); }

		// Copies color attachment `attachment` of `src` into color 0 of `dst` (resolving MSAA), or the depth.
		virtual void BlitColor(const RenderTarget& src, uint32_t srcW, uint32_t srcH, const RenderTarget& dst, uint32_t dstW, uint32_t dstH,
			uint32_t attachment, BlitFilter filter) = 0;
		virtual void BlitDepth(const RenderTarget& src, uint32_t srcW, uint32_t srcH, const RenderTarget& dst, uint32_t dstW, uint32_t dstH) = 0;

		virtual void WriteTimestamp(QueryHandle query) = 0;
		// Sends what was recorded to the GPU without waiting (the frame's GPU timers need it).
		virtual void Flush() = 0;
	};

	// Creates and destroys the resources; reads back. Destroying a none handle is a no-op.
	class Device
	{
	public:
		virtual ~Device() = default;
		virtual const char* Name() const = 0;
		virtual CommandList& Commands() = 0;   // the device's command list (one context: no threads yet)
		virtual const DeviceLimits& Limits() const = 0;

		virtual BufferHandle CreateBuffer(size_t bytes, const void* data, bool dynamic) = 0;   // fixed size; `dynamic`: UpdateBuffer allowed
		virtual BufferHandle CreateStreamBuffer() = 0;                                         // filled by CommandList::UploadStream
		virtual void UpdateBuffer(BufferHandle buffer, size_t offset, size_t bytes, const void* data) = 0;
		virtual void DestroyBuffer(BufferHandle buffer) = 0;
		// T71: a persistent upload ring of `regions` x `regionBytes` (see UploadRing). It owns its buffer; destroy it before the device.
		virtual std::unique_ptr<UploadRing> CreateUploadRing(size_t regionBytes, uint32_t regions) = 0;
		std::unique_ptr<UploadRing> CreateUploadRing(size_t regionBytes) { return CreateUploadRing(regionBytes, 3); }

		virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
		virtual void SetSampler(TextureHandle texture, const SamplerDesc& sampler) = 0;
		virtual void UploadTexture(TextureHandle texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, PixelData layout, const void* data) = 0;   // level 0
		virtual void GenerateMips(TextureHandle texture) = 0;
		// Reads a block of level 0 into `out` (`bytes` long).
		virtual void ReadTexture(TextureHandle texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, PixelData layout, size_t bytes, void* out) = 0;
		// T71: a block of one mip level of one layer (layer 0 of a plain 2D texture), uncompressed pixels...
		virtual void UploadTextureLayer(TextureHandle texture, uint32_t level, uint32_t layer, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
			PixelData layout, const void* data) = 0;
		// ... or compressed blocks of the texture's format (BC5, BC7): x, y, width and height in texels, multiples of 4 (or up to the level's
		// edge); `bytes` is CompressedBytes(format, width, height).
		virtual void UploadCompressed(TextureHandle texture, uint32_t level, uint32_t layer, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
			size_t bytes, const void* data) = 0;
		virtual void ReadTextureLayer(TextureHandle texture, uint32_t level, uint32_t layer, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
			PixelData layout, size_t bytes, void* out) = 0;
		// The compressed blocks as stored (for the tests: a fixed block reads back bit for bit).
		virtual void ReadCompressed(TextureHandle texture, uint32_t level, uint32_t layer, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
			size_t bytes, void* out) = 0;
		virtual void DestroyTexture(TextureHandle texture) = 0;

		virtual RenderTarget CreateRenderTarget(const RenderTargetDesc& desc) = 0;
		virtual void DestroyRenderTarget(RenderTarget& target) = 0;
		// RGBA8 pixels of color 0 of `target` (none = the back buffer), bottom row first, tightly packed.
		virtual void ReadPixels(RenderTargetHandle target, uint32_t width, uint32_t height, uint8_t* out) = 0;

		// Compiles and links; on failure logs the errors and returns none.
		virtual ProgramHandle CreateProgram(const std::string& vertexSource, const std::string& fragmentSource) = 0;
		virtual int GetUniformLocation(ProgramHandle program, const std::string& name) = 0;   // -1 if the program has no such uniform
		virtual void DestroyProgram(ProgramHandle program) = 0;

		virtual VertexLayoutHandle CreateVertexLayout(const VertexLayoutDesc& desc) = 0;
		virtual void DestroyVertexLayout(VertexLayoutHandle layout) = 0;

		virtual PipelineHandle CreatePipeline(const PipelineDesc& desc) = 0;
		virtual void DestroyPipeline(PipelineHandle pipeline) = 0;

		virtual QueryHandle CreateTimestamp() = 0;
		virtual void DestroyTimestamp(QueryHandle query) = 0;
		virtual bool TimestampReady(QueryHandle query) = 0;      // never waits
		virtual uint64_t TimestampNs(QueryHandle query) = 0;     // waits if not ready
	};

	// Run-time switches of the backend (T70, render M0), to measure what they save; the editor binds them to render.uniform_cache,
	// render.state_filter and render.string_uniforms. The defaults come from the environment, read once at start-up: GB_RENDER_UNIFORM_CACHE=0
	// and GB_RENDER_STATE_FILTER=0 turn the first two off (a test or a bench without the editor), GB_RENDER_STRING_UNIFORMS=1 turns the third on.
	// The GL backend honors them; the null one ignores the two filters (its log keeps every command) and only counts.
	struct Options
	{
		bool UniformCache = true;     // a uniform set to the value its program already holds is not sent again
		bool StateFilter = true;      // cull, alpha to coverage and blend set to the state already current are not sent again (reset at each pass)
		bool StringUniforms = false;  // Shader looks uniforms up through a temporary std::string, as before T70: only to measure what it cost
	};
	Options OptionsFromEnvironment();
	struct Tuning
	{
		static inline Options Current = OptionsFromEnvironment();
	};

	// The device the render uses. The first call with none set creates the GL device (a GL 4.5 context must be current by then); a test
	// installs another (the null device) with SetDevice before creating any resource, and SetDevice(nullptr) goes back to the default.
	// GetDevice is defined by the GL backend (RHI/GL/DefaultDevice.cpp, GB_RHI_GL): which device is the default is GL's business, not the
	// interface's. CurrentDevice is the one installed with SetDevice (or null).
	Device& GetDevice();
	void SetDevice(Device* device);
	Device* CurrentDevice();
	// Defined by the GL backend (RHI/GL/GLDevice.cpp).
	std::unique_ptr<Device> CreateGLDevice();
}
