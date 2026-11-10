#pragma once
// The last value set to each uniform of each program (T70, render M0), so a backend can skip sending a value the program already has. A flat
// table per program, indexed by location, with the value inline: a lookup is two vector indexings and a memcmp, cheaper than the API call it
// saves. Values compare bit for bit (0.0 and -0.0 differ, a NaN equals itself): skipping a send never changes what the program holds.
//
// Only single values are kept (an int, a float to a vec4, a mat4); an array set forgets its location and is always sent. A program's
// entries must be forgotten when it is destroyed (GL reuses the names) and everything when the cache is turned back on (the sets made
// while it was off were not recorded).
#include <cstdint>
#include <cstring>
#include <vector>

namespace GB::RHI
{
	class UniformCache
	{
	public:
		// What a value is: the same bits as another kind (an int 1 and a float 1.4e-45) are a different value.
		enum class Kind : uint8_t { None, Int, Float1, Float2, Float3, Float4, Mat4 };

		// True if (program, location) did not hold this value: it is recorded and the caller sends it. False: the program already has it.
		bool Update(uint32_t program, int location, Kind kind, const void* data, uint32_t bytes)
		{
			if (location < 0 || location >= kMaxLocation || program == 0 || program >= kMaxProgram || bytes > sizeof(Slot::Value))
				return true;
			if (program >= m_Programs.size())
				m_Programs.resize(program + 1);
			std::vector<Slot>& slots = m_Programs[program];
			if (static_cast<size_t>(location) >= slots.size())
				slots.resize(static_cast<size_t>(location) + 1);
			Slot& slot = slots[static_cast<size_t>(location)];
			if (slot.What == kind && std::memcmp(slot.Value, data, bytes) == 0)
				return false;
			slot.What = kind;
			std::memcpy(slot.Value, data, bytes);
			return true;
		}

		void Forget(uint32_t program, int location)
		{
			if (program < m_Programs.size() && location >= 0 && static_cast<size_t>(location) < m_Programs[program].size())
				m_Programs[program][static_cast<size_t>(location)].What = Kind::None;
		}
		void ForgetProgram(uint32_t program)
		{
			if (program < m_Programs.size())
				m_Programs[program].clear();
		}
		void Clear() { m_Programs.clear(); }

	private:
		// Bounds past which nothing is cached (sent every time): GL names and locations are small integers in practice.
		static constexpr uint32_t kMaxProgram = 1u << 16;
		static constexpr int kMaxLocation = 4096;
		struct Slot
		{
			Kind What = Kind::None;
			float Value[16] = {};
		};
		std::vector<std::vector<Slot>> m_Programs;   // by program id, then by location
	};
}
