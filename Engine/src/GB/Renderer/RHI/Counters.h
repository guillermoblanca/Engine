#pragma once
// What a frame asks of the RHI backend and what reaches the graphics API (T70, render M0: DESACOPLE_MATERIALES §1.2 and §4). The backends (GL
// and Null) count as the commands are recorded; the renderer zeroes the counters at the start of a frame and copies them into RenderStats.
// Plain data with no RHI types, so RenderStats (and Scene.h, which includes it) need not see the RHI.
#include <cstdint>

namespace GB::RHI
{
	struct FrameCounters
	{
		uint32_t UniformCalls = 0;    // SetUniform* asked for
		uint32_t UniformsSent = 0;    // ... that reached the API (with the uniform cache on, a value equal to the program's last one is not sent)
		uint32_t TextureBinds = 0;    // BindTexture
		uint32_t PipelineBinds = 0;   // BindPipeline
		uint32_t BufferBinds = 0;     // BindStorageBuffer and SetVertexBuffer
		uint32_t LayoutBinds = 0;     // BindVertexLayout
		uint32_t StateCalls = 0;      // cull, alpha to coverage and blend, asked for (dynamic or by a pipeline)
		uint32_t StateChanges = 0;    // ... that reached the API (with the state filter on, a repeat of the current state is not sent)
		uint64_t UploadBytes = 0;     // UploadStream, UpdateBuffer, UploadTexture and buffers created with data
		// T71 (render M1), apart from the ones above so these keep meaning what they did:
		uint32_t UniformBufferBinds = 0;   // BindUniformBuffer
		uint32_t StorageRangeBinds = 0;    // BindStorageBufferRange (BindStorageBuffer stays in BufferBinds)
		uint64_t RingBytes = 0;            // allocated from upload rings (with the alignment padding); never in UploadBytes
		uint64_t CompressedBytes = 0;      // UploadCompressed (UploadTextureLayer counts in UploadBytes, as UploadTexture)

		// The work between two snapshots (`*this` taken after `before`).
		FrameCounters Since(const FrameCounters& before) const
		{
			FrameCounters d;
			d.UniformCalls = UniformCalls - before.UniformCalls;
			d.UniformsSent = UniformsSent - before.UniformsSent;
			d.TextureBinds = TextureBinds - before.TextureBinds;
			d.PipelineBinds = PipelineBinds - before.PipelineBinds;
			d.BufferBinds = BufferBinds - before.BufferBinds;
			d.LayoutBinds = LayoutBinds - before.LayoutBinds;
			d.StateCalls = StateCalls - before.StateCalls;
			d.StateChanges = StateChanges - before.StateChanges;
			d.UploadBytes = UploadBytes - before.UploadBytes;
			d.UniformBufferBinds = UniformBufferBinds - before.UniformBufferBinds;
			d.StorageRangeBinds = StorageRangeBinds - before.StorageRangeBinds;
			d.RingBytes = RingBytes - before.RingBytes;
			d.CompressedBytes = CompressedBytes - before.CompressedBytes;
			return d;
		}
		FrameCounters& operator+=(const FrameCounters& o)
		{
			UniformCalls += o.UniformCalls; UniformsSent += o.UniformsSent; TextureBinds += o.TextureBinds; PipelineBinds += o.PipelineBinds;
			BufferBinds += o.BufferBinds; LayoutBinds += o.LayoutBinds; StateCalls += o.StateCalls; StateChanges += o.StateChanges;
			UploadBytes += o.UploadBytes;
			UniformBufferBinds += o.UniformBufferBinds; StorageRangeBinds += o.StorageRangeBinds; RingBytes += o.RingBytes;
			CompressedBytes += o.CompressedBytes;
			return *this;
		}
	};

	// The counters of the frame being recorded (one context, no threads: like DrawCounter).
	struct Counters
	{
		static inline FrameCounters Frame;
	};
}
