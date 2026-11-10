#pragma once
// The OpenGL 4.5 backend of the RHI (T25). GL is an immediate API: the command list runs each command as it is recorded, which keeps the
// order (and the image) the renderer had when it called GL itself. Handles are the GL object names. Redundant state is not filtered: other
// code (ImGui, the editor's tools) also changes GL state between frames, so the backend cannot trust a cache of it.
#include "GB/Renderer/RHI/RHI.h"
#include <unordered_map>
#include <vector>

namespace GB::RHI
{
	class GLCommandList final : public CommandList
	{
	public:
		explicit GLCommandList(class GLDevice& device) : m_Device(device) {}

		void BeginPass(const PassDesc& pass) override;
		void EndPass() override {}
		void SetDrawAttachments(RenderTargetHandle target, uint32_t count) override;
		void ClearColorAttachment(RenderTargetHandle target, uint32_t attachment, const glm::vec4& value) override;

		void BindPipeline(PipelineHandle pipeline) override;
		void SetCull(Cull cull) override;
		void SetAlphaToCoverage(bool on) override;
		void SetDepthWrite(bool on) override;
		void SetBlend(const BlendState& blend) override;
		void SetBlendColor(const glm::vec4& color) override;
		void SetScissor(const std::optional<glm::ivec4>& rect) override;

		void SetUniformInt(ProgramHandle program, int location, int value) override;
		void SetUniformFloats(ProgramHandle program, int location, uint32_t components, uint32_t count, const float* values) override;
		void SetUniformMat4(ProgramHandle program, int location, const glm::mat4& value) override;

		void BindTexture(uint32_t unit, TextureHandle texture) override;
		void BindStorageBuffer(uint32_t slot, BufferHandle buffer) override;
		void UploadStream(BufferHandle buffer, size_t bytes, const void* data) override;

		void BindVertexLayout(VertexLayoutHandle layout) override;
		void SetVertexBuffer(VertexLayoutHandle layout, uint32_t binding, BufferHandle buffer, size_t offset, uint32_t stride) override;
		void Draw(Topology topology, uint32_t first, uint32_t count) override;
		void DrawInstanced(Topology topology, uint32_t first, uint32_t count, uint32_t instances) override;
		void DrawIndexed(uint32_t firstIndex, uint32_t count) override;
		void DrawIndexedInstanced(uint32_t count, uint32_t instances) override;

		void BlitColor(const RenderTarget& src, uint32_t srcW, uint32_t srcH, const RenderTarget& dst, uint32_t dstW, uint32_t dstH,
			uint32_t attachment, BlitFilter filter) override;
		void BlitDepth(const RenderTarget& src, uint32_t srcW, uint32_t srcH, const RenderTarget& dst, uint32_t dstW, uint32_t dstH) override;

		void WriteTimestamp(QueryHandle query) override;
		void Flush() override;

	private:
		GLDevice& m_Device;
	};

	class GLDevice final : public Device
	{
	public:
		GLDevice() : m_Commands(*this) {}
		const char* Name() const override { return "GL"; }
		CommandList& Commands() override { return m_Commands; }

		BufferHandle CreateBuffer(size_t bytes, const void* data, bool dynamic) override;
		BufferHandle CreateStreamBuffer() override;
		void UpdateBuffer(BufferHandle buffer, size_t offset, size_t bytes, const void* data) override;
		void DestroyBuffer(BufferHandle buffer) override;

		TextureHandle CreateTexture(const TextureDesc& desc) override;
		void SetSampler(TextureHandle texture, const SamplerDesc& sampler) override;
		void UploadTexture(TextureHandle texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, PixelData layout, const void* data) override;
		void GenerateMips(TextureHandle texture) override;
		void ReadTexture(TextureHandle texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, PixelData layout, size_t bytes, void* out) override;
		void DestroyTexture(TextureHandle texture) override;

		RenderTarget CreateRenderTarget(const RenderTargetDesc& desc) override;
		void DestroyRenderTarget(RenderTarget& target) override;
		void ReadPixels(RenderTargetHandle target, uint32_t width, uint32_t height, uint8_t* out) override;

		ProgramHandle CreateProgram(const std::string& vertexSource, const std::string& fragmentSource) override;
		int GetUniformLocation(ProgramHandle program, const std::string& name) override;
		void DestroyProgram(ProgramHandle program) override;

		VertexLayoutHandle CreateVertexLayout(const VertexLayoutDesc& desc) override;
		void DestroyVertexLayout(VertexLayoutHandle layout) override;

		PipelineHandle CreatePipeline(const PipelineDesc& desc) override;
		void DestroyPipeline(PipelineHandle pipeline) override;
		const PipelineDesc* GetPipeline(PipelineHandle pipeline) const;

		QueryHandle CreateTimestamp() override;
		void DestroyTimestamp(QueryHandle query) override;
		bool TimestampReady(QueryHandle query) override;
		uint64_t TimestampNs(QueryHandle query) override;

	private:
		GLCommandList m_Commands;
		std::unordered_map<uint32_t, uint32_t> m_DepthBuffers;   // render target -> its depth renderbuffer
		std::vector<std::optional<PipelineDesc>> m_Pipelines;    // by id - 1 (empty once destroyed, reused by the next one)
	};
}
