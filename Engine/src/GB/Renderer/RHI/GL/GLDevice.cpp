#include "GLDevice.h"
#include "GB/Log.h"
#include <algorithm>
#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

namespace GB::RHI
{
	std::unique_ptr<Device> CreateGLDevice() { return std::make_unique<GLDevice>(); }

	namespace
	{
		GLenum InternalFormat(Format format)
		{
			switch (format)
			{
			case Format::RGBA8: return GL_RGBA8;
			case Format::SRGB8A8: return GL_SRGB8_ALPHA8;
			case Format::RGBA16F: return GL_RGBA16F;
			case Format::RGB16F: return GL_RGB16F;
			case Format::R11G11B10F: return GL_R11F_G11F_B10F;
			case Format::Depth24: return GL_DEPTH_COMPONENT24;
			case Format::None: break;
			}
			return GL_RGBA8;
		}
		void PixelFormat(PixelData layout, GLenum& format, GLenum& type)
		{
			switch (layout)
			{
			case PixelData::RGBA8: format = GL_RGBA; type = GL_UNSIGNED_BYTE; return;
			case PixelData::RGBFloat: format = GL_RGB; type = GL_FLOAT; return;
			case PixelData::RedFloat: format = GL_RED; type = GL_FLOAT; return;
			}
		}
		GLint FilterOf(Filter f) { return f == Filter::Nearest ? GL_NEAREST : f == Filter::Linear ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR; }
		GLint WrapOf(Wrap w) { return w == Wrap::Repeat ? GL_REPEAT : w == Wrap::ClampToEdge ? GL_CLAMP_TO_EDGE : GL_CLAMP_TO_BORDER; }
		GLenum TopologyOf(Topology t) { return t == Topology::Triangles ? GL_TRIANGLES : GL_TRIANGLE_STRIP; }
		GLenum FactorOf(BlendFactor f)
		{
			switch (f)
			{
			case BlendFactor::Zero: return GL_ZERO;
			case BlendFactor::One: return GL_ONE;
			case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
			case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
			case BlendFactor::ConstantColor: return GL_CONSTANT_COLOR;
			}
			return GL_ONE;
		}
		void Toggle(GLenum cap, bool on) { if (on) glEnable(cap); else glDisable(cap); }

		GLuint Compile(GLenum type, const std::string& source)
		{
			const GLuint shader = glCreateShader(type);
			const char* src = source.c_str();
			glShaderSource(shader, 1, &src, nullptr);
			glCompileShader(shader);

			GLint ok = GL_FALSE;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
			if (!ok)
			{
				GLint len = 0;
				glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
				std::vector<char> log(len > 0 ? static_cast<size_t>(len) : 1);
				glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
				GB_CORE_ERROR("Shader compile error:\n{}", log.data());
				glDeleteShader(shader);
				return 0;
			}
			return shader;
		}
	}

	// ---- Command list -------------------------------------------------------------------------------------------------------------------

