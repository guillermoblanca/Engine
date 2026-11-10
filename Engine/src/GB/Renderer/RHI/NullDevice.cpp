#include "NullDevice.h"
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
}
