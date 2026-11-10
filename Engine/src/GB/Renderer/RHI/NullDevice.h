#pragma once
// The null backend of the RHI (T25): accepts every call, draws nothing and keeps a log of the commands, so the render frontend (draw list,
// order, culling, pass structure) runs and is tested on the CPU without a GL context. Resources are ids that are always valid: programs
// "compile", render targets are complete, timestamps are ready at 0. It counts like the GL backend (RHI/Counters.h) but never filters
// (RHI::Tuning is ignored): the log keeps every command.
//
//   GB::RHI::NullDevice device;
//   GB::RHI::SetDevice(&device);   // before creating any resource; SetDevice(nullptr) at the end
#include "GB/Renderer/RHI/RHI.h"
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace GB::RHI
{
	enum class NullOp : uint8_t
	{
		BeginPass, EndPass, SetDrawAttachments, ClearColorAttachment, BindPipeline, SetCull, SetAlphaToCoverage, SetDepthWrite, SetBlend,
		SetBlendColor, SetScissor, SetUniform, BindTexture, BindStorageBuffer, UploadStream, BindVertexLayout, SetVertexBuffer, Draw,
		DrawInstanced, DrawIndexed, DrawIndexedInstanced, BlitColor, BlitDepth, WriteTimestamp, Flush,
		BindUniformBuffer, BindStorageBufferRange   // T71
	};

	// One recorded command. The meaning of the fields depends on the op: A is the main handle (target, pipeline, program, texture, buffer,
	// layout, query), B and C counts or slots (unit, binding, first, count, instances), Location the uniform's location, Name the pass's.
	struct NullCommand
	{
		NullOp Op = NullOp::Flush;
		uint32_t A = 0, B = 0, C = 0;
		int Location = -1;
		std::string Name;
		std::vector<float> Values;   // SetUniform: the floats (an int is stored as one float); UploadStream: unused
		size_t Bytes = 0;            // UploadStream; BindUniformBuffer and BindStorageBufferRange: the range's size
		size_t Offset = 0;           // BindUniformBuffer and BindStorageBufferRange: the range's offset
		// DrawIndexedInstanced (B: count, C: instances, as before T71):
		uint32_t FirstIndex = 0, BaseInstance = 0;
		int32_t BaseVertex = 0;
	};

	class NullCommandList final : public CommandList
	{
	public:
		explicit NullCommandList(class NullDevice& device) : m_Device(device) {}

		std::vector<NullCommand> Log;

		void BeginPass(const PassDesc& pass) override;
		void EndPass() override { Push(NullOp::EndPass); }
		void SetDrawAttachments(RenderTargetHandle target, uint32_t count) override { Push(NullOp::SetDrawAttachments, target.Id, count); }
		void ClearColorAttachment(RenderTargetHandle target, uint32_t attachment, const glm::vec4&) override { Push(NullOp::ClearColorAttachment, target.Id, attachment); }

		void BindPipeline(PipelineHandle pipeline) override { Push(NullOp::BindPipeline, pipeline.Id); }
		void SetCull(Cull cull) override { Push(NullOp::SetCull, static_cast<uint32_t>(cull)); }
		void SetAlphaToCoverage(bool on) override { Push(NullOp::SetAlphaToCoverage, on ? 1u : 0u); }
		void SetDepthWrite(bool on) override { Push(NullOp::SetDepthWrite, on ? 1u : 0u); }
		void SetBlend(const BlendState& blend) override { Push(NullOp::SetBlend, blend.Enabled ? 1u : 0u); }
		void SetBlendColor(const glm::vec4&) override { Push(NullOp::SetBlendColor); }
		void SetScissor(const std::optional<glm::ivec4>& rect) override { Push(NullOp::SetScissor, rect ? 1u : 0u); }

		void SetUniformInt(ProgramHandle program, int location, int value) override;
		void SetUniformFloats(ProgramHandle program, int location, uint32_t components, uint32_t count, const float* values) override;
		void SetUniformMat4(ProgramHandle program, int location, const glm::mat4& value) override;

		void BindTexture(uint32_t unit, TextureHandle texture) override { Push(NullOp::BindTexture, texture.Id, unit); }
		void BindStorageBuffer(uint32_t slot, BufferHandle buffer) override { Push(NullOp::BindStorageBuffer, buffer.Id, slot); }
		void BindUniformBuffer(uint32_t slot, BufferHandle buffer, size_t offset, size_t bytes) override;
		void BindStorageBufferRange(uint32_t slot, BufferHandle buffer, size_t offset, size_t bytes) override;
		void UploadStream(BufferHandle buffer, size_t bytes, const void*) override;

		void BindVertexLayout(VertexLayoutHandle layout) override { Push(NullOp::BindVertexLayout, layout.Id); }
		void SetVertexBuffer(VertexLayoutHandle layout, uint32_t binding, BufferHandle buffer, size_t, uint32_t) override { Push(NullOp::SetVertexBuffer, layout.Id, binding, buffer.Id); }
		void Draw(Topology, uint32_t first, uint32_t count) override { Push(NullOp::Draw, 0, first, count); }
		void DrawInstanced(Topology, uint32_t, uint32_t count, uint32_t instances) override { Push(NullOp::DrawInstanced, 0, count, instances); }
		void DrawIndexed(uint32_t firstIndex, uint32_t count) override { Push(NullOp::DrawIndexed, 0, firstIndex, count); }
		using CommandList::DrawIndexedInstanced;
		void DrawIndexedInstanced(const IndexedDraw& draw) override;

		void BlitColor(const RenderTarget& src, uint32_t, uint32_t, const RenderTarget& dst, uint32_t, uint32_t, uint32_t attachment, BlitFilter) override
		{
			Push(NullOp::BlitColor, src.Handle.Id, dst.Handle.Id, attachment);
		}
		void BlitDepth(const RenderTarget& src, uint32_t, uint32_t, const RenderTarget& dst, uint32_t, uint32_t) override { Push(NullOp::BlitDepth, src.Handle.Id, dst.Handle.Id); }

		void WriteTimestamp(QueryHandle query) override { Push(NullOp::WriteTimestamp, query.Id); }
		void Flush() override { Push(NullOp::Flush); }

	private:
		NullCommand& Push(NullOp op, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0);
		NullDevice& m_Device;
	};

	// The null upload ring (T71): CPU memory, so Allocate hands out real pointers; it counts the waits and fences a GL ring would do.
	class NullUploadRing final : public UploadRing
	{
	public:
		NullUploadRing(BufferHandle buffer, std::unique_ptr<std::byte[]> memory, size_t regionBytes, uint32_t regions, size_t defaultAlign)
			: UploadRing(buffer, memory.get(), regionBytes, regions, defaultAlign), m_Memory(std::move(memory)) {}
		std::vector<uint32_t> Waits;    // the region waited for, at each BeginFrame
		std::vector<uint32_t> Fences;   // the region fenced, at each EndFrame

	private:
		void WaitRegion(uint32_t region) override { Waits.push_back(region); }
		void FenceRegion(uint32_t region) override { Fences.push_back(region); }
		std::unique_ptr<std::byte[]> m_Memory;
	};

	class NullDevice final : public Device
	{
	public:
		NullDevice() = default;
		NullDevice(const NullDevice&) = delete;              // (its command list points back to it)
		NullDevice& operator=(const NullDevice&) = delete;
		NullCommandList List{ *this };

		// The limits it reports (T71): a test sets other alignments before creating a ring.
		DeviceLimits Caps;
		// Calls that broke the contract of the interface (T71: a compressed upload with the wrong size or to an uncompressed texture, a layer
		// past the texture's, an offset off the alignment); each is also logged as an error.
		uint32_t Misuses = 0;

		const char* Name() const override { return "Null"; }
		CommandList& Commands() override { return List; }
		const DeviceLimits& Limits() const override { return Caps; }

		BufferHandle CreateBuffer(size_t bytes, const void* data, bool) override { if (data) Counters::Frame.UploadBytes += bytes; return { Next() }; }
		BufferHandle CreateStreamBuffer() override { return { Next() }; }
		void UpdateBuffer(BufferHandle, size_t, size_t bytes, const void*) override { Counters::Frame.UploadBytes += bytes; }
		void DestroyBuffer(BufferHandle) override {}
		// A ring over CPU memory (no fences: the null device has no GPU to wait for); it counts its frames' waits and fences for the tests.
		std::unique_ptr<UploadRing> CreateUploadRing(size_t regionBytes, uint32_t regions) override;
		using Device::CreateUploadRing;

		TextureHandle CreateTexture(const TextureDesc& desc) override;
		const TextureDesc* GetTexture(TextureHandle texture) const;   // what it was created with (null once destroyed)
		void UploadTextureLayer(TextureHandle texture, uint32_t level, uint32_t layer, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
			PixelData layout, const void* data) override;
		void UploadCompressed(TextureHandle texture, uint32_t level, uint32_t layer, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
			size_t bytes, const void* data) override;
		void ReadTextureLayer(TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, PixelData, size_t bytes, void* out) override;
		void ReadCompressed(TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, size_t bytes, void* out) override;
		void SetSampler(TextureHandle, const SamplerDesc&) override {}
		void UploadTexture(TextureHandle, uint32_t, uint32_t, uint32_t width, uint32_t height, PixelData layout, const void*) override
		{
			Counters::Frame.UploadBytes += static_cast<uint64_t>(width) * height * (layout == PixelData::RGBFloat ? 12u : 4u);
		}
		void GenerateMips(TextureHandle) override {}
		void ReadTexture(TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, PixelData, size_t bytes, void* out) override;
		void DestroyTexture(TextureHandle texture) override { m_Textures.erase(texture.Id); }

		RenderTarget CreateRenderTarget(const RenderTargetDesc& desc) override;
		void DestroyRenderTarget(RenderTarget& target) override { target = {}; }
		void ReadPixels(RenderTargetHandle, uint32_t width, uint32_t height, uint8_t* out) override;

		ProgramHandle CreateProgram(const std::string&, const std::string&) override { return { Next() }; }
		int GetUniformLocation(ProgramHandle program, const std::string& name) override;
		void DestroyProgram(ProgramHandle) override {}
		// The name of a uniform location handed out by GetUniformLocation ("" if unknown): the tests read the log by name.
		const std::string& UniformName(ProgramHandle program, int location) const;

		VertexLayoutHandle CreateVertexLayout(const VertexLayoutDesc&) override { return { Next() }; }
		void DestroyVertexLayout(VertexLayoutHandle) override {}

		PipelineHandle CreatePipeline(const PipelineDesc& desc) override;
		void DestroyPipeline(PipelineHandle) override {}
		const PipelineDesc* GetPipeline(PipelineHandle pipeline) const;

		QueryHandle CreateTimestamp() override { return { Next() }; }
		void DestroyTimestamp(QueryHandle) override {}
		bool TimestampReady(QueryHandle) override { return true; }
		uint64_t TimestampNs(QueryHandle) override { return 0; }

		// Says whether a texture region / range call keeps the contract; if not, counts and logs it.
		bool Expect(bool ok, const char* what);

	private:
		uint32_t Next() { return ++m_NextId; }
		uint32_t m_NextId = 0;
		std::vector<PipelineDesc> m_Pipelines;
		std::unordered_map<uint32_t, TextureDesc> m_Textures;
		std::unordered_map<uint64_t, std::string> m_UniformNames;      // (program << 32 | location) -> name
		std::unordered_map<uint32_t, std::unordered_map<std::string, int>> m_Locations;
	};
}
