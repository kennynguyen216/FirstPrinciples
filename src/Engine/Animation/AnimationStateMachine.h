#pragma once

#include "Core/CoreMinimal.h"

namespace FP
{
	// How a condition tests its parameter. Bools are 1 or 0.
	enum class AnimCompare : uint8
	{
		Greater,
		Less,
		Equal,
		NotEqual,
		IsTrue,
		IsFalse,
		Triggered, // set by SetTrigger and used up by the transition that sees it
	};

	struct AnimCondition
	{
		String Parameter;
		AnimCompare Compare = AnimCompare::IsTrue;
		float Value = 0;
	};

	// One clip and how it plays.
	struct AnimState
	{
		String Name;
		String Clip;
		bool Loop = true;
		float Speed = 1;
	};

	// Moves from one state to another, blending over BlendTime seconds.
	struct AnimTransition
	{
		String From; // empty means from any state
		String To;
		float BlendTime = 0.2f;
		float ExitTime = -1; // fraction of From that must play first, below 0 to leave any time
		Array<AnimCondition> Conditions; // all must pass. None means leave at ExitTime.

		// Adds a condition. Returns itself so calls chain.
		AnimTransition& When(std::string_view parameter, AnimCompare compare = AnimCompare::IsTrue, float value = 0)
		{
			Conditions.Add({ String(parameter), compare, value });
			return *this;
		}

		// Waits until this much of From has played. 1 is the end of the clip.
		AnimTransition& AfterExitTime(float fraction = 1)
		{
			ExitTime = fraction;
			return *this;
		}
	};

	// States, transitions and parameters, plus which state is playing. Plain data, so it can be
	// built from scripts and stepped without any clips loaded.
	class AnimationStateMachine
	{
	public:
		// Adding a state or transition that already exists replaces it, so building the same
		// machine twice (for example after a hot reload) changes nothing.
		AnimState& AddState(std::string_view name, std::string_view clip, bool loop = true, float speed = 1);
		AnimTransition& AddTransition(std::string_view from, std::string_view to, float blendTime = 0.2f);
		void Clear();

		bool HasStates() const { return !m_States.IsEmpty(); }
		const AnimState* FindState(std::string_view name) const;

		void SetFloat(std::string_view name, float value);
		void SetBool(std::string_view name, bool value) { SetFloat(name, value ? 1.0f : 0.0f); }
		void SetTrigger(std::string_view name);
		void ResetTrigger(std::string_view name);
		float GetFloat(std::string_view name) const;
		bool GetBool(std::string_view name) const { return GetFloat(name) != 0; }

		// Jumps to a state, blending from the current one when blendTime is above 0.
		void Play(std::string_view state, float blendTime = 0);

		// Advances time and takes at most one transition. clipLength(clipName) returns seconds,
		// or 0 when the clip is missing.
		template <typename ClipLength>
		void Update(float deltaTime, ClipLength&& clipLength)
		{
			if (m_Current.State < 0 && !m_States.IsEmpty())
				Play(m_States[0].Name);
			Advance(m_Current, deltaTime, clipLength(StateClip(m_Current.State)));
			if (m_Previous.State >= 0)
			{
				Advance(m_Previous, deltaTime, clipLength(StateClip(m_Previous.State)));
				m_BlendElapsed += deltaTime;
				if (m_BlendElapsed >= m_BlendTime)
					m_Previous = {};
			}
			const int32 next = FindTransition(clipLength(StateClip(m_Current.State)));
			if (next >= 0)
				Take(m_Transitions[next]);
		}

		// What is playing. Time is in clip seconds, already looped or clamped.
		struct Playback
		{
			int32 State = -1;
			float Time = 0;
			float Played = 0; // seconds since the state started, scaled by its speed
		};
		const AnimState* GetCurrentState() const { return m_States.IsValidIndex(m_Current.State) ? &m_States[m_Current.State] : nullptr; }
		const AnimState* GetPreviousState() const { return m_States.IsValidIndex(m_Previous.State) ? &m_States[m_Previous.State] : nullptr; }
		const Playback& GetCurrent() const { return m_Current; }
		const Playback& GetPrevious() const { return m_Previous; }

		// How much of the current state shows, from 0 just after a transition to 1 once it is done.
		float GetBlend() const;

		const Array<AnimState>& GetStates() const { return m_States; }
		const Array<AnimTransition>& GetTransitions() const { return m_Transitions; }

	private:
		struct Parameter
		{
			String Name;
			float Value = 0;
		};

		const String& StateClip(int32 state) const;
		int32 FindStateIndex(std::string_view name) const;
		void Advance(Playback& playback, float deltaTime, float length) const;
		int32 FindTransition(float currentLength) const;
		bool Passes(const AnimCondition& condition) const;
		void Take(const AnimTransition& transition);

		Array<AnimState> m_States;
		Array<AnimTransition> m_Transitions;
		Array<Parameter> m_Parameters;
		Playback m_Current;
		Playback m_Previous;
		float m_BlendTime = 0;
		float m_BlendElapsed = 0;
	};
}
