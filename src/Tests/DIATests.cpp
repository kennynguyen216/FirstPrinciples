#include "Tests/TestFramework.h"
#include "Engine/Render/RayScene.h"
#include "Engine/Engine.h"
#include "Engine/Material.h"
#include "Engine/SceneSerializer.h"
#include "Engine/FieldSerializer.h"
#include "Core/Misc/File.h"
#include <filesystem>
#include <cstring>

#include <limits>

using namespace FP;

namespace
{
	RayInstance Instance(RefPtr<RayMesh> mesh, uint32 entity, Vec3 position = {}, Vec3 scale = { 1, 1, 1 })
	{
		RayInstance instance;
		instance.Identity = { 1, entity, 1 };
		instance.Mesh = std::move(mesh);
		instance.Transform = Mat4::TRS(position, Quat(), scale);
		return instance;
	}
}

FP_TEST(DIA_HitIdentityAndCoordinates)
{
	RayScene scene;
	FP_EXPECT(scene.Publish({ Instance(RayMesh::Build(Primitives::Quad()), 42) }));
	const RayHit hit = scene.Trace({ { 0.1f, 0.05f, 2 }, { 0, 0, -1 } });
	FP_EXPECT(hit.Class == RayResultClass::Surface);
	FP_EXPECT(hit.Identity.Entity == 42 && hit.Identity.Scene == 1);
	FP_EXPECT(std::fabs(hit.Distance - 2) < 1e-5f);
	FP_EXPECT((hit.Position - Vec3(0.1f, 0.05f, 0)).Length() < 1e-5f);
	FP_EXPECT(std::fabs(hit.Barycentrics.X + hit.Barycentrics.Y + hit.Barycentrics.Z - 1) < 1e-5f);
	FP_EXPECT(hit.UV.X > 0 && hit.UV.X < 1 && hit.UV.Y > 0 && hit.UV.Y < 1);
}

FP_TEST(DIA_CameraViewPreservesUnparentedRotationBits)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity camera = world->CreateEntity("Camera");
	auto& settings = world->Add<CameraComponent>(camera);
	settings.Perspective = true;
	settings.FieldOfView = 55;
	auto& transform = world->Get<TransformComponent>(camera);
	transform.Position = { .2f, 0, 5 };
	const auto sameView = [](const RenderView& a, const RenderView& b)
	{
		FP_EXPECT(std::memcmp(a.View.M, b.View.M, sizeof(a.View.M)) == 0);
		FP_EXPECT(std::memcmp(a.Projection.M, b.Projection.M, sizeof(a.Projection.M)) == 0);
		FP_EXPECT(std::memcmp(&a.CameraPosition, &b.CameraPosition, sizeof(Vec3)) == 0);
		FP_EXPECT(std::memcmp(&a.CameraForward, &b.CameraForward, sizeof(Vec3)) == 0);
		FP_EXPECT(std::memcmp(&a.CameraRight, &b.CameraRight, sizeof(Vec3)) == 0);
		FP_EXPECT(std::memcmp(&a.CameraUp, &b.CameraUp, sizeof(Vec3)) == 0);
	};
	for (float yaw : { 6.f, 5.94866917f, 5.79555496f, 5.5432772f, 5.19615242f, 4.76012004f,
		4.24264069f, 3.65256857f, 3.f, 2.29610059f, 1.55291427f, .783157153f })
	{
		transform.Rotation = { 0, yaw, 0 };
		const RenderView view = RenderView::FromWorld(*world.Get(), 960, 720);
		sameView(view, RenderView::FromCamera(transform, settings, 960, 720));
		FP_EXPECT(view.Camera == camera);
	}
	const Entity parent = world->CreateEntity("Parent");
	world->Get<TransformComponent>(parent).Position = { 2, -1, .5f };
	world->Get<TransformComponent>(parent).Rotation = { 10, 30, -7 };
	world->Get<TransformComponent>(parent).Scale = { 2, .5f, 3 };
	FP_EXPECT(world->SetParent(camera, parent, false));
	const RenderView view = RenderView::FromWorld(*world.Get(), 960, 720);
	sameView(view, RenderView::FromCamera(world->GetWorldTransform(camera), settings, 960, 720));
	FP_EXPECT((view.CameraPosition - world->GetWorldPosition(camera)).Length() < 1e-6f);
	FP_EXPECT((view.CameraForward - world->GetWorldRotation(camera).Rotate({ 0, 0, -1 })).Length() < 1e-6f);
}

