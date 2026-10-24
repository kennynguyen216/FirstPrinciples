#include "Tests/TestFramework.h"
#include "imgui.h"

FP_TEST(ImGui_DockingAndDemoProduceDrawData)
{
	FP_EXPECT(IMGUI_CHECKVERSION());
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1280.0f, 720.0f);
	io.DeltaTime = 1.0f / 60.0f;
	unsigned char* pixels = nullptr;
	int width = 0, height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	FP_EXPECT(pixels != nullptr && width > 0 && height > 0);

	// Windows finish sizing on the first frame, so check the next frame's geometry.
	for (int frame = 0; frame < 2; ++frame)
	{
		ImGui::NewFrame();
		FP_EXPECT(ImGui::DockSpaceOverViewport() != 0);
		ImGui::ShowDemoWindow();
		ImGui::Render();
	}
	const ImDrawData* drawData = ImGui::GetDrawData();
	FP_EXPECT(drawData != nullptr && drawData->Valid);
	FP_EXPECT(drawData != nullptr && drawData->TotalVtxCount > 0 && drawData->TotalIdxCount > 0);
	ImGui::DestroyContext();
}
