#include "NullDevice.h"
#include "GB/Log.h"
#include <algorithm>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>

namespace GB::RHI
{
	NullCommand& NullCommandList::Push(NullOp op, uint32_t a, uint32_t b, uint32_t c)
	{
		// The counters of the frame (T70), as the GL backend with its filters off: every command reaches the "API".
		FrameCounters& n = Counters::Frame;
		switch (op)
		{
		case NullOp::BindPipeline: ++n.PipelineBinds; break;
		case NullOp::SetCull: case NullOp::SetAlphaToCoverage: case NullOp::SetBlend: ++n.StateCalls; ++n.StateChanges; break;
		case NullOp::SetUniform: ++n.UniformCalls; ++n.UniformsSent; break;
		case NullOp::BindTexture: ++n.TextureBinds; break;
		case NullOp::BindStorageBuffer: case NullOp::SetVertexBuffer: ++n.BufferBinds; break;
		case NullOp::BindVertexLayout: ++n.LayoutBinds; break;
		default: break;
		}
		NullCommand& cmd = Log.emplace_back();
		cmd.Op = op;
		cmd.A = a;
		cmd.B = b;
		cmd.C = c;
		return cmd;
	}

	void NullCommandList::BeginPass(const PassDesc& pass)
	{
		NullCommand& cmd = Push(NullOp::BeginPass, pass.Target.Id, pass.Width, pass.Height);
		cmd.Name = pass.Name ? pass.Name : "";
	}

	void NullCommandList::SetUniformInt(ProgramHandle program, int location, int value)
	{
		NullCommand& cmd = Push(NullOp::SetUniform, program.Id, 1, 1);
		cmd.Location = location;
		cmd.Values.push_back(static_cast<float>(value));
	}

	void NullCommandList::SetUniformFloats(ProgramHandle program, int location, uint32_t components, uint32_t count, const float* values)
	{
		NullCommand& cmd = Push(NullOp::SetUniform, program.Id, components, count);
		cmd.Location = location;
		cmd.Values.assign(values, values + static_cast<size_t>(components) * count);
	}

	void NullCommandList::SetUniformMat4(ProgramHandle program, int location, const glm::mat4& value)
	{
		NullCommand& cmd = Push(NullOp::SetUniform, program.Id, 16, 1);
		cmd.Location = location;
		const float* p = glm::value_ptr(value);
		cmd.Values.assign(p, p + 16);
	}

	void NullCommandList::UploadStream(BufferHandle buffer, size_t bytes, const void*)
	{
		NullCommand& cmd = Push(NullOp::UploadStream, buffer.Id);
		cmd.Bytes = bytes;
		Counters::Frame.UploadBytes += bytes;
	}

	void NullDevice::ReadTexture(TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, PixelData, size_t bytes, void* out)
	{
		if (out) std::memset(out, 0, bytes);
	}

	RenderTarget NullDevice::CreateRenderTarget(const RenderTargetDesc& desc)
	{
		RenderTarget rt;
		rt.Handle = { Next() };
		if (desc.Color != Format::None) rt.Color = { Next() };
		if (desc.Color != Format::None && desc.Extra != Format::None) rt.Extra = { Next() };
		if (desc.Depth && desc.DepthAsTexture) rt.DepthTexture = { Next() };
		rt.Complete = true;
		return rt;
	}

	void NullDevice::ReadPixels(RenderTargetHandle, uint32_t width, uint32_t height, uint8_t* out)
	{
		if (out) std::memset(out, 0, static_cast<size_t>(width) * height * 4);
	}

	int NullDevice::GetUniformLocation(ProgramHandle program, const std::string& name)
	{
		std::unordered_map<std::string, int>& names = m_Locations[program.Id];
		const auto found = names.find(name);
		if (found != names.end())
			return found->second;
		const int location = static_cast<int>(names.size());
		names.emplace(name, location);
		m_UniformNames[(static_cast<uint64_t>(program.Id) << 32) | static_cast<uint32_t>(location)] = name;
		return location;
	}

	const std::string& NullDevice::UniformName(ProgramHandle program, int location) const
	{
		static const std::string none;
		if (location < 0) return none;
		const auto found = m_UniformNames.find((static_cast<uint64_t>(program.Id) << 32) | static_cast<uint32_t>(location));
		return found != m_UniformNames.end() ? found->second : none;
	}

	PipelineHandle NullDevice::CreatePipeline(const PipelineDesc& desc)
	{
		m_Pipelines.push_back(desc);
		return { static_cast<uint32_t>(m_Pipelines.size()) };
	}

	const PipelineDesc* NullDevice::GetPipeline(PipelineHandle pipeline) const
	{
		if (!pipeline || pipeline.Id > m_Pipelines.size())
			return nullptr;
		return &m_Pipelines[pipeline.Id - 1];
	}