FP_TEST(DIA_SharedTriangleEdgesHaveNoCracks)
{
	RayScene scene;
	FP_EXPECT(scene.Publish({ Instance(RayMesh::Build(Primitives::Quad()), 1, {}, { 100, 100, 1 }) }));
	TransformComponent transform;
	transform.Position.Z = 5;
	CameraComponent camera;
	camera.Perspective = true;
	const RenderView view = RenderView::FromCamera(transform, camera, 64, 64);
	for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
		FP_EXPECT(scene.Trace(view.ScreenToRay({ x + 0.5f, y + 0.5f })).Class == RayResultClass::Surface);
}

FP_TEST(DIA_MeshSectionsKeepLocalTriangleIdentity)
{
	const MeshData data = Primitives::Quad();
	const auto left = RayMesh::Build(data, 0, 3);
	const auto right = RayMesh::Build(data, 3, 3);
	FP_EXPECT(left && right && left->SourceTriangleCount == 1 && right->SourceTriangleCount == 1);
	FP_EXPECT(left->Triangles.Num() == 1 && right->Triangles.Num() == 1);
	FP_EXPECT(left->Triangles[0].Primitive == 0 && right->Triangles[0].Primitive == 0);
	auto first = Instance(left, 1);
	auto second = Instance(right, 2);
	first.Material = 7;
	second.Material = 11;
	RayScene scene;
	FP_EXPECT(scene.Publish({ first, second }));
	const auto leftHit = scene.Trace({ { -.25f, -.25f, 2 }, { 0, 0, -1 } });
	const auto rightHit = scene.Trace({ { .25f, .25f, 2 }, { 0, 0, -1 } });
	FP_EXPECT(leftHit.Identity.Entity == 1 && scene.GetInstances()[static_cast<int32>(leftHit.Instance)].Material == 7);
	FP_EXPECT(rightHit.Identity.Entity == 2 && scene.GetInstances()[static_cast<int32>(rightHit.Instance)].Material == 11);
	FP_EXPECT(!RayMesh::Build(data, 1, 3));
	FP_EXPECT(!RayMesh::Build(data, 3, 4));
	FP_EXPECT(!RayMesh::Build(data, 6, 3));
}

FP_TEST(DIA_FiniteLightSegment)
{
	const auto quad = RayMesh::Build(Primitives::Quad());
	RayScene scene;
	FP_EXPECT(scene.Publish({ Instance(quad, 1, { 0, 0, 3 }) }));
	FP_EXPECT(scene.IsVisible({ 0, 0, 0 }, { 0, 0, 2 }, { 0, 0, 1 }));
	FP_EXPECT(scene.Publish({ Instance(quad, 1, { 0, 0, 1 }) }));
	FP_EXPECT(!scene.IsVisible({ 0, 0, 0 }, { 0, 0, 2 }, { 0, 0, 1 }));
}

