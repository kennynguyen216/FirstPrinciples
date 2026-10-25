#include "Editor/MeshletDebugWidget.h"
#include "imgui.h"
#include "backends/imgui_impl_sdlgpu3.h"
#include <QDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace FP::Editor
{
	namespace
	{
		struct ContextScope
		{
			ImGuiContext* Previous = ImGui::GetCurrentContext();
			explicit ContextScope(ImGuiContext* context) { ImGui::SetCurrentContext(context); }
			~ContextScope() { ImGui::SetCurrentContext(Previous); }
		};
	}
	MeshletDebugWidget::MeshletDebugWidget(ViewportWidget& viewport, QWidget* parent)
		: QWidget(parent), m_Viewport(&viewport), m_Device(viewport.GetRenderer()->GetDevice())
	{
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);
		m_PreviousCameraContext = viewport.GetMeshletCameraContext();
		viewport.SetMeshletCameraContext(true);
		ImGuiContext* previous = ImGui::GetCurrentContext();
		m_Context = ImGui::CreateContext();
		ImGui::GetIO().IniFilename = nullptr;
		ImGui::StyleColorsDark();
		// The camera target is sRGB. Render the debugger into sRGB as well, converting
		// ImGui's authored UI colors to linear so both the UI and preview match Qt.
		for (auto& color : ImGui::GetStyle().Colors)
		{
			auto linear = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
			color.x = linear(color.x); color.y = linear(color.y); color.z = linear(color.z);
		}
		ImGui_ImplSDLGPU3_InitInfo info;
		info.Device = m_Device.GetHandle();
		info.ColorTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
		m_Ready = ImGui_ImplSDLGPU3_Init(&info);
		ImGui::SetCurrentContext(previous);
		auto* timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this]() { if (isVisible()) RenderFrame(); });
		timer->start(33);
	}
	MeshletDebugWidget::~MeshletDebugWidget()
	{
		if (m_Viewport) m_Viewport->SetMeshletCameraContext(m_PreviousCameraContext);
		SDL_WaitForGPUIdle(m_Device.GetHandle());
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_Context);
		if (m_Ready) ImGui_ImplSDLGPU3_Shutdown();
		ImGui::DestroyContext(m_Context);
		ImGui::SetCurrentContext(previous == m_Context ? nullptr : previous);
	}
	void MeshletDebugWidget::RenderFrame()
	{
		if (!m_Ready || !m_Viewport || width() < 1 || height() < 1) return;
		ContextScope scope(m_Context);
		Renderer& renderer = *m_Viewport->GetRenderer();
		auto& settings = renderer.GetMeshletSettings();
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(static_cast<float>(width()), static_cast<float>(height()));
		io.DisplayFramebufferScale = ImVec2(static_cast<float>(devicePixelRatioF()), static_cast<float>(devicePixelRatioF()));
		io.DeltaTime = 1.0f / 30.0f;
		ImGui_ImplSDLGPU3_NewFrame();
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("Nanite / meshlet visibility", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ImGui::TextUnformatted("MESHLET VISIBILITY");
		ImGui::TextWrapped("Clusters are built automatically at mesh upload: up to %u vertices / %u triangles per cluster.", MeshletMaxVertices, MeshletMaxTriangles);
		if (!renderer.HasMeshletCulling()) ImGui::TextColored(ImVec4(1, 0.5f, 0.2f, 1), "Compute unavailable: using full mesh draws.");
		ImGui::Checkbox("Enable meshlet rendering", &settings.Enabled);
		ImGui::Checkbox("Frustum culling", &settings.Frustum); ImGui::SameLine();
		ImGui::Checkbox("Compute occlusion", &settings.Occlusion);
		ImGui::Checkbox("Color meshlets", &settings.Colorize); ImGui::SameLine();
		ImGui::Checkbox("Freeze culling camera", &settings.FreezeCamera);
		bool cameraContext = m_Viewport->GetMeshletCameraContext();
		if (ImGui::Checkbox("Show outside camera", &cameraContext)) m_Viewport->SetMeshletCameraContext(cameraContext);
		if (cameraContext) ImGui::SliderFloat("Observer width", &m_Viewport->GetMeshletObserverWidth(), 1.05f, 2.0f, "%.2fx");
		ImGui::TextWrapped("Yellow frame = the culling camera. The wider view shows clusters crossing its edges live. Move the camera with WASD + right mouse in the viewport.");
		ImGui::TextWrapped("Freeze, then move the editor camera to inspect what the captured camera keeps. Unfreeze to resume live visibility.");
		if (settings.Enabled && renderer.GetActiveMeshlets())
		{
			const auto& stats = renderer.GetMeshletStats();
			ImGui::Text("GPU snapshot: %u visible / %u meshlets", stats.Visible, stats.Total);
			ImGui::Text("Rejected: %u frustum / %u occluded", stats.Frustum, stats.Occluded);
			ImGui::Text("Main-pass triangles: %llu / %llu", static_cast<unsigned long long>(stats.VisibleTriangles), static_cast<unsigned long long>(stats.Triangles));
		}
		else ImGui::TextUnformatted("No active meshlet draws. Full mesh rendering or no static meshes.");
		ImGui::TextWrapped("Static opaque meshes use indirect draws. Skinning and glass keep full draws. Cutouts do not act as occluders. Occlusion is off in wireframe.");
		ImGui::Separator();
		ImGui::TextUnformatted("DRAW BATCHING");
		bool batching = renderer.IsBatchingEnabled();
		if (ImGui::Checkbox("Batch compatible draws", &batching)) renderer.SetBatchingEnabled(batching);
		const auto& draws = renderer.GetRenderStats();
		ImGui::Text("Scene: %u submissions / %u section instances", draws.SceneDrawCalls, draws.SceneInstances);
		ImGui::Text("Instanced groups: %u / indirect commands: %u", draws.InstancedDrawCalls, draws.IndirectCommands);
		ImGui::Text("Depth submissions: %u shadow / %u occlusion", draws.ShadowDrawCalls, draws.OcclusionDrawCalls);
		ImGui::TextWrapped("Compatible neighbors share a draw. Meshlet commands keep visibility per instance. Animated poses keep separate draws.");
		ImGui::Separator();
		ImGui::TextUnformatted(cameraContext ? "Live observer view (yellow frame = culling camera)" : "Live viewport camera (edit camera, or game camera during Play)");
		if (auto* preview = m_Viewport->GetRenderTarget())
		{
			const float aspect = static_cast<float>(preview->GetWidth()) / preview->GetHeight();
			const ImVec2 room = ImGui::GetContentRegionAvail();
			const float w = std::max(1.0f, std::min(room.x, room.y * aspect));
			ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(preview->GetHandle())), ImVec2(w, w / aspect));
		}
		ImGui::End();
		ImGui::Render();
		const uint32 w = static_cast<uint32>(width() * devicePixelRatioF()), h = static_cast<uint32>(height() * devicePixelRatioF());
		if (!m_Target || m_Target->GetWidth() != w || m_Target->GetHeight() != h)
		{
			m_Target = m_Device.CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, w, h, false);
			m_Image = QImage(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGBA8888);
			m_Image.setDevicePixelRatio(devicePixelRatioF());
		}
		if (!m_Target) return;
		auto* cmd = SDL_AcquireGPUCommandBuffer(m_Device.GetHandle());
		if (!cmd) return;
		ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);
		SDL_GPUColorTargetInfo target{};
		target.texture = m_Target->GetHandle(); target.load_op = SDL_GPU_LOADOP_CLEAR; target.store_op = SDL_GPU_STOREOP_STORE;
		auto* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
		ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd, pass);
		SDL_EndGPURenderPass(pass);
		SDL_SubmitGPUCommandBuffer(cmd);
		m_Device.ReadPixels(m_Target->GetHandle(), w, h, m_Image.bits());
		update();
	}
	void MeshletDebugWidget::paintEvent(QPaintEvent*) { QPainter p(this); p.fillRect(rect(), Qt::black); p.drawImage(QPointF(0, 0), m_Image); }
	void MeshletDebugWidget::mouseMoveEvent(QMouseEvent* e)
	{
		ContextScope scope(m_Context);
		ImGui::GetIO().AddMousePosEvent(static_cast<float>(e->position().x()), static_cast<float>(e->position().y()));
	}
	void MeshletDebugWidget::MouseButton(QMouseEvent* e, bool down)
	{
		mouseMoveEvent(e);
		ContextScope scope(m_Context);
		int button = e->button() == Qt::LeftButton ? 0 : e->button() == Qt::RightButton ? 1 : 2;
		ImGui::GetIO().AddMouseButtonEvent(button, down);
	}
	void MeshletDebugWidget::mousePressEvent(QMouseEvent* e) { setFocus(); MouseButton(e, true); }
	void MeshletDebugWidget::mouseReleaseEvent(QMouseEvent* e) { MouseButton(e, false); }
	void MeshletDebugWidget::wheelEvent(QWheelEvent* e)
	{
		ContextScope scope(m_Context);
		ImGui::GetIO().AddMouseWheelEvent(e->angleDelta().x() / 120.0f, e->angleDelta().y() / 120.0f);
	}
	void MeshletDebugWidget::focusOutEvent(QFocusEvent*)
	{
		ContextScope scope(m_Context);
		for (int i = 0; i < 3; ++i) ImGui::GetIO().AddMouseButtonEvent(i, false);
	}
	void ShowMeshletDebugger(ViewportWidget& viewport, QWidget* parent)
	{
		if (auto* existing = parent->findChild<QDialog*>("MeshletDebugger")) { existing->show(); existing->raise(); return; }
		auto* dialog = new QDialog(parent);
		dialog->setObjectName("MeshletDebugger");
		dialog->setWindowTitle("Nanite / Meshlet Debugger (ImGui)");
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		auto* layout = new QVBoxLayout(dialog);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(new MeshletDebugWidget(viewport, dialog));
		dialog->resize(760, 780);
		dialog->show();
	}
}
