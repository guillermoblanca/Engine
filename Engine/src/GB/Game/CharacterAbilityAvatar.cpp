#include "GB/Game/CharacterAbilityAvatar.h"

namespace GB
{
	namespace
	{
		class CharacterAbilityMove : public IAbilityMove
		{
		public:
			explicit CharacterAbilityMove(const CharacterAbilityAvatar& avatar) : m_Avatar(avatar) {}

			bool Start(const InteractionTarget& target, AnimInstance* anim)
			{
				m_Runner.ActSeconds = 0.0f;
				return m_Runner.Start(target, anim, false);
			}

			CharacterInput Update(AnimInstance* anim, float dt) override { return m_Runner.Update(*m_Avatar.Controller, *m_Avatar.World, anim, dt); }

			Status GetStatus() const override
			{
				switch (m_Runner.GetPhase())
				{
				case InteractionRunner::Phase::Done: return Status::Done;
				case InteractionRunner::Phase::Failed: return Status::Failed;
				default: return Status::Running;
				}
			}

			const std::string& Error() const override { return m_Runner.Error(); }
			void Cancel() override { m_Runner.Cancel(); }

		private:
			const CharacterAbilityAvatar& m_Avatar;
			InteractionRunner m_Runner;
		};
	}

	std::unique_ptr<IAbilityMove> CharacterAbilityAvatar::MoveTo(const std::string& entity, const std::string& point, AnimInstance* anim, std::string& error)
	{
		InteractionTarget target;
		if (!Resolve || !Resolve(entity, point, target, &error)) return nullptr;
		target.Montage.clear();   // the montage is the ability's next task
		target.Requires.clear();
		target.Gives.clear();
		auto move = std::make_unique<CharacterAbilityMove>(*this);
		move->Start(target, anim);   // a move that cannot start is Failed, with its error
		return move;
	}
}