FP_TEST(DIA_AncestorBoundsMatchSegmentTraversal)
{
	const auto verify = [](const Array<RayNode>& source, uint32 count)
	{
		for (uint32 offset : { 0u, 17u })
		{
			const uint32 first = offset * 3;
			Array<RayNode> nodes;
			for (uint32 i = 0; i < offset; ++i) nodes.Add({});
			for (RayNode node : source)
			{
				node.First += first;
				node.Right += offset;
				node.Escape += offset;
				if (!node.Count && offset) node.Right |= 0x80000000u;
				nodes.Add(node);
			}
			for (const Ray ray : { Ray{ { -1, .125f, .125f }, { 1, 0, 0 } },
				Ray{ { .25f, .25f, -1 }, { 0, 0, 1 } }, Ray{ { -1, 100, 0 }, { 1, 0, 0 } } })
				for (float maximum : { .0001f, .5f, 1.f, 3.f, 10000.f })
				{
					const auto boundsHit = [&](const RayNode& node)
					{
						return RayBounds{ node.Min.XYZ(), node.Max.XYZ() }.Intersect(ray, .0001f, maximum);
					};
					Array<uint8> reached;
					for (uint32 i = 0; i < count; ++i) reached.Add(0);
					for (uint32 index = offset; index < static_cast<uint32>(nodes.Num());)
					{
						const auto& node = nodes[static_cast<int32>(index)];
						if (!boundsHit(node)) { index = node.Escape; continue; }
						for (uint32 i = node.First; i < node.First + node.Count; ++i) reached[static_cast<int32>(i - first)] = 1;
						++index;
					}
					for (uint32 target = first; target < first + count; ++target)
					{
						uint32 index = offset, rangeFirst = first, rangeCount = count;
						bool hit = true;
						for (uint32 depth = 0; depth <= 16; ++depth)
						{
							const auto& node = nodes[static_cast<int32>(index)];
							if (!boundsHit(node)) { hit = false; break; }
							if (node.Count)
							{
								FP_EXPECT(node.First == rangeFirst && node.Count == rangeCount);
								FP_EXPECT(target >= node.First && target < node.First + node.Count);
								break;
							}
							FP_EXPECT(depth < 16 && rangeCount > 4);
							const uint32 leftCount = rangeCount / 2;
							if (target < rangeFirst + leftCount) { ++index; rangeCount = leftCount; }
							else { index = node.Right & 0x7fffffffu; rangeFirst += leftCount; rangeCount -= leftCount; }
						}
						FP_EXPECT(hit == (reached[static_cast<int32>(target - first)] != 0));
					}
				}
		}
	};
	Array<uint32> counts;
	for (uint32 count = 1; count <= 64; ++count) counts.Add(count);
	for (uint32 count : { 127u, 128u, 129u, 255u, 256u, 257u, 1023u, 4096u }) counts.Add(count);
	const auto quad = RayMesh::Build(Primitives::Quad());
	for (uint32 count : counts)
	{
		MeshData data;
		for (uint32 i = 0; i < count; ++i)
		{
			const float x = static_cast<float>((i * 1031u) % count);
			for (Vec3 position : { Vec3{ x, 0, 0 }, Vec3{ x + .5f, 0, 0 }, Vec3{ x, .5f, .25f } })
			{
				data.Indices.Add(static_cast<uint32>(data.Vertices.Num()));
				data.Vertices.Add({ position, { 0, 0, 1 }, {}, { 1, 0, 0, 1 } });
			}
		}
		const auto mesh = RayMesh::Build(data);
		FP_EXPECT(mesh && mesh->Triangles.Num() == static_cast<int32>(count));
		verify(mesh->Nodes, count);
		if (count > RayScene::MaxInstances) continue;
		Array<RayInstance> instances;
		for (uint32 i = 0; i < count; ++i)
			instances.Add(Instance(quad, i + 1, { static_cast<float>((i * 113u) % count), 0, 0 }));
		RayScene scene;
		FP_EXPECT(scene.Publish(std::move(instances)));
		verify(scene.GetNodes(), count);
	}
}

FP_TEST(DIA_OpaqueVisibilityStopsAtFirstBlocker)
{
	const auto quad = RayMesh::Build(Primitives::Quad());
	Array<RayInstance> instances;
	for (uint32 i = 0; i < 16; ++i) instances.Add(Instance(quad, i + 1, { 0, 0, static_cast<float>(i) }));
	RayScene scene;
	FP_EXPECT(scene.Publish(instances));
	const Ray ray{ { 0.1f, 0.05f, 20 }, { 0, 0, -1 } };
	FP_EXPECT(scene.Trace(ray, 100, true, 5).Class == RayResultClass::Surface);
	FP_EXPECT(scene.Trace(ray, 100, false, 5).Class == RayResultClass::Exhausted);
}

FP_TEST(DIA_CurrentRigidGeometry)
{
	const auto quad = RayMesh::Build(Primitives::Quad());
	RayScene scene;
	FP_EXPECT(scene.Publish({ Instance(quad, 1) }));
	const Ray ray{ { 0, 0, 2 }, { 0, 0, -1 } };
	FP_EXPECT(scene.Trace(ray).Class == RayResultClass::Surface);
	FP_EXPECT(scene.Publish({ Instance(quad, 1, { 4, 0, 0 }) }));
	FP_EXPECT(scene.Trace(ray).Class == RayResultClass::Escaped);
	FP_EXPECT(scene.Publish({}));
	FP_EXPECT(scene.Trace(ray).Class == RayResultClass::Escaped);
	FP_EXPECT(quad->Triangles.Num() == 2);
}

