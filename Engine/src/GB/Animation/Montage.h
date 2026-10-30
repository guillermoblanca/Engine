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

	// The shape of a blend: how the weight (0..1, linear in time) becomes the share of the montage in the pose.
	enum class BlendCurve { Smooth, Linear, EaseIn, EaseOut };
	const char* BlendCurveName(BlendCurve curve);                       // "smooth", "linear", "easeIn", "easeOut"
	BlendCurve BlendCurveFromName(const std::string& name);              // unknown = Smooth
	float ShapeBlend(BlendCurve curve, float weight);                    // 0 -> 0 and 1 -> 1 for every curve

	// The animation clock of one montage run, without a window or a model: where the clip is after `dt` seconds of its own time, inside the
	// trimmed part (fractions of `duration`). A loop wraps from the trim end to the trim start; a one-shot stops at the end.
	struct ClockStep
	{
		float Time = 0.0f;
		bool Wrapped = false;
		bool Ended = false;   // reached the trim end (a loop wraps instead)
	};
	ClockStep AdvanceClipClock(float time, float dt, float duration, float trimStart, float trimEnd, bool loop);

	// How one montage gives way to another in the same slot (instead of the old one's BlendOut and the new one's BlendIn): `From` and `To` are
	// montage names, "*" for any.
	struct MontageTransition
	{
		std::string From, To;
		float Duration = 0.2f;
		BlendCurve Curve = BlendCurve::Smooth;
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
		// Stays on the clip's last frame until Stop instead of blending out at the end: a pose the action ends in and keeps (lying down to sleep,
		// sitting down to eat). Ignored with Loop.
		bool HoldEnd = false;
		// The part of the clip that plays, as fractions (0..1) of it: the run starts at TrimStart, ends (or loops) at TrimEnd. Notifies and sections
		// stay fractions of the whole clip.
		float TrimStart = 0.0f, TrimEnd = 1.0f;
		BlendCurve Curve = BlendCurve::Smooth;   // shape of its blend in and out
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
		std::vector<std::string> SlotNames() const;
		float Duration(const std::string& name) const;   // seconds the WHOLE clip lasts at the montage's play rate (trim not applied), 0 if unknown
		float PlayDuration(const std::string& name) const;   // seconds a run lasts: the trimmed part at the play rate
		float ClipDuration(const std::string& name) const;   // seconds of the whole clip (no trim, no rate), 0 if unknown
		float Weight(const std::string& name) const;     // 0..1: how much of this montage run is in the slot now (0 when it is not playing)
		// Blends between pairs of montages (see MontageTransition): Play uses the matching one (exact pair, then From -> "*", then "*" -> To) for the
		// montage it replaces and for itself.
		void SetTransitions(std::vector<MontageTransition> transitions) { m_Transitions = std::move(transitions); }
		const std::vector<MontageTransition>& Transitions() const { return m_Transitions; }

		const MontageDesc* Get(const std::string& name) const;      // null when unknown
		std::vector<MontageDesc> Descs() const;                      // every montage, for saving
		bool Remove(const std::string& name);                        // stops it too

		// `section`: start from that section of the montage ("" = the start). `rateScale` multiplies the montage's own play rate for this run only.
		bool Play(const std::string& name, const std::string& section = "", float rateScale = 1.0f);
		// Moves the playing montage to a section (false when it is not playing or has no such section).
		bool JumpToSection(const std::string& name, const std::string& section);
		// Editing aids: freeze every montage (a paused preview keeps its weight) and move the one that is playing to `seconds` into its clip.
		void SetPaused(bool paused) { m_Paused = paused; }
		bool Paused() const { return m_Paused; }
		bool Seek(const std::string& name, float seconds);
		float Time(const std::string& name) const;                   // seconds into the clip of the playing montage, -1 when not playing
		// How far the run is, 0..1 of the part that plays (the trim): the clock things tied to "the fraction of the animation" read, so a trimmed montage
		// has its props and reach windows over the part that is seen, not over the whole clip. -1 when not playing.
		float RunFraction(const std::string& name) const;
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
			float RateScale = 1.0f;  // multiplies the montage's PlayRate (a faster swing with a better tool)
			float Weight = 0.0f;
			bool Leaving = false;    // blending out (stopped, replaced or at its end)
			float LeaveBlend = 0.25f;
			bool Interrupted = false;
			float BlendInTime = -1.0f;   // >= 0: a transition's duration replaces the montage's BlendIn
			BlendCurve Curve = BlendCurve::Smooth;
			bool CurveSet = false;       // a transition's curve replaces the montage's
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
		std::vector<MontageTransition> m_Transitions;
	};

	// Montages as JSON text (names, clips, slots, blends, rate, loop, notifies and sections): what the timeline editor saves and loads.
	std::string MontagesToJson(const std::vector<MontageDesc>& montages);
	bool MontagesFromJson(const std::string& text, std::vector<MontageDesc>& out, std::string* error = nullptr);

	// The farming pack's one-shot actions as montages (the ones whose clips the model has), and the "UpperBody" slot to add.
	std::vector<MontageDesc> MixamoMontages();

	// The Universal Animation Library's actions (interact, pick up, roll, hits, death, sitting, sword, punches, pistol, spells, dance), as they are
	// named once retargeted onto a character; the ones whose clips the model has are added with the farming ones.
	std::vector<MontageDesc> UniversalMontages();
}
