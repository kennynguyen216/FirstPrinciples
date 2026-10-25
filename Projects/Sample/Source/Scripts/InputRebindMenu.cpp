#include "Engine/Scripting/Script.h"

using namespace FP;

// Rebinds controls from an HTML menu and saves the player's overrides.
FPCLASS(Category = "UI")
class InputRebindMenu : public Script
{
public:
	FPPROPERTY()
	String SaveSlot = "input_bindings";

	void OnStart() override
	{
		Input::SetCursorMode(CursorMode::Visible);
		if (SaveData::Exists(SaveSlot.View()) && !Input::LoadActionBindings(SaveSlot.View()))
			m_Message = "Saved controls could not be loaded. Project bindings are still active.";
	}

	void OnHotReload() override
	{
		Input::CancelActionRebind();
		m_Ready = false;
	}

	void OnStop() override { Input::CancelActionRebind(); }

	void OnUpdate(float) override
	{
		UIDocument* ui = GetUI();
		if (!ui || !ui->IsReady()) return;
		if (!m_Ready) { Publish(); m_Ready = true; }
		const RebindStatus status = Input::GetActionRebindStatus();
		if (status != m_LastStatus)
		{
			if (status == RebindStatus::Complete) m_Message = String("Bound ") + String(Input::GetLastReboundInput().c_str());
			else if (status == RebindStatus::Cancelled) m_Message = "Rebinding cancelled.";
			m_LastStatus = status;
			Publish();
		}
		if (Input::WasActionPressed("Jump")) ui->Set("jumpCount", ++m_Jumps);
	}

	void OnUIEvent(std::string_view name, const Json& data) override
	{
		const String action = data["action"].AsString();
		std::vector<std::string> bindings = Input::GetActionBindings(action.View());
		const double number = data["index"].AsNumber(-1);
		const bool row = number >= 0 && number < bindings.size() && number == std::floor(number);
		if (name == "controls.rebind" && row)
		{
			if (Input::BeginActionRebind(action.View(), static_cast<size_t>(number), data["axes"].AsBool()))
				m_Message = "Press a new control. Escape cancels. Release it before trying Jump.";
		}
		else if (name == "controls.set" && row)
			m_Message = Input::RebindAction(action.View(), static_cast<size_t>(number), data["binding"].AsString().View()) ? "Binding updated." : "Invalid binding. Try Key:G or Button:South.";
		else if (name == "controls.remove" && row)
		{
			bindings.erase(bindings.begin() + static_cast<size_t>(number));
			Input::SetActionBindings(action.View(), bindings);
			m_Message = "Binding removed.";
		}
		else if (name == "controls.add")
		{
			bindings.push_back("Key:Space");
			m_Message = Input::SetActionBindings(action.View(), bindings) ? "Binding added. Press Rebind to replace it." : "Choose an action first.";
		}
		else if (name == "controls.reset")
		{
			Input::ResetActionBindings(action.View());
			m_Message = "Project bindings restored for this action.";
		}
		else if (name == "controls.resetAll")
		{
			Input::ResetAllActionBindings();
			m_Message = "Project bindings restored. Save to keep them.";
		}
		else if (name == "controls.save")
			m_Message = Input::SaveActionBindings(SaveSlot.View()) ? "Player controls saved." : "Could not save player controls.";
		else if (name == "controls.load")
			m_Message = Input::LoadActionBindings(SaveSlot.View()) ? "Saved player controls loaded." : "No valid saved controls. Current bindings are unchanged.";
		else return;
		Publish();
	}

private:
	void Publish()
	{
		UIDocument* ui = GetUI();
		if (!ui) return;
		std::string json = "[";
		for (const std::string& action : Input::GetActionNames())
		{
			if (json.size() > 1) json += ",";
			json += "{\"name\":" + UIDocument::ToJsonString(action) + ",\"bindings\":[";
			bool first = true;
			for (const std::string& binding : Input::GetActionBindings(action))
			{
				if (!first) json += ",";
				json += UIDocument::ToJsonString(binding);
				first = false;
			}
			json += "]}";
		}
		ui->SetJson("controls", json + "]");
		ui->Set("controlsStatus", m_Message.View());
		ui->Set("capturing", Input::GetActionRebindStatus() == RebindStatus::Listening);
		ui->Set("jumpCount", m_Jumps);
	}

	String m_Message = "Choose an action, then press Rebind. Changes take effect immediately.";
	RebindStatus m_LastStatus = RebindStatus::Idle;
	bool m_Ready = false;
	int32 m_Jumps = 0;
};

FP_SCRIPT(InputRebindMenu);