FP_TEST(DIA_MirroredNonuniformTransform)
{
	RayScene scene;
	FP_EXPECT(scene.Publish({ Instance(RayMesh::Build(Primitives::Quad()), 7, { 0, 0, -3 }, { -4, 2, 0.5f }) }));
	const RayHit hit = scene.Trace({ { 1, 0.5f, 2 }, { 0, 0, -1 } });
	FP_EXPECT(hit.Class == RayResultClass::Surface);
	FP_EXPECT(std::fabs(hit.Distance - 5) < 1e-4f);
	FP_EXPECT(hit.GeometricNormal.Z < -0.99f);
	FP_EXPECT(Vec3::Dot(hit.ShadingNormal, hit.GeometricNormal) > 0.99f);
}

FP_TEST(DIA_UnknownCoverageDoesNotBecomeSky)
{
	RayScene scene;
	FP_EXPECT(scene.Publish({ Instance(RayMesh::Build(Primitives::Cube()), 1) }, false));
	FP_EXPECT(scene.Trace({ { 3, 3, 3 }, { 0, 0, 1 } }).Class == RayResultClass::UnknownCoverage);
	FP_EXPECT(!scene.IsVisible({ 3, 3, 3 }, { 3, 3, 4 }, { 0, 0, 1 }));
	FP_EXPECT(scene.Publish({ Instance(RayMesh::Build(Primitives::Cube()), 1, { 0, 0, -10 }) }));
	FP_EXPECT(scene.Trace({ { 0, 0, 0 }, { 0, 0, -1 } }, 5).Class == RayResultClass::UnknownCoverage);
	FP_EXPECT(scene.Trace({ { 0, 0, 0 }, { 0, 0, -1 } }, 5, true).Class == RayResultClass::ClearSegment);
}

FP_TEST(DIA_TraversalExhaustionIsExplicit)
{
	RayScene scene;
	FP_EXPECT(scene.Publish({ Instance(RayMesh::Build(Primitives::Cube()), 1) }));
	FP_EXPECT(scene.Trace({ { 0, 0, 2 }, { 0, 0, -1 } }, 128, false, 1).Class == RayResultClass::Exhausted);
}

FP_TEST(DIA_InvalidPublicationPreservesScene)
{
	const auto cube = RayMesh::Build(Primitives::Cube());
	RayScene scene;
	FP_EXPECT(scene.Publish({ Instance(cube, 9) }));
	const uint64 revision = scene.GetRevision();
	FP_EXPECT(!scene.Publish({ Instance(cube, 10, {}, { 0, 1, 1 }) }));
	FP_EXPECT(scene.GetRevision() == revision);
	FP_EXPECT(scene.Trace({ { 0, 0, 2 }, { 0, 0, -1 } }).Identity.Entity == 9);
	FP_EXPECT(!scene.Publish({ Instance(cube, 1), Instance(cube, 1) }));
	RayInstance invalid = Instance(cube, 3);
	invalid.Transform = Mat4::Translation({ std::numeric_limits<float>::quiet_NaN(), 0, 0 });
	FP_EXPECT(!scene.Publish({ invalid }));
}

FP_TEST(DIA_InvalidMeshAndQuery)
{
	MeshData data = Primitives::Quad();
	data.Indices[0] = 99999;
	FP_EXPECT(!RayMesh::Build(data));
	data = Primitives::Quad();
	data.Vertices[0].Position.X = std::numeric_limits<float>::infinity();
	FP_EXPECT(!RayMesh::Build(data));
	RayScene scene;
	FP_EXPECT(scene.Trace({ {}, { 0, 0, 0 } }).Class == RayResultClass::Invalid);
	FP_EXPECT(scene.Trace({ {}, { 0, 0, -1 } }, -1).Class == RayResultClass::Invalid);
}

FP_TEST(DIA_TinySourceGeometryUnderLargeTransform)
{
	MeshData data = Primitives::Quad();
	for (auto& vertex : data.Vertices) vertex.Position *= 0.00001f;
	const auto mesh = RayMesh::Build(data);
	FP_EXPECT(mesh && mesh->Triangles.Num() == 2);
	RayScene scene;
	FP_EXPECT(scene.Publish({ Instance(mesh, 1, {}, { 100000, 100000, 100000 }) }));
	FP_EXPECT(scene.Trace({ { 0.1f, 0.1f, 2 }, { 0, 0, -1 } }).Class == RayResultClass::Surface);
}

