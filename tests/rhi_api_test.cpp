// T71 (render M1) and T65: the RHI's API of M1 on the null backend, in a program that links GB_RHI alone. That it links at all says the
// interface and the null backend need no GL, no GB_Render and no GetDevice (which lives in GB_RHI_GL). The cases: the compressed formats
// and their sizes, texture arrays, compressed and per-layer uploads, uniform and storage buffers bound by range, the indexed instanced
// draw with first index, base vertex and base instance, the upload ring (regions, alignment, waits and fences) and the new counters.
#include "GB/Renderer/RHI/NullDevice.h"
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

	using namespace GB::RHI;

	void Formats()
	{
		static_assert(IsCompressed(Format::BC7) && IsCompressed(Format::BC7SRGB) && IsCompressed(Format::BC5));
		static_assert(!IsCompressed(Format::RGBA8) && !IsCompressed(Format::SRGB8A8) && !IsCompressed(Format::Depth24));
		Check(CompressedBytes(Format::BC7, 4, 4) == 16 && CompressedBytes(Format::BC5, 4, 4) == 16, "formats: one 4x4 block is 16 bytes");
		Check(CompressedBytes(Format::BC7, 1024, 1024) == 1024u * 1024u, "formats: BC7 is one byte per texel");
		Check(CompressedBytes(Format::BC5, 5, 3) == 2 * 1 * 16 && CompressedBytes(Format::BC7SRGB, 1, 1) == 16, "formats: partial blocks round up");
		Check(CompressedBytes(Format::RGBA8, 4, 4) == 0, "formats: an uncompressed format has no blocks");

		TextureDesc plain, layered, single;
		layered.Layers = 3;
		single.Array = true;
		Check(!plain.IsArray() && layered.IsArray() && single.IsArray(), "texture desc: Layers > 1 or Array make an array; the default is 2D");
	}

	void Textures(NullDevice& device)
	{
		const FrameCounters before = Counters::Frame;
		TextureDesc desc;
		desc.Width = desc.Height = 8;
		desc.Levels = 2;
		desc.Layers = 4;
		desc.Fmt = Format::BC7SRGB;
		const TextureHandle albedo = device.CreateTexture(desc);
		const TextureDesc* kept = device.GetTexture(albedo);
		Check(kept && kept->Layers == 4 && kept->Fmt == Format::BC7SRGB && kept->IsArray(), "textures: an array of 4 BC7 sRGB layers");

		const std::vector<unsigned char> blocks(CompressedBytes(Format::BC7SRGB, 8, 8), 0x40);
		device.UploadCompressed(albedo, 0, 3, 0, 0, 8, 8, blocks.size(), blocks.data());
		device.UploadCompressed(albedo, 1, 2, 0, 0, 4, 4, 16, blocks.data());
		Check(device.Misuses == 0, "textures: compressed uploads to a level and a layer");
		device.UploadCompressed(albedo, 0, 0, 0, 0, 8, 8, 16, blocks.data());
		Check(device.Misuses == 1, "textures: the wrong number of bytes for the region is a misuse");
		device.UploadCompressed(albedo, 0, 4, 0, 0, 4, 4, 16, blocks.data());
		Check(device.Misuses == 2, "textures: a layer past the array's is a misuse");
		device.UploadTextureLayer(albedo, 0, 0, 0, 0, 4, 4, PixelData::RGBA8, blocks.data());
		Check(device.Misuses == 3, "textures: uncompressed pixels into a compressed texture are a misuse");

		TextureDesc rgba;
		rgba.Width = rgba.Height = 4;
		rgba.Layers = 2;
		const TextureHandle layers = device.CreateTexture(rgba);
		const std::vector<unsigned char> pixels(4 * 4 * 4, 0x7f);
		device.UploadTextureLayer(layers, 0, 1, 0, 0, 4, 4, PixelData::RGBA8, pixels.data());
		Check(device.Misuses == 3, "textures: RGBA8 pixels into layer 1 of an array");
		device.UploadCompressed(layers, 0, 0, 0, 0, 4, 4, 16, blocks.data());
		Check(device.Misuses == 4, "textures: compressed blocks into an uncompressed texture are a misuse");

		TextureDesc normal;
		normal.Width = normal.Height = 4;
		normal.Fmt = Format::BC5;
		const TextureHandle flat = device.CreateTexture(normal);
		device.UploadCompressed(flat, 0, 0, 0, 0, 4, 4, 16, blocks.data());
		Check(device.Misuses == 4 && !device.GetTexture(flat)->IsArray(), "textures: a plain 2D BC5 texture takes its block");
		device.UploadCompressed(flat, 0, 1, 0, 0, 4, 4, 16, blocks.data());
		Check(device.Misuses == 5, "textures: a plain 2D texture has only layer 0");

		const FrameCounters d = Counters::Frame.Since(before);
		// counted: 64 + 16 + 16 (the misuse with 16 bytes) + 16 (layer 4) + 16 (into RGBA8) + 16 (BC5) + 16 (BC5 layer 1)
		Check(d.CompressedBytes == 64 + 16 * 6, "counters: compressed uploads in CompressedBytes");
		Check(d.UploadBytes == 2 * 4 * 4 * 4, "counters: per-layer uploads in UploadBytes, as UploadTexture");

		device.DestroyTexture(layers);
		Check(device.GetTexture(layers) == nullptr, "textures: destroyed, forgotten");
		device.Misuses = 0;
	}

	void RangesAndDraws(NullDevice& device)
	{
		device.Caps.UniformBufferAlignment = 256;
		device.Caps.StorageBufferAlignment = 16;
		NullCommandList& list = device.List;
		list.Log.clear();
		const FrameCounters before = Counters::Frame;
		const BufferHandle buffer = device.CreateBuffer(4096, nullptr, true);

		CommandList& cmd = device.Commands();
		cmd.BindUniformBuffer(0, buffer, 256, 1408);
		cmd.BindStorageBufferRange(3, buffer, 2048, 512);
		cmd.BindStorageBufferRange(4, buffer, 48, 80);
		cmd.BindStorageBuffer(5, buffer);
		Check(device.Misuses == 0, "ranges: offsets on the alignments");
		cmd.BindUniformBuffer(1, buffer, 100, 64);
		Check(device.Misuses == 1, "ranges: a uniform offset off the alignment is a misuse");
		device.Misuses = 0;

		Check(list.Log.size() == 5 && list.Log[0].Op == NullOp::BindUniformBuffer && list.Log[0].A == buffer.Id && list.Log[0].B == 0
			&& list.Log[0].Offset == 256 && list.Log[0].Bytes == 1408, "ranges: the uniform range is recorded (slot, buffer, offset, size)");
		Check(list.Log[1].Op == NullOp::BindStorageBufferRange && list.Log[1].B == 3 && list.Log[1].Offset == 2048 && list.Log[1].Bytes == 512,
			"ranges: the storage range is recorded");

		cmd.DrawIndexedInstanced(IndexedDraw{ 36, 5, 12, -3, 7 });
		cmd.DrawIndexedInstanced(36, 2);
		const NullCommand& full = list.Log[5];
		const NullCommand& old = list.Log[6];
		Check(full.Op == NullOp::DrawIndexedInstanced && full.B == 36 && full.C == 5 && full.FirstIndex == 12 && full.BaseVertex == -3
			&& full.BaseInstance == 7, "draws: first index, base vertex and base instance are recorded");
		Check(old.Op == NullOp::DrawIndexedInstanced && old.B == 36 && old.C == 2 && old.FirstIndex == 0 && old.BaseVertex == 0
			&& old.BaseInstance == 0, "draws: the call of before is the same draw with the three at 0");

		const FrameCounters d = Counters::Frame.Since(before);
		Check(d.UniformBufferBinds == 2 && d.StorageRangeBinds == 2 && d.BufferBinds == 1,
			"counters: uniform and storage range binds apart; BindStorageBuffer still in BufferBinds");
	}

	void Ring(NullDevice& device)
	{
		device.Caps.UniformBufferAlignment = 64;
		device.Caps.StorageBufferAlignment = 32;
		std::unique_ptr<UploadRing> ring = device.CreateUploadRing(1024);
		NullUploadRing& r = static_cast<NullUploadRing&>(*ring);
		Check(ring->Regions() == 3 && ring->RegionBytes() == 1024 && ring->DefaultAlignment() == 64 && ring->Buffer(),
			"ring: 3 regions by default, aligned to the larger of the two alignments");
		Check(!ring->Allocate(16), "ring: nothing before the first BeginFrame");

		const FrameCounters before = Counters::Frame;
		ring->BeginFrame();
		Check(ring->Region() == 0 && r.Waits == std::vector<uint32_t>{ 0 }, "ring: the first frame is region 0 (and waits for it)");
		const RingAllocation a = ring->Allocate(100), b = ring->Allocate(10), c = ring->Allocate(16, 16);
		Check(a && a.Offset == 0 && a.Bytes == 100 && b.Offset == 128 && c.Offset == 144, "ring: offsets rounded up to the alignment asked");
		Check(a.Buffer == ring->Buffer() && static_cast<std::byte*>(b.Ptr) == static_cast<std::byte*>(a.Ptr) + 128, "ring: pointers into the buffer");
		std::memset(a.Ptr, 0xab, a.Bytes);   // (real memory: the write must not crash)
		Check(ring->Used() == 160 && Counters::Frame.Since(before).RingBytes == 160, "ring: used and counted with the padding");
		Check(!ring->Allocate(2000), "ring: more than the region is refused");
		const RingAllocation rest = ring->Allocate(1024 - 192);
		Check(rest && rest.Offset == 192 && !ring->Allocate(1), "ring: fills the region to the byte, then refuses (it never wraps)");
		ring->EndFrame();
		Check(r.Fences == std::vector<uint32_t>{ 0 } && !ring->Allocate(16), "ring: EndFrame fences region 0; no allocation outside a frame");

		ring->BeginFrame();
		const RingAllocation second = ring->Allocate(8);
		Check(ring->Region() == 1 && second.Offset == 1024 && ring->Used() == 8, "ring: the next frame writes region 1");
		ring->EndFrame();
		ring->BeginFrame();
		ring->EndFrame();
		ring->BeginFrame();
		Check(ring->Region() == 0 && r.Waits == std::vector<uint32_t>{ 0, 1, 2, 0 } && r.Fences == std::vector<uint32_t>{ 0, 1, 2 },
			"ring: after 3 frames region 0 again, waited for before it is written");
		ring->EndFrame();
		Check(device.Misuses == 0, "ring: no misuse");
	}

	void Installed(NullDevice& device)
	{
		Check(CurrentDevice() == nullptr, "device: none installed at start");
		SetDevice(&device);
		Check(CurrentDevice() == &device && &device.Limits() == &device.Caps, "device: SetDevice installs it; Limits are the null device's Caps");
		SetDevice(nullptr);
		Check(CurrentDevice() == nullptr, "device: SetDevice(nullptr) uninstalls it");

		FrameCounters a, b;
		a.UniformBufferBinds = 3; a.StorageRangeBinds = 2; a.RingBytes = 100; a.CompressedBytes = 16;
		b = a;
		b += a;
		const FrameCounters d = b.Since(a);
		Check(b.UniformBufferBinds == 6 && b.RingBytes == 200 && d.StorageRangeBinds == 2 && d.CompressedBytes == 16,
			"counters: the new fields add and subtract");
	}

	// T101: a render target's color with a mip chain (the copy of the scene the glass blurs through).
	void TargetLevels(NullDevice& device)
	{
		static_assert(FullMipLevels(1, 1) == 1 && FullMipLevels(4, 4) == 3 && FullMipLevels(1920, 1080) == 11 && FullMipLevels(192, 108) == 8);
		const uint32_t misuses = device.Misuses;
		RenderTargetDesc plain;
		plain.Width = 64; plain.Height = 32; plain.Depth = false; plain.Color = Format::RGBA16F;
		const RenderTarget a = device.CreateRenderTarget(plain);
		const TextureDesc* ca = device.GetTexture(a.Color);
		Check(ca && ca->Levels == 1 && ca->Width == 64 && ca->Fmt == Format::RGBA16F, "target levels: a target's color has one level by default");
		RenderTargetDesc mipped = plain;
		mipped.Levels = FullMipLevels(64, 32);
		const RenderTarget b = device.CreateRenderTarget(mipped);
		const TextureDesc* cb = device.GetTexture(b.Color);
		Check(cb && cb->Levels == 7 && device.Misuses == misuses, "target levels: a single-sample target keeps its 7 levels down to 1x1");
		device.GenerateMips(b.Color);
		RenderTargetDesc msaa = mipped;
		msaa.Samples = 4;
		const RenderTarget c = device.CreateRenderTarget(msaa);
		const TextureDesc* cc = device.GetTexture(c.Color);
		Check(device.Misuses == misuses + 1 && cc && cc->Levels == 1, "target levels: mips on a multisampled target are a misuse (it keeps 1)");
		RenderTargetDesc tooMany = plain;
		tooMany.Levels = 8;
		device.CreateRenderTarget(tooMany);
		Check(device.Misuses == misuses + 2, "target levels: more levels than the size has are a misuse");
	}
}

int main()
{
	NullDevice device;
	Formats();
	Textures(device);
	RangesAndDraws(device);
	Ring(device);
	Installed(device);
	TargetLevels(device);
	if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
	return failures ? 1 : 0;
}