	void GLCommandList::BeginPass(const PassDesc& pass)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, pass.Target.Id);
		glViewport(0, 0, static_cast<GLsizei>(pass.Width), static_cast<GLsizei>(pass.Height));
		GLbitfield bits = 0;
		if (pass.ClearColor)
		{
			glClearColor(pass.ClearColor->r, pass.ClearColor->g, pass.ClearColor->b, pass.ClearColor->a);
			bits |= GL_COLOR_BUFFER_BIT;
		}
		if (pass.ClearDepth)
		{
			glDepthMask(GL_TRUE);
			bits |= GL_DEPTH_BUFFER_BIT;
		}
		if (bits)
			glClear(bits);
	}

	void GLCommandList::SetDrawAttachments(RenderTargetHandle target, uint32_t count)
	{
		const GLenum both[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		glNamedFramebufferDrawBuffers(target.Id, static_cast<GLsizei>(std::min(count, 2u)), both);
	}

	void GLCommandList::ClearColorAttachment(RenderTargetHandle target, uint32_t attachment, const glm::vec4& value)
	{
		glClearNamedFramebufferfv(target.Id, GL_COLOR, static_cast<GLint>(attachment), glm::value_ptr(value));
	}

	void GLCommandList::BindPipeline(PipelineHandle pipeline)
	{
		const PipelineDesc* p = m_Device.GetPipeline(pipeline);
		if (!p)
			return;
		if (p->Program)
			glUseProgram(p->Program.Id);
		if (p->Multisample) Toggle(GL_MULTISAMPLE, *p->Multisample);
		if (p->DepthTest) Toggle(GL_DEPTH_TEST, *p->DepthTest);
		if (p->DepthWrite) glDepthMask(*p->DepthWrite ? GL_TRUE : GL_FALSE);
		if (p->CullMode) SetCull(*p->CullMode);
		if (p->Scissor) Toggle(GL_SCISSOR_TEST, *p->Scissor);
		if (p->Blend) SetBlend(*p->Blend);
		if (p->AlphaToCoverage) Toggle(GL_SAMPLE_ALPHA_TO_COVERAGE, *p->AlphaToCoverage);
		if (p->Offset)
		{
			Toggle(GL_POLYGON_OFFSET_FILL, p->Offset->Enabled);
			if (p->Offset->Enabled)
				glPolygonOffset(p->Offset->Factor, p->Offset->Units);
		}
	}

	void GLCommandList::SetCull(Cull cull)
	{
		if (cull == Cull::None)
			glDisable(GL_CULL_FACE);
		else
		{
			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
		}
	}

	void GLCommandList::SetAlphaToCoverage(bool on) { Toggle(GL_SAMPLE_ALPHA_TO_COVERAGE, on); }
	void GLCommandList::SetDepthWrite(bool on) { glDepthMask(on ? GL_TRUE : GL_FALSE); }

	void GLCommandList::SetBlend(const BlendState& blend)
	{
		Toggle(GL_BLEND, blend.Enabled);
		if (blend.Op)
			glBlendEquation(*blend.Op == BlendOp::Max ? GL_MAX : GL_FUNC_ADD);
		if (blend.Enabled)
			glBlendFunc(FactorOf(blend.Src), FactorOf(blend.Dst));
	}

	void GLCommandList::SetBlendColor(const glm::vec4& c) { glBlendColor(c.r, c.g, c.b, c.a); }
	void GLCommandList::SetScissor(const std::optional<glm::ivec4>& rect)
	{
		if (!rect)
		{
			glDisable(GL_SCISSOR_TEST);
			return;
		}
		glEnable(GL_SCISSOR_TEST);
		glScissor(rect->x, rect->y, rect->z, rect->w);
	}

	void GLCommandList::SetUniformInt(ProgramHandle program, int location, int value) { glProgramUniform1i(program.Id, location, value); }

	void GLCommandList::SetUniformFloats(ProgramHandle program, int location, uint32_t components, uint32_t count, const float* values)
	{
		const GLsizei n = static_cast<GLsizei>(count);
		switch (components)
		{
		case 1: glProgramUniform1fv(program.Id, location, n, values); break;
		case 2: glProgramUniform2fv(program.Id, location, n, values); break;
		case 3: glProgramUniform3fv(program.Id, location, n, values); break;
		default: glProgramUniform4fv(program.Id, location, n, values); break;
		}
	}

	void GLCommandList::SetUniformMat4(ProgramHandle program, int location, const glm::mat4& value)
	{
		glProgramUniformMatrix4fv(program.Id, location, 1, GL_FALSE, glm::value_ptr(value));
	}

	void GLCommandList::BindTexture(uint32_t unit, TextureHandle texture) { glBindTextureUnit(unit, texture.Id); }
	void GLCommandList::BindStorageBuffer(uint32_t slot, BufferHandle buffer) { glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, buffer.Id); }

	void GLCommandList::UploadStream(BufferHandle buffer, size_t bytes, const void* data)
	{
		glNamedBufferData(buffer.Id, static_cast<GLsizeiptr>(bytes), data, GL_STREAM_DRAW);
	}

	void GLCommandList::BindVertexLayout(VertexLayoutHandle layout) { glBindVertexArray(layout.Id); }

	void GLCommandList::SetVertexBuffer(VertexLayoutHandle layout, uint32_t binding, BufferHandle buffer, size_t offset, uint32_t stride)
	{
		glVertexArrayVertexBuffer(layout.Id, binding, buffer.Id, static_cast<GLintptr>(offset), static_cast<GLsizei>(stride));
	}

	void GLCommandList::Draw(Topology topology, uint32_t first, uint32_t count)
	{
		glDrawArrays(TopologyOf(topology), static_cast<GLint>(first), static_cast<GLsizei>(count));
	}

	void GLCommandList::DrawInstanced(Topology topology, uint32_t first, uint32_t count, uint32_t instances)
	{
		glDrawArraysInstanced(TopologyOf(topology), static_cast<GLint>(first), static_cast<GLsizei>(count), static_cast<GLsizei>(instances));
	}

	void GLCommandList::DrawIndexed(uint32_t firstIndex, uint32_t count)
	{
		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT,
			reinterpret_cast<const void*>(static_cast<uintptr_t>(firstIndex) * sizeof(uint32_t)));
	}

	void GLCommandList::DrawIndexedInstanced(uint32_t count, uint32_t instances)
	{
		glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(instances));
	}

	void GLCommandList::BlitColor(const RenderTarget& src, uint32_t srcW, uint32_t srcH, const RenderTarget& dst, uint32_t dstW, uint32_t dstH,
		uint32_t attachment, BlitFilter filter)
	{
		if (attachment == 1)
			glNamedFramebufferReadBuffer(src.Handle.Id, GL_COLOR_ATTACHMENT1);
		glBlitNamedFramebuffer(src.Handle.Id, dst.Handle.Id,
			0, 0, static_cast<GLint>(srcW), static_cast<GLint>(srcH),
			0, 0, static_cast<GLint>(dstW), static_cast<GLint>(dstH),
			GL_COLOR_BUFFER_BIT, filter == BlitFilter::Linear ? GL_LINEAR : GL_NEAREST);
		if (attachment == 1)
			glNamedFramebufferReadBuffer(src.Handle.Id, GL_COLOR_ATTACHMENT0);
	}

	void GLCommandList::BlitDepth(const RenderTarget& src, uint32_t srcW, uint32_t srcH, const RenderTarget& dst, uint32_t dstW, uint32_t dstH)
	{
		glBlitNamedFramebuffer(src.Handle.Id, dst.Handle.Id,
			0, 0, static_cast<GLint>(srcW), static_cast<GLint>(srcH),
			0, 0, static_cast<GLint>(dstW), static_cast<GLint>(dstH),
			GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}

	void GLCommandList::WriteTimestamp(QueryHandle query) { glQueryCounter(query.Id, GL_TIMESTAMP); }
	void GLCommandList::Flush() { glFlush(); }

	// ---- Device -------------------------------------------------------------------------------------------------------------------------

	BufferHandle GLDevice::CreateBuffer(size_t bytes, const void* data, bool dynamic)
	{
		GLuint b = 0;
		glCreateBuffers(1, &b);
		glNamedBufferStorage(b, static_cast<GLsizeiptr>(bytes), data, dynamic ? GL_DYNAMIC_STORAGE_BIT : 0);
		return { b };
	}

	BufferHandle GLDevice::CreateStreamBuffer()
	{
		GLuint b = 0;
		glCreateBuffers(1, &b);
		return { b };
	}

	void GLDevice::UpdateBuffer(BufferHandle buffer, size_t offset, size_t bytes, const void* data)
	{
		glNamedBufferSubData(buffer.Id, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes), data);
	}

	void GLDevice::DestroyBuffer(BufferHandle buffer)
	{
		if (buffer) glDeleteBuffers(1, &buffer.Id);
	}

	TextureHandle GLDevice::CreateTexture(const TextureDesc& desc)
	{
		const bool multisampled = desc.Samples > 1;
		GLuint t = 0;
		glCreateTextures(multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, 1, &t);
		const GLsizei w = static_cast<GLsizei>(desc.Width), h = static_cast<GLsizei>(desc.Height);
		if (multisampled)
			glTextureStorage2DMultisample(t, static_cast<GLsizei>(desc.Samples), InternalFormat(desc.Fmt), w, h, GL_TRUE);
		else
			glTextureStorage2D(t, static_cast<GLsizei>(std::max(desc.Levels, 1u)), InternalFormat(desc.Fmt), w, h);
		return { t };
	}

	void GLDevice::SetSampler(TextureHandle texture, const SamplerDesc& s)
	{
		const GLuint t = texture.Id;
		glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, FilterOf(s.Min));
		glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, FilterOf(s.Mag));
		glTextureParameteri(t, GL_TEXTURE_WRAP_S, WrapOf(s.WrapS));
		glTextureParameteri(t, GL_TEXTURE_WRAP_T, WrapOf(s.WrapT));
		if (s.BorderColor)
			glTextureParameterfv(t, GL_TEXTURE_BORDER_COLOR, glm::value_ptr(*s.BorderColor));
		if (s.DepthCompare)
		{
			glTextureParameteri(t, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
			glTextureParameteri(t, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
		}
		if (s.MaxAnisotropy > 1.0f && GLAD_GL_EXT_texture_filter_anisotropic)
		{
			GLfloat maxAniso = 1.0f;
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
			glTextureParameterf(t, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(s.MaxAnisotropy, maxAniso));
		}
	}

	void GLDevice::UploadTexture(TextureHandle texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, PixelData layout, const void* data)
	{
		GLenum format = GL_RGBA, type = GL_UNSIGNED_BYTE;
		PixelFormat(layout, format, type);
		glTextureSubImage2D(texture.Id, 0, static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(width), static_cast<GLsizei>(height),
			format, type, data);
	}

	void GLDevice::GenerateMips(TextureHandle texture) { glGenerateTextureMipmap(texture.Id); }

	void GLDevice::ReadTexture(TextureHandle texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, PixelData layout, size_t bytes, void* out)
	{
		GLenum format = GL_RGBA, type = GL_UNSIGNED_BYTE;
		PixelFormat(layout, format, type);
		glGetTextureSubImage(texture.Id, 0, static_cast<GLint>(x), static_cast<GLint>(y), 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 1,
			format, type, static_cast<GLsizei>(bytes), out);
	}

	void GLDevice::DestroyTexture(TextureHandle texture)
	{
		if (texture) glDeleteTextures(1, &texture.Id);
	}

	RenderTarget GLDevice::CreateRenderTarget(const RenderTargetDesc& desc)
	{
		RenderTarget rt;
		const GLsizei w = static_cast<GLsizei>(desc.Width), h = static_cast<GLsizei>(desc.Height);
		const bool multisampled = desc.Samples > 1;
		GLuint fbo = 0;
		glCreateFramebuffers(1, &fbo);
		rt.Handle = { fbo };

		// A color texture of `format`, multisampled like the target, at `attachment`.
		auto makeColor = [&](Format format, GLenum attachment)
		{
			TextureDesc td;
			td.Width = desc.Width;
			td.Height = desc.Height;
			td.Samples = desc.Samples;
			td.Fmt = format == Format::RGBA16F || format == Format::R11G11B10F ? format : Format::RGBA8;
			const TextureHandle texture = CreateTexture(td);
			if (!multisampled)
			{
				SamplerDesc s;
				s.WrapS = s.WrapT = Wrap::ClampToEdge;
				SetSampler(texture, s);
			}
			glNamedFramebufferTexture(fbo, attachment, texture.Id, 0);
			return texture;
		};
		if (desc.Color != Format::None)
		{
			rt.Color = makeColor(desc.Color, GL_COLOR_ATTACHMENT0);
			if (desc.Extra != Format::None)
			{
				rt.Extra = makeColor(desc.Extra, GL_COLOR_ATTACHMENT1);
				m_Commands.SetDrawAttachments(rt.Handle, 2);
			}
		}
		else
		{
			glNamedFramebufferDrawBuffer(fbo, GL_NONE);
			glNamedFramebufferReadBuffer(fbo, GL_NONE);
		}

		if (desc.Depth)
		{
			if (desc.DepthAsTexture)
			{
				TextureDesc td;
				td.Width = desc.Width;
				td.Height = desc.Height;
				td.Fmt = Format::Depth24;
				rt.DepthTexture = CreateTexture(td);
				SamplerDesc s;
				s.WrapS = s.WrapT = Wrap::ClampToBorder;
				s.BorderColor = glm::vec4(1.0f);
				if (desc.DepthCompare)
					s.DepthCompare = true;
				else
					s.Min = s.Mag = Filter::Nearest;
				SetSampler(rt.DepthTexture, s);
				glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, rt.DepthTexture.Id, 0);
			}
			else
			{
				GLuint rb = 0;
				glCreateRenderbuffers(1, &rb);
				if (multisampled)
					glNamedRenderbufferStorageMultisample(rb, static_cast<GLsizei>(desc.Samples), GL_DEPTH_COMPONENT24, w, h);
				else
					glNamedRenderbufferStorage(rb, GL_DEPTH_COMPONENT24, w, h);
				glNamedFramebufferRenderbuffer(fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
				m_DepthBuffers[fbo] = rb;
			}
		}

		rt.Complete = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
		if (!rt.Complete)
			GB_CORE_ERROR("Framebuffer {}x{} is incomplete", desc.Width, desc.Height);
		return rt;
	}

	void GLDevice::DestroyRenderTarget(RenderTarget& target)
	{
		if (target.Handle)
		{
			glDeleteFramebuffers(1, &target.Handle.Id);
			const auto rb = m_DepthBuffers.find(target.Handle.Id);
			if (rb != m_DepthBuffers.end())
			{
				glDeleteRenderbuffers(1, &rb->second);
				m_DepthBuffers.erase(rb);
			}
		}
		DestroyTexture(target.Color);
		DestroyTexture(target.Extra);
		DestroyTexture(target.DepthTexture);
		target = {};
	}

	void GLDevice::ReadPixels(RenderTargetHandle target, uint32_t width, uint32_t height, uint8_t* out)
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id);
		if (!target)
			glReadBuffer(GL_BACK);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE, out);
	}

	ProgramHandle GLDevice::CreateProgram(const std::string& vertexSource, const std::string& fragmentSource)
	{
		const GLuint vs = Compile(GL_VERTEX_SHADER, vertexSource);
		const GLuint fs = Compile(GL_FRAGMENT_SHADER, fragmentSource);
		if (!vs || !fs)
		{
			glDeleteShader(vs);
			glDeleteShader(fs);
			return {};
		}

		const GLuint program = glCreateProgram();
		glAttachShader(program, vs);
		glAttachShader(program, fs);
		glLinkProgram(program);
		glDeleteShader(vs);
		glDeleteShader(fs);

		GLint ok = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &ok);
		if (!ok)
		{
			GLint len = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
			std::vector<char> log(len > 0 ? static_cast<size_t>(len) : 1);
			glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
			GB_CORE_ERROR("Shader link error:\n{}", log.data());
			glDeleteProgram(program);
			return {};
		}
		return { program };
	}

	int GLDevice::GetUniformLocation(ProgramHandle program, const std::string& name) { return glGetUniformLocation(program.Id, name.c_str()); }

	void GLDevice::DestroyProgram(ProgramHandle program)
	{
		if (program) glDeleteProgram(program.Id);
	}

	VertexLayoutHandle GLDevice::CreateVertexLayout(const VertexLayoutDesc& desc)
	{
		GLuint vao = 0;
		glCreateVertexArrays(1, &vao);
		for (const VertexBinding& b : desc.Bindings)
		{
			if (b.Buffer)
				glVertexArrayVertexBuffer(vao, b.Binding, b.Buffer.Id, static_cast<GLintptr>(b.Offset), static_cast<GLsizei>(b.Stride));
			if (b.Divisor)
				glVertexArrayBindingDivisor(vao, b.Binding, b.Divisor);
		}
		if (desc.Indices)
			glVertexArrayElementBuffer(vao, desc.Indices.Id);
		for (const VertexAttrib& a : desc.Attribs)
		{
			glEnableVertexArrayAttrib(vao, a.Location);
			if (a.Type == AttribType::UInt)
				glVertexArrayAttribIFormat(vao, a.Location, static_cast<GLint>(a.Components), GL_UNSIGNED_INT, a.Offset);
			else
				glVertexArrayAttribFormat(vao, a.Location, static_cast<GLint>(a.Components), GL_FLOAT, GL_FALSE, a.Offset);
			glVertexArrayAttribBinding(vao, a.Location, a.Binding);
		}
		return { vao };
	}

	void GLDevice::DestroyVertexLayout(VertexLayoutHandle layout)
	{
		if (layout) glDeleteVertexArrays(1, &layout.Id);
	}

	PipelineHandle GLDevice::CreatePipeline(const PipelineDesc& desc)
	{
		for (size_t i = 0; i < m_Pipelines.size(); ++i)
			if (!m_Pipelines[i])
			{
				m_Pipelines[i] = desc;
				return { static_cast<uint32_t>(i + 1) };
			}
		m_Pipelines.push_back(desc);
		return { static_cast<uint32_t>(m_Pipelines.size()) };
	}

	void GLDevice::DestroyPipeline(PipelineHandle pipeline)
	{
		if (pipeline && pipeline.Id <= m_Pipelines.size())
			m_Pipelines[pipeline.Id - 1].reset();
	}

	const PipelineDesc* GLDevice::GetPipeline(PipelineHandle pipeline) const
	{
		if (!pipeline || pipeline.Id > m_Pipelines.size() || !m_Pipelines[pipeline.Id - 1])
			return nullptr;
		return &*m_Pipelines[pipeline.Id - 1];
	}

	QueryHandle GLDevice::CreateTimestamp()
	{
		GLuint q = 0;
		glGenQueries(1, &q);
		return { q };
	}

	void GLDevice::DestroyTimestamp(QueryHandle query)
	{
		if (query) glDeleteQueries(1, &query.Id);
	}

	bool GLDevice::TimestampReady(QueryHandle query)
	{
		GLint ready = 0;
		glGetQueryObjectiv(query.Id, GL_QUERY_RESULT_AVAILABLE, &ready);
		return ready != 0;
	}

	uint64_t GLDevice::TimestampNs(QueryHandle query)
	{
		GLuint64 t = 0;
		glGetQueryObjectui64v(query.Id, GL_QUERY_RESULT, &t);
		return t;
	}
}
