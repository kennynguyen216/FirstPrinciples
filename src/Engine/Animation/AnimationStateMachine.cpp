#include "Engine/Animation/AnimationStateMachine.h"

#include <cmath>

namespace FP
{
	AnimState& AnimationStateMachine::AddState(std::string_view name, std::string_view clip, bool loop, float speed)
	{
		int32 index = FindStateIndex(name);
		if (index < 0)
		{
			index = m_States.Num();
			m_States.Emplace().Name = String(name);
		}
		AnimState& state = m_States[index];
		state.Clip = String(clip);
		state.Loop = loop;
		state.Speed = speed;
		return state;
	}

	AnimTransition& AnimationStateMachine::AddTransition(std::string_view from, std::string_view to, float blendTime)
	{
		int32 index = m_Transitions.FindIf([&](const AnimTransition& t) { return t.From == from && t.To == to; });
		if (index < 0)
		{
			index = m_Transitions.Num();
			AnimTransition& added = m_Transitions.Emplace();
			added.From = String(from);
			added.To = String(to);
		}
		AnimTransition& transition = m_Transitions[index];
		transition.BlendTime = blendTime;
		transition.ExitTime = -1;
		transition.Conditions.Clear();
		return transition;
	}

	void AnimationStateMachine::Clear()
	{
		*this = AnimationStateMachine();
	}

	const AnimState* AnimationStateMachine::FindState(std::string_view name) const
	{
		const int32 index = FindStateIndex(name);
		return index >= 0 ? &m_States[index] : nullptr;
	}

	int32 AnimationStateMachine::FindStateIndex(std::string_view name) const
	{
		return m_States.FindIf([&](const AnimState& state) { return state.Name == name; });
	}

	void AnimationStateMachine::SetFloat(std::string_view name, float value)
	{
		const int32 index = m_Parameters.FindIf([&](const Parameter& p) { return p.Name == name; });
		if (index >= 0)
			m_Parameters[index].Value = value;
		else
			m_Parameters.Add({ String(name), value });
	}

	void AnimationStateMachine::SetTrigger(std::string_view name)
	{
		SetFloat(name, 1);
	}

	void AnimationStateMachine::ResetTrigger(std::string_view name)
	{
		SetFloat(name, 0);
	}

	float AnimationStateMachine::GetFloat(std::string_view name) const
	{
		const int32 index = m_Parameters.FindIf([&](const Parameter& p) { return p.Name == name; });
		return index >= 0 ? m_Parameters[index].Value : 0;
	}

	void AnimationStateMachine::Play(std::string_view state, float blendTime)
	{
		const int32 index = FindStateIndex(state);
		if (index < 0)
			return;
		// Nothing to blend from on the first state, so it just starts.
		if (blendTime > 0 && m_Current.State >= 0)
		{
			m_Previous = m_Current;
			m_BlendTime = blendTime;
			m_BlendElapsed = 0;
		}
		else
		{
			m_Previous = {};
		}
		m_Current = {};
		m_Current.State = index;
	}

	float AnimationStateMachine::GetBlend() const
	{
		if (m_Previous.State < 0 || m_BlendTime <= 0)
			return 1;
		const float t = m_BlendElapsed / m_BlendTime;
		return t > 1 ? 1 : t;
	}

	const String& AnimationStateMachine::StateClip(int32 state) const
	{
		static const String none;
		return m_States.IsValidIndex(state) ? m_States[state].Clip : none;
	}

	void AnimationStateMachine::Advance(Playback& playback, float deltaTime, float length) const
	{
		if (!m_States.IsValidIndex(playback.State))
			return;
		const AnimState& state = m_States[playback.State];
		playback.Played += deltaTime * state.Speed;
		if (length <= 0)
			playback.Time = 0;
		else if (state.Loop)
			playback.Time = std::fmod(playback.Played, length) + (playback.Played < 0 ? length : 0);
		else
			playback.Time = playback.Played < 0 ? 0 : playback.Played > length ? length : playback.Played;
	}

	bool AnimationStateMachine::Passes(const AnimCondition& condition) const
	{
		const float value = GetFloat(condition.Parameter.View());
		switch (condition.Compare)
		{
			case AnimCompare::Greater: return value > condition.Value;
			case AnimCompare::Less: return value < condition.Value;
			case AnimCompare::Equal: return value == condition.Value;
			case AnimCompare::NotEqual: return value != condition.Value;
			case AnimCompare::IsTrue: return value != 0;
			case AnimCompare::IsFalse: return value == 0;
			case AnimCompare::Triggered: return value != 0;
		}
		return false;
	}

	int32 AnimationStateMachine::FindTransition(float currentLength) const
	{
		const AnimState* current = GetCurrentState();
		if (!current)
			return -1;
		const float fraction = currentLength > 0 ? m_Current.Played / currentLength : 1;
		for (int32 i = 0; i < m_Transitions.Num(); ++i)
		{
			const AnimTransition& transition = m_Transitions[i];
			if (!transition.From.IsEmpty() && transition.From != current->Name)
				continue;
			// Any state transitions never restart the state they lead to.
			if (transition.From.IsEmpty() && transition.To == current->Name)
				continue;
			if (transition.ExitTime >= 0 && fraction < transition.ExitTime)
				continue;
			if (transition.Conditions.IsEmpty() && transition.ExitTime < 0)
				continue;
			bool passes = true;
			for (const AnimCondition& condition : transition.Conditions)
				passes = passes && Passes(condition);
			if (passes)
				return i;
		}
		return -1;
	}

	void AnimationStateMachine::Take(const AnimTransition& transition)
	{
		for (const AnimCondition& condition : transition.Conditions)
		{
			if (condition.Compare == AnimCompare::Triggered)
				ResetTrigger(condition.Parameter.View());
		}
		Play(transition.To.View(), transition.BlendTime);
	}
}