	// ---- T71 ----------------------------------------------------------------------------------------------------------------------------

	bool NullDevice::Expect(bool ok, const char* what)
	{
		if (!ok)
		{
			++Misuses;
			GB_CORE_ERROR("RHI (null): {}", what);
		}
		return ok;
	}

	void NullCommandList::BindUniformBuffer(uint32_t slot, BufferHandle buffer, size_t offset, size_t bytes)
	{
		++Counters::Frame.UniformBufferBinds;
		m_Device.Expect(offset % std::max(m_Device.Caps.UniformBufferAlignment, 1u) == 0, "BindUniformBuffer: offset off the alignment");
		NullCommand& cmd = Push(NullOp::BindUniformBuffer, buffer.Id, slot);
		cmd.Offset = offset;
		cmd.Bytes = bytes;
	}

	void NullCommandList::BindStorageBufferRange(uint32_t slot, BufferHandle buffer, size_t offset, size_t bytes)
	{
		++Counters::Frame.StorageRangeBinds;
		m_Device.Expect(offset % std::max(m_Device.Caps.StorageBufferAlignment, 1u) == 0, "BindStorageBufferRange: offset off the alignment");
		NullCommand& cmd = Push(NullOp::BindStorageBufferRange, buffer.Id, slot);
		cmd.Offset = offset;
		cmd.Bytes = bytes;
	}

	void NullCommandList::DrawIndexedInstanced(const IndexedDraw& draw)
	{
		NullCommand& cmd = Push(NullOp::DrawIndexedInstanced, 0, draw.Count, draw.Instances);
		cmd.FirstIndex = draw.FirstIndex;
		cmd.BaseVertex = draw.BaseVertex;
		cmd.BaseInstance = draw.BaseInstance;
	}

	std::unique_ptr<UploadRing> NullDevice::CreateUploadRing(size_t regionBytes, uint32_t regions)
	{
		regions = std::max(regions, 1u);
		const size_t align = std::max(Caps.UniformBufferAlignment, Caps.StorageBufferAlignment);
		return std::make_unique<NullUploadRing>(BufferHandle{ Next() }, std::make_unique<std::byte[]>(regionBytes * regions), regionBytes, regions, align);
	}

	TextureHandle NullDevice::CreateTexture(const TextureDesc& desc)
	{
		const TextureHandle texture{ Next() };
		Expect(!desc.IsArray() || desc.Samples <= 1, "CreateTexture: a multisampled array");
		Expect(!desc.IsArray() || desc.Layers <= Caps.MaxArrayLayers, "CreateTexture: more layers than the device allows");
		m_Textures.emplace(texture.Id, desc);
		return texture;
	}

	const TextureDesc* NullDevice::GetTexture(TextureHandle texture) const
	{
		const auto found = m_Textures.find(texture.Id);
		return found != m_Textures.end() ? &found->second : nullptr;
	}

	void NullDevice::UploadTextureLayer(TextureHandle texture, uint32_t level, uint32_t layer, uint32_t, uint32_t, uint32_t width, uint32_t height,
		PixelData layout, const void*)
	{
		Counters::Frame.UploadBytes += static_cast<uint64_t>(width) * height * (layout == PixelData::RGBFloat ? 12u : 4u);
		const TextureDesc* desc = GetTexture(texture);
		static_cast<void>(Expect(desc != nullptr, "UploadTextureLayer: no such texture")
			&& Expect(!IsCompressed(desc->Fmt), "UploadTextureLayer: a compressed texture (UploadCompressed)")
			&& Expect(level < desc->Levels && layer < (desc->IsArray() ? desc->Layers : 1u), "UploadTextureLayer: level or layer out of range"));
	}

	void NullDevice::UploadCompressed(TextureHandle texture, uint32_t level, uint32_t layer, uint32_t, uint32_t, uint32_t width, uint32_t height,
		size_t bytes, const void*)
	{
		Counters::Frame.CompressedBytes += bytes;
		const TextureDesc* desc = GetTexture(texture);
		// (one misuse per call: the first broken rule)
		static_cast<void>(Expect(desc != nullptr, "UploadCompressed: no such texture")
			&& Expect(IsCompressed(desc->Fmt), "UploadCompressed: an uncompressed texture")
			&& Expect(bytes == CompressedBytes(desc->Fmt, width, height), "UploadCompressed: the bytes are not the blocks of the region")
			&& Expect(level < desc->Levels && layer < (desc->IsArray() ? desc->Layers : 1u), "UploadCompressed: level or layer out of range"));
	}

	void NullDevice::ReadTextureLayer(TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, PixelData, size_t bytes, void* out)
	{
		if (out) std::memset(out, 0, bytes);
	}

	void NullDevice::ReadCompressed(TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, size_t bytes, void* out)
	{
		if (out) std::memset(out, 0, bytes);
	}
}
