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
#include "GB/Renderer/RHI/Counters.h"
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
		Depth24                                        // depth
	};

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
		uint32_t Samples = 1;     // > 1: multisampled (no mips, no sampler)
		Format Fmt = Format::RGBA8;
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
		// Replaces the whole contents of a stream buffer (new storage each time: the draws before keep reading the old data).
		virtual void UploadStream(BufferHandle buffer, size_t bytes, const void* data) = 0;

		virtual void BindVertexLayout(VertexLayoutHandle layout) = 0;
		virtual void SetVertexBuffer(VertexLayoutHandle layout, uint32_t binding, BufferHandle buffer, size_t offset, uint32_t stride) = 0;
		virtual void Draw(Topology topology, uint32_t first, uint32_t count) = 0;
		virtual void DrawInstanced(Topology topology, uint32_t first, uint32_t count, uint32_t instances) = 0;
		virtual void DrawIndexed(uint32_t firstIndex, uint32_t count) = 0;
		virtual void DrawIndexedInstanced(uint32_t count, uint32_t instances) = 0;

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

		virtual BufferHandle CreateBuffer(size_t bytes, const void* data, bool dynamic) = 0;   // fixed size; `dynamic`: UpdateBuffer allowed
		virtual BufferHandle CreateStreamBuffer() = 0;                                         // filled by CommandList::UploadStream
		virtual void UpdateBuffer(BufferHandle buffer, size_t offset, size_t bytes, const void* data) = 0;
		virtual void DestroyBuffer(BufferHandle buffer) = 0;

		virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
		virtual void SetSampler(TextureHandle texture, const SamplerDesc& sampler) = 0;
		virtual void UploadTexture(TextureHandle texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, PixelData layout, const void* data) = 0;   // level 0
		virtual void GenerateMips(TextureHandle texture) = 0;
		// Reads a block of level 0 into `out` (`bytes` long).
		virtual void ReadTexture(TextureHandle texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, PixelData layout, size_t bytes, void* out) = 0;
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
	Device& GetDevice();
	void SetDevice(Device* device);
	// Defined by the GL backend (RHI/GL/GLDevice.cpp).
	std::unique_ptr<Device> CreateGLDevice();
}
