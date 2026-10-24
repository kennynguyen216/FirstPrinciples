#include "Engine/SceneHistory.h"
#include "Engine/SceneSerializer.h"

namespace FP
{
	void SceneHistory::Reset(const Guid& selection)
	{
		m_Undo.Clear();
		m_Redo.Clear();
		m_Current = Capture(selection);
	}

	bool SceneHistory::Commit(const Guid& selection)
	{
		State now = Capture(selection);
		if (now.Scene == m_Current.Scene)
			return false;
		if (m_Undo.Num() >= MaxSteps)
			m_Undo.RemoveAt(0);
		m_Undo.Add(std::move(m_Current));
		m_Current = std::move(now);
		m_Redo.Clear();
		return true;
	}

	bool SceneHistory::Undo()
	{
		if (m_Undo.IsEmpty())
			return false;
		m_Redo.Add(std::move(m_Current));
		m_Current = m_Undo.Pop();
		Restore(m_Current);
		return true;
	}

	bool SceneHistory::Redo()
	{
		if (m_Redo.IsEmpty())
			return false;
		m_Undo.Add(std::move(m_Current));
		m_Current = m_Redo.Pop();
		Restore(m_Current);
		return true;
	}

	SceneHistory::State SceneHistory::Capture(const Guid& selection) const
	{
		return { SceneSerializer::SaveToString(m_World), selection };
	}

	void SceneHistory::Restore(const State& state)
	{
		SceneSerializer::LoadFromString(m_World, state.Scene.View());
	}
}