FP_TEST(DIA_PrimaryCameraClippingAndOrthographicVisibility)
{
	TransformComponent cameraTransform;
	cameraTransform.Position.Z = 5;
	CameraComponent camera;
	camera.Perspective = true;
	camera.FarClip = 10;
	const auto mesh = RayMesh::Build(Primitives::Quad());
	RayScene scene;
	FP_EXPECT(scene.Publish({ Instance(mesh, 1, { 0, 0, 4.95f }), Instance(mesh, 2), Instance(mesh, 3, { 0, 0, -20 }) }));
	float maximum;
	const Ray ray = RenderView::FromCamera(cameraTransform, camera, 64, 64).ScreenToRay({ 32, 32 }, &maximum);
	FP_EXPECT(scene.Trace(ray, maximum).Identity.Entity == 2);
	FP_EXPECT(maximum > 9.89f && maximum < 9.91f);
	FP_EXPECT(scene.Publish({ Instance(mesh, 2) }));
	const Ray orthographic = RenderView::Orthographic2D({}, 64, 64, 64).ScreenToRay({ 32, 32 }, &maximum);
	FP_EXPECT(scene.Trace(orthographic, maximum).Class == RayResultClass::Surface);
}

FP_TEST(DIA_BvhMatchesBruteInstanceDistances)
{
	const auto cube = RayMesh::Build(Primitives::Cube());
	Array<RayInstance> instances;
	for (uint32 i = 0; i < 64; ++i)
		instances.Add(Instance(cube, i + 1, { static_cast<float>(i % 8) * 2, static_cast<float>(i / 8) * 2, -static_cast<float>(i) * 0.1f }));
	RayScene scene;
	FP_EXPECT(scene.Publish(instances));
	for (uint32 i = 0; i < 64; ++i)
	{
		const RayHit hit = scene.Trace({ { static_cast<float>(i % 8) * 2, static_cast<float>(i / 8) * 2, 5 }, { 0, 0, -1 } });
		FP_EXPECT(hit.Class == RayResultClass::Surface && hit.Identity.Entity == i + 1);
		FP_EXPECT(std::fabs(hit.Distance - (4.5f + static_cast<float>(i) * 0.1f)) < 1e-4f);
	}
}

FP_TEST(DIA_AnalyticPunctualAndSpotResponse)
{
	for (float distance : { 0.1f, 1.0f, 2.0f, 10.0f })
	{
		const float ratio = distance / 100.0f;
		const float cutoff = 1 - ratio * ratio * ratio * ratio;
		const float expected = cutoff * cutoff / (distance * distance);
		FP_EXPECT(std::fabs(PunctualAttenuation(distance, 100) / expected - 1) < 1e-5f);
	}
	FP_EXPECT(PunctualAttenuation(100, 100) == 0);
	FP_EXPECT(PunctualAttenuation(101, 100) == 0);
	FP_EXPECT(std::isfinite(PunctualAttenuation(0, 10)));
	FP_EXPECT(SpotAttenuation(1, 20, 30) == 1);
	FP_EXPECT(SpotAttenuation(std::cos(40 * DegToRad), 20, 30) == 0);
	FP_EXPECT(SpotAttenuation(1, 30, 20) == 0);
}

FP_TEST(DIA_TransportSamplesExcludeEndpoints)
{
	for (const uint32 bits : { 0u, 1u, 0x7fffffffu, 0xfffffffeu, 0xffffffffu })
	{
		const float value = TransportUnitSample(bits);
		FP_EXPECT(value > 0 && value < 1 && std::isfinite(value));
	}
}

