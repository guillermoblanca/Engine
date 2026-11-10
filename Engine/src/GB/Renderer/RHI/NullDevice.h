#pragma once
// The null backend of the RHI (T25): accepts every call, draws nothing and keeps a log of the commands, so the render frontend (draw list,
// order, culling, pass structure) runs and is tested on the CPU without a GL context. Resources are ids that are always valid: programs
// "compile", render targets are complete, timestamps are ready at 0. It counts like the GL backend (RHI/Counters.h) but never filters
// (RHI::Tuning is ignored): the log keeps every command.
//
//   GB::RHI::NullDevice device;
//   GB::RHI::SetDevice(&device);   // before creating any resource; SetDevice(nullptr) at the end
#include "GB/Renderer/RHI/RHI.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace GB::RHI
{
	enum class NullOp : uint8_t
	{
		BeginPass, EndPass, SetDrawAttachments, ClearColorAttachment, BindPipeline, SetCull, SetAlphaToCoverage, SetDepthWrite, SetBlend,
		SetBlendColor, SetScissor, SetUniform, BindTexture, BindStorageBuffer, UploadStream, BindVertexLayout, SetVertexBuffer, Draw,
		DrawInstanced, DrawIndexed, DrawIndexedInstanced, BlitColor, BlitDepth, WriteTimestamp, Flush
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
		size_t Bytes = 0;            // UploadStream
	};

	class NullCommandList final : public CommandList
	{
	public:
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
		void UploadStream(BufferHandle buffer, size_t bytes, const void*) override;

		void BindVertexLayout(VertexLayoutHandle layout) override { Push(NullOp::BindVertexLayout, layout.Id); }
		void SetVertexBuffer(VertexLayoutHandle layout, uint32_t binding, BufferHandle buffer, size_t, uint32_t) override { Push(NullOp::SetVertexBuffer, layout.Id, binding, buffer.Id); }
		void Draw(Topology, uint32_t first, uint32_t count) override { Push(NullOp::Draw, 0, first, count); }
		void DrawInstanced(Topology, uint32_t, uint32_t count, uint32_t instances) override { Push(NullOp::DrawInstanced, 0, count, instances); }
		void DrawIndexed(uint32_t firstIndex, uint32_t count) override { Push(NullOp::DrawIndexed, 0, firstIndex, count); }
		void DrawIndexedInstanced(uint32_t count, uint32_t instances) override { Push(NullOp::DrawIndexedInstanced, 0, count, instances); }

		void BlitColor(const RenderTarget& src, uint32_t, uint32_t, const RenderTarget& dst, uint32_t, uint32_t, uint32_t attachment, BlitFilter) override
		{
			Push(NullOp::BlitColor, src.Handle.Id, dst.Handle.Id, attachment);
		}
		void BlitDepth(const RenderTarget& src, uint32_t, uint32_t, const RenderTarget& dst, uint32_t, uint32_t) override { Push(NullOp::BlitDepth, src.Handle.Id, dst.Handle.Id); }

		void WriteTimestamp(QueryHandle query) override { Push(NullOp::WriteTimestamp, query.Id); }
		void Flush() override { Push(NullOp::Flush); }

	private:
		NullCommand& Push(NullOp op, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0);
	};

	class NullDevice final : public Device
	{
	public:
		NullCommandList List;

		const char* Name() const override { return "Null"; }
		CommandList& Commands() override { return List; }

		BufferHandle CreateBuffer(size_t bytes, const void* data, bool) override { if (data) Counters::Frame.UploadBytes += bytes; return { Next() }; }
		BufferHandle CreateStreamBuffer() override { return { Next() }; }
		void UpdateBuffer(BufferHandle, size_t, size_t bytes, const void*) override { Counters::Frame.UploadBytes += bytes; }
		void DestroyBuffer(BufferHandle) override {}

		TextureHandle CreateTexture(const TextureDesc&) override { return { Next() }; }
		void SetSampler(TextureHandle, const SamplerDesc&) override {}
		void UploadTexture(TextureHandle, uint32_t, uint32_t, uint32_t width, uint32_t height, PixelData layout, const void*) override
		{
			Counters::Frame.UploadBytes += static_cast<uint64_t>(width) * height * (layout == PixelData::RGBFloat ? 12u : 4u);
		}
		void GenerateMips(TextureHandle) override {}
		void ReadTexture(TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, PixelData, size_t bytes, void* out) override;
		void DestroyTexture(TextureHandle) override {}

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

	private:
		uint32_t Next() { return ++m_NextId; }
		uint32_t m_NextId = 0;
		std::vector<PipelineDesc> m_Pipelines;
		std::unordered_map<uint64_t, std::string> m_UniformNames;      // (program << 32 | location) -> name
		std::unordered_map<uint32_t, std::unordered_map<std::string, int>> m_Locations;
	};
}
