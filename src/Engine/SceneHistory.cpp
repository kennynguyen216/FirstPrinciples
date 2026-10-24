#include "Engine/SceneHistory.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Material.h"

#include <cstring>

namespace FP
{
	namespace
	{
		template<typename T>
		void SortByPath(Array<T>& items)
		{
			items.Sort([](const T& a, const T& b) { return std::strcmp(a.Path.CStr(), b.Path.CStr()) < 0; });
		}
	}

	void SceneHistory::Reset(const Guid& selection)
	{
		m_Undo.Clear();
		m_Redo.Clear();
		m_Current = Capture(selection);
	}

	bool SceneHistory::Commit(const Guid& selection)
	{
		State now = Capture(selection);
		// A material loaded since the last step had not been edited before, so its file is what undo goes back to.
		for (const MaterialText& material : now.Materials)
		{
			if (FindMaterial(m_Current, material.Path))
				continue;
			RefPtr<Material> original = MakeRef<Material>(material.Path);
			if (original->Load())
				m_Current.Materials.Add({ material.Path, original->SaveToString() });
		}
		SortByPath(m_Current.Materials);
		if (now.Scene == m_Current.Scene && SameMaterials(now, m_Current))
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
		State state;
		state.Scene = SceneSerializer::SaveToString(m_World);
		for (const Material* material : MaterialLibrary::Get().GetLoaded())
			state.Materials.Add({ material->GetPath(), material->SaveToString() });
		SortByPath(state.Materials);
		state.Selection = selection;
		return state;
	}

	void SceneHistory::Restore(const State& state)
	{
		SceneSerializer::LoadFromString(m_World, state.Scene.View());
		for (const MaterialText& saved : state.Materials)
		{
			Material* material = MaterialLibrary::Get().Load(saved.Path);
			if (!material || material->SaveToString() == saved.Text)
				continue;
			material->LoadFromString(saved.Text.View());
			material->MarkDirty(); // differs from the file now, so saving the scene writes it
		}
	}

	const SceneHistory::MaterialText* SceneHistory::FindMaterial(const State& state, const String& path)
	{
		for (const MaterialText& material : state.Materials)
		{
			if (material.Path == path)
				return &material;
		}
		return nullptr;
	}

	bool SceneHistory::SameMaterials(const State& a, const State& b)
	{
		if (a.Materials.Num() != b.Materials.Num())
			return false;
		for (int32 i = 0; i < a.Materials.Num(); ++i)
		{
			if (a.Materials[i].Path != b.Materials[i].Path || a.Materials[i].Text != b.Materials[i].Text)
				return false;
		}
		return true;
	}
}
