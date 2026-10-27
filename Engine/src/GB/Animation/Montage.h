#pragma once
#include "GB/Animation/ClipAnalysis.h"
#include "GB/Animation/Pose.h"
#include <string>
#include <vector>

namespace GB
{
	// A named moment of a montage, as a fraction (0..1) of its clip: the hand reaches the fruit, the bucket tips, the spade lands.
	struct MontageNotify
	{
		float Time = 0.0f;
		std::string Name;
	};

	// A named part of a montage that it can start from or jump to (a "start", a "loop" and an "end" of an action): from `Time` (a fraction of
	// the clip) to the next section.
	struct MontageSection
	{
		float Time = 0.0f;
		std::string Name;
	};

	// A one-shot action (pick a fruit, milk a cow, dig) played over the locomotion pose in a slot. A slot is a part of the
	// body (the whole body, or everything below a bone such as the spine): a montage only replaces the bones of its slot.
	struct MontageDesc
	{
		std::string Name;
		std::string Clip;                 // clip name in the model
		std::string Slot = "FullBody";
		float BlendIn = 0.25f, BlendOut = 0.25f;   // seconds
		float PlayRate = 1.0f;
		bool Loop = false;                // plays until Stop
		bool StripTravel = true;          // the controller owns the position: take the clip's own horizontal travel out of the root
		std::vector<MontageNotify> Notifies;
		std::vector<MontageSection> Sections;
	};

	struct MontageEvent
	{
		enum class Kind { Notify, Finished, Interrupted };
		Kind What = Kind::Notify;
		std::string Montage;
		std::string Name;                 // the notify's name
	};

	// Plays montages over a pose. Several slots can be active at once (an upper-body action while the legs keep walking); a
	// new montage in a slot blends the old one out. Weights move linearly (then ease), so stopping or replacing in the middle of
	// a blend never jumps.
	class MontagePlayer
	{
	public:
		static constexpr const char* kFullBody = "FullBody";

		MontagePlayer(const Model& model, const ClipAnalysisConfig& analysis = {});

		// "FullBody" always exists. False when `rootBone` is not in the model.
		bool AddSlot(const std::string& name, const std::string& rootBone);
		// False when the clip or the slot does not exist. The same name replaces the montage.
		bool Add(const MontageDesc& desc);
		bool Has(const std::string& name) const { return Find(name) >= 0; }
		std::vector<std::string> Names() const;
		float Duration(const std::string& name) const;   // seconds at the montage's play rate, 0 if unknown

		const MontageDesc* Get(const std::string& name) const;      // null when unknown
		std::vector<MontageDesc> Descs() const;                      // every montage, for saving
		bool Remove(const std::string& name);                        // stops it too

		// `section`: start from that section of the montage ("" = the start).
		bool Play(const std::string& name, const std::string& section = "");
		// Moves the playing montage to a section (false when it is not playing or has no such section).
		bool JumpToSection(const std::string& name, const std::string& section);
		// Editing aids: freeze every montage (a paused preview keeps its weight) and move the one that is playing to `seconds` into its clip.
		void SetPaused(bool paused) { m_Paused = paused; }
		bool Paused() const { return m_Paused; }
		bool Seek(const std::string& name, float seconds);
		float Time(const std::string& name) const;                   // seconds into the clip of the playing montage, -1 when not playing
		void Stop(const std::string& name, float blendOut = -1.0f);   // < 0: the montage's own BlendOut
		void StopSlot(const std::string& slot, float blendOut = -1.0f);
		bool IsPlaying(const std::string& name) const;
		bool Busy() const { return !m_Active.empty(); }
		float SlotWeight(const std::string& slot) const;   // 0..1: how much of the slot is montage
		float FullBodyWeight() const { return SlotWeight(kFullBody); }

		// Advances the montages and collects this frame's events (notifies, finished, interrupted).
		void Update(float dt);
		void Apply(Pose& pose);
		const std::vector<MontageEvent>& Events() const { return m_Events; }

	private:
		struct Slot { std::string Name; std::vector<float> Mask; };
		struct Entry { MontageDesc Desc; int Clip = -1, Slot = 0; ClipStats Stats; };
		struct Active
		{
			int Entry = 0;
			float Time = 0.0f;       // seconds into the clip
			float Weight = 0.0f;
			bool Leaving = false;    // blending out (stopped, replaced or at its end)
			float LeaveBlend = 0.25f;
			bool Interrupted = false;
		};
		int Find(const std::string& name) const;
		int FindSlot(const std::string& name) const;
		void Leave(Active& a, float blendOut, bool interrupted);

		const Model& m_Model;
		int m_Root = -1;
		std::vector<Slot> m_Slots;
		std::vector<Entry> m_Entries;
		std::vector<Active> m_Active;
		std::vector<MontageEvent> m_Events, m_Pending;   // pending: raised by Play / Stop between two updates
		Pose m_Scratch;
		bool m_Paused = false;
	};

	// Montages as JSON text (names, clips, slots, blends, rate, loop, notifies and sections): what the timeline editor saves and loads.
	std::string MontagesToJson(const std::vector<MontageDesc>& montages);
	bool MontagesFromJson(const std::string& text, std::vector<MontageDesc>& out, std::string* error = nullptr);

	// The farming pack's one-shot actions as montages (the ones whose clips the model has), and the "UpperBody" slot to add.
	std::vector<MontageDesc> MixamoMontages();
}
