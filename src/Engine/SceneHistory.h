#pragma once

#include "Engine/World.h"

namespace FP
{
	// Undo and redo for the editor. Each step is the whole scene saved as text,
	// plus the text of every loaded material because the inspector edits those too.
	// Scenes are small, so a full copy per step is simpler than recording every kind of edit
	// and new components and fields work with no extra code.
	class SceneHistory
	{
	public:
		// Oldest steps are dropped past this.
		static constexpr int32 MaxSteps = 100;

		explicit SceneHistory(World& world) : m_World(world) {}

		// Forgets every step and starts from the world as it is now.
		void Reset(const Guid& selection = Guid());

		// Records a step when the world changed since the last one. Returns true when it did.
		// selection is what is selected after the change.
		bool Commit(const Guid& selection);

		// The selection that goes with the current step. Set it when the selection
		// changes with no edit in between so undo brings it back.
		void SetSelection(const Guid& selection) { m_Current.Selection = selection; }
		const Guid& GetSelection() const { return m_Current.Selection; }

		// Put the world back one step. Uncommitted changes are lost, so commit first.
		bool Undo();
		bool Redo();

		bool CanUndo() const { return !m_Undo.IsEmpty(); }
		bool CanRedo() const { return !m_Redo.IsEmpty(); }

	private:
		struct MaterialText
		{
			String Path;
			String Text;
		};

		struct State
		{
			String Scene;
			Array<MaterialText> Materials; // sorted by path
			Guid Selection;
		};

		State Capture(const Guid& selection) const;
		static const MaterialText* FindMaterial(const State& state, const String& path);
		static bool SameMaterials(const State& a, const State& b);
		void Restore(const State& state);

		World& m_World;
		State m_Current;
		Array<State> m_Undo;
		Array<State> m_Redo;
	};
}
