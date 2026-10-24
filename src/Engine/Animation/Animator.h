#pragma once

#include "Engine/Animation/AnimationStateMachine.h"
#include "Engine/Animation/SkeletalModel.h"
#include "Engine/World.h"

#include <memory>

namespace FP
{
	// Controls one entity's animation. Scripts get it with GetAnimator() and usually build a state
	// machine in OnStart, then set parameters in OnUpdate:
	//
	//   Animator* animator = GetAnimator();
	//   animator->AddState("Idle", "Idle");
	//   animator->AddState("Walk", "Walk");
	//   animator->AddTransition("Idle", "Walk").When("Speed", AnimCompare::Greater, 0.1f);
	//   animator->AddTransition("Walk", "Idle").When("Speed", AnimCompare::Less, 0.1f);
	//
	// Every call runs in the engine, so the animator keeps no code from a script module that
	// may reload. States and transitions with the same names replace the old ones.
	// References returned by AddState and AddTransition are only good until the next add.
	class Animator
	{
	public:
		virtual ~Animator() = default;

		// The first state added plays first.
		virtual AnimState& AddState(std::string_view name, std::string_view clip, bool loop = true, float speed = 1) = 0;
		// An empty from means any state.
		virtual AnimTransition& AddTransition(std::string_view from, std::string_view to, float blendTime = 0.2f) = 0;
		virtual void ClearStates() = 0;

		virtual void SetFloat(std::string_view name, float value) = 0;
		virtual void SetBool(std::string_view name, bool value) = 0;
		virtual void SetTrigger(std::string_view name) = 0;
		virtual void ResetTrigger(std::string_view name) = 0;
		virtual float GetFloat(std::string_view name) const = 0;
		virtual bool GetBool(std::string_view name) const = 0;

		// Jumps straight to a state, skipping transitions.
		virtual void Play(std::string_view state, float blendTime = 0.2f) = 0;

		// Null before the first update and when there are no states.
		virtual const AnimState* GetCurrentState() const = 0;
		bool IsInState(std::string_view name) const
		{
			const AnimState* state = GetCurrentState();
			return state && state->Name == name;
		}

		// How far through the current state, where 1 is one play of its clip.
		virtual float GetNormalizedTime() const = 0;

		// Seconds, or 0 when the skeletal mesh has no clip with the name.
		virtual float GetClipLength(std::string_view clip) const = 0;
	};

	// The engine's Animator. Holds the state machine and the pose it produced last.
	class AnimatorInstance final : public Animator
	{
	public:
		AnimState& AddState(std::string_view name, std::string_view clip, bool loop, float speed) override { return m_Machine.AddState(name, clip, loop, speed); }
		AnimTransition& AddTransition(std::string_view from, std::string_view to, float blendTime) override { return m_Machine.AddTransition(from, to, blendTime); }
		void ClearStates() override { m_Machine.Clear(); }

		void SetFloat(std::string_view name, float value) override { m_Machine.SetFloat(name, value); }
		void SetBool(std::string_view name, bool value) override { m_Machine.SetBool(name, value); }
		void SetTrigger(std::string_view name) override { m_Machine.SetTrigger(name); }
		void ResetTrigger(std::string_view name) override { m_Machine.ResetTrigger(name); }
		float GetFloat(std::string_view name) const override { return m_Machine.GetFloat(name); }
		bool GetBool(std::string_view name) const override { return m_Machine.GetBool(name); }

		void Play(std::string_view state, float blendTime) override { m_Machine.Play(state, blendTime); }
		const AnimState* GetCurrentState() const override { return m_Machine.GetCurrentState(); }
		float GetNormalizedTime() const override;
		float GetClipLength(std::string_view clip) const override;

		// Steps time and samples the pose. Without states, loops fallbackClip.
		void Tick(const SkeletalModel& model, float deltaTime, std::string_view fallbackClip);

		const Pose& GetPose() const { return m_Pose; }
		const AnimationStateMachine& GetMachine() const { return m_Machine; }

		// The animator for an entity with an AnimatorComponent, made on first use. Null otherwise.
		static AnimatorInstance* For(World& world, Entity entity);

	private:
		AnimationStateMachine m_Machine;
		const SkeletalModel* m_Model = nullptr;
		float m_FallbackTime = 0;
		Pose m_Pose;
		Pose m_BlendFrom;
	};

	// Where the engine keeps each entity's animator while playing. Not saved.
	struct AnimatorStateComponent
	{
		std::unique_ptr<AnimatorInstance> Instance;
	};
}