FP_TEST(DIA_PersistenceRejectsInvalidCandidate)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity original = world->CreateEntity("Preserved");
	const uint64 generation = world->GetSceneGeneration();
	const String before = SceneSerializer::SaveToString(*world.Get());
	for (const char* value : { "nan", "inf", "1e1000", "oops", "1 2", "-1" })
	{
		const String candidate = String("version 2\nentity 00000000-0000-4000-8000-000000000001\ncomponent PointLight\nIntensity ") + value + "\n";
		FP_EXPECT(!SceneSerializer::LoadFromString(*world.Get(), candidate.View()));
		FP_EXPECT(world->IsValid(original));
		FP_EXPECT(SceneSerializer::SaveToString(*world.Get()) == before);
		FP_EXPECT(world->GetSceneGeneration() == generation);
	}
	FP_EXPECT(!SceneSerializer::LoadFromString(*world.Get(), "version 99\n"));
	FP_EXPECT(!SceneSerializer::LoadFromString(*world.Get(), "entity invalid-id\n"));
	FP_EXPECT(!SceneSerializer::LoadFromString(*world.Get(), "entity 00000000-0000-4000-8000-000000000001\ncomponent SpotLight\nInnerAngle 50\nOuterAngle 20\n"));
	FP_EXPECT(!SceneSerializer::LoadFromString(*world.Get(), "entity 00000000-0000-4000-8000-000000000001\ncomponent Transform\nScale 1 0 1\n"));
	FP_EXPECT(!SceneSerializer::LoadFromString(*world.Get(), "version 2\nentity 00000000-0000-4000-8000-000000000001\ncomponent Name\nName \"unterminated\n"));
}

FP_TEST(DIA_LightingFieldsRoundTrip)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	Entity light = world->CreateEntity("Spot");
	SpotLightComponent& spot = world->Add<SpotLightComponent>(light);
	spot.Intensity = 25;
	spot.OuterAngle = 42;
	world->Add<DIAComponent>(light).SkyIntensity = 0.03f;
	const String text = SceneSerializer::SaveToString(*world.Get());
	FP_EXPECT(SceneSerializer::LoadFromString(*world.Get(), text.View()));
	light = world->FindEntityByName("Spot");
	FP_EXPECT(world->Get<SpotLightComponent>(light).OuterAngle == 42);
	FP_EXPECT(world->Get<DIAComponent>(light).SkyIntensity == 0.03f);
	FP_EXPECT(SceneSerializer::SaveToString(*world.Get()) == text);
}

FP_TEST(Shadow_LightSettingsRoundTrip)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	Entity entity = world->CreateEntity("Shadow lights");
	auto& point = world->Add<PointLightComponent>(entity);
	auto& spot = world->Add<SpotLightComponent>(entity);
	auto& directional = world->Add<DirectionalLightComponent>(entity);
	FP_EXPECT(point.CastShadows && spot.CastShadows && directional.CastShadows);
	point.CastShadows = spot.CastShadows = directional.CastShadows = true;
	const String text = SceneSerializer::SaveToString(*world.Get());
	FP_EXPECT(SceneSerializer::LoadFromString(*world.Get(), text.View()));
	entity = world->FindEntityByName("Shadow lights");
	FP_EXPECT(world->Get<PointLightComponent>(entity).CastShadows);
	FP_EXPECT(world->Get<SpotLightComponent>(entity).CastShadows);
	FP_EXPECT(world->Get<DirectionalLightComponent>(entity).CastShadows);
	FP_EXPECT(SceneSerializer::SaveToString(*world.Get()) == text);
	world->Get<PointLightComponent>(entity).Range = 0;
	FP_EXPECT(!SceneSerializer::Validate(*world.Get()));
	world->Get<PointLightComponent>(entity).CastShadows = false;
	world->Get<PointLightComponent>(entity).Range = 0.005f;
	FP_EXPECT(SceneSerializer::Validate(*world.Get()));
}

FP_TEST(DIA_MaterialValidationAndAtomicFiles)
{
	MaterialData data;
	FP_EXPECT(ValidateMaterial(data));
	data.Metallic = 2;
	FP_EXPECT(!ValidateMaterial(data));
	data.Metallic = 0;
	data.Roughness = std::numeric_limits<float>::quiet_NaN();
	FP_EXPECT(!ValidateMaterial(data));
	const auto native = std::filesystem::temp_directory_path() / std::filesystem::path(u8"fp-lighting-\u00e9-\u5149.txt");
	const auto utf8 = native.u8string();
	const String path(reinterpret_cast<const char*>(utf8.data()), static_cast<int32>(utf8.size()));
	FP_EXPECT(File::WriteAllText(path.CStr(), "first"));
	FP_EXPECT(File::WriteAllText(path.CStr(), "second"));
	String text;
	FP_EXPECT(File::ReadAllText(path.CStr(), text));
	FP_EXPECT(text == "second");
	std::error_code error;
	std::filesystem::remove(native, error);
	FP_EXPECT(!error);
}
