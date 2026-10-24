#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/World.h"
#include "Engine/Render/MeshData.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace FP
{
	namespace
	{
		// Bodies that move only collide with bodies that move, plus the level. The level never
		// collides with itself, which saves the broad phase most of its work.
		namespace Layers
		{
			constexpr JPH::ObjectLayer Still = 0;
			constexpr JPH::ObjectLayer Moving = 1;
			constexpr JPH::ObjectLayer Count = 2;
		}

		class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
		{
		public:
			JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer)); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer.GetValue() == 0 ? "Still" : "Moving"; }
#endif
		};

		class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override
			{
				return layer == Layers::Moving || broad.GetValue() == Layers::Moving;
			}
		};

		class ObjectVsObject final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
			{
				return a == Layers::Moving || b == Layers::Moving;
			}
		};

		void Trace(const char* format, ...)
		{
			char buffer[1024];
			va_list args;
			va_start(args, format);
			std::vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			FP_LOG_VERBOSE("[Physics] %s", buffer);
		}

		// Jolt keeps a few things global, so it starts once for the whole program.
		void StartJolt()
		{
			static std::once_flag once;
			std::call_once(once, [] {
				JPH::RegisterDefaultAllocator();
				JPH::Trace = &Trace;
				JPH::Factory::sInstance = new JPH::Factory();
				JPH::RegisterTypes();
			});
		}

		JPH::Vec3 ToJolt(Vec3 v) { return JPH::Vec3(v.X, v.Y, v.Z); }
		JPH::Quat ToJolt(const Quat& q) { return JPH::Quat(q.X, q.Y, q.Z, q.W).Normalized(); }
		Vec3 FromJolt(JPH::Vec3Arg v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
		Quat FromJolt(JPH::QuatArg q) { return { q.GetX(), q.GetY(), q.GetZ(), q.GetW() }; }
		uint32 Key(entt::entity entity) { return static_cast<uint32>(entt::to_integral(entity)); }

		// Everything a body was built from, so a change in any of it rebuilds the body.
		struct Recipe
		{
			ColliderComponent Collider;
			RigidBodyComponent Body;
			bool HasBody = false;
			Vec3 Scale;
			String Mesh;

			bool operator==(const Recipe& o) const
			{
				const ColliderComponent& a = Collider;
				const ColliderComponent& b = o.Collider;
				const RigidBodyComponent& c = Body;
				const RigidBodyComponent& d = o.Body;
				return a.Shape == b.Shape.View() && a.Size == b.Size && a.Radius == b.Radius && a.Height == b.Height && a.Offset == b.Offset && a.Trigger == b.Trigger
					&& HasBody == o.HasBody && (!HasBody || (c.Motion == d.Motion.View() && c.Mass == d.Mass && c.Friction == d.Friction && c.Restitution == d.Restitution
					&& c.LinearDamping == d.LinearDamping && c.AngularDamping == d.AngularDamping && c.GravityScale == d.GravityScale && c.LockRotation == d.LockRotation
					&& c.Continuous == d.Continuous)) && (Scale - o.Scale).Length() < 1e-5f && Mesh == o.Mesh.View();
			}
		};

		struct Record
		{
			JPH::BodyID Id;
			Recipe Made;
			JPH::EMotionType Motion = JPH::EMotionType::Static;
			// Where this body last put the entity, to spot transforms moved by hand.
			Vec3 Position;
			Quat Rotation;
		};

		JPH::EMotionType MotionOf(const RigidBodyComponent* body)
		{
			if (!body || body->Motion == "Static")
				return JPH::EMotionType::Static;
			return body->Motion == "Kinematic" ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
		}

		JPH::ShapeRefC MakeShape(const Recipe& recipe, JPH::EMotionType motion)
		{
			const ColliderComponent& collider = recipe.Collider;
			const Vec3 scale{ std::fabs(recipe.Scale.X), std::fabs(recipe.Scale.Y), std::fabs(recipe.Scale.Z) };
			const float largest = std::max({ scale.X, scale.Y, scale.Z, 1e-4f });
			JPH::ShapeSettings::ShapeResult result;

			if (collider.Shape == "Sphere")
				result = JPH::SphereShapeSettings(std::max(collider.Radius * largest, 0.001f)).Create();
			else if (collider.Shape == "Capsule")
			{
				const float radius = std::max(collider.Radius * std::max(scale.X, scale.Z), 0.001f);
				const float halfCylinder = std::max(collider.Height * scale.Y * 0.5f - radius, 0.001f);
				result = JPH::CapsuleShapeSettings(halfCylinder, radius).Create();
			}
			else if (collider.Shape == "Mesh")
			{
				MeshData mesh;
				if (LoadMesh(recipe.Mesh.View(), mesh) && !mesh.Indices.IsEmpty())
				{
					if (motion == JPH::EMotionType::Static)
					{
						// Exact triangles for the level.
						JPH::VertexList vertices;
						for (const MeshVertex& v : mesh.Vertices)
							vertices.push_back(JPH::Float3(v.Position.X * scale.X, v.Position.Y * scale.Y, v.Position.Z * scale.Z));
						JPH::IndexedTriangleList triangles;
						for (int32 i = 0; i + 2 < mesh.Indices.Num(); i += 3)
							triangles.push_back(JPH::IndexedTriangle(mesh.Indices[i], mesh.Indices[i + 1], mesh.Indices[i + 2]));
						result = JPH::MeshShapeSettings(vertices, triangles).Create();
					}
					else
					{
						// Moving bodies need a solid shape, so they get the mesh's convex hull.
						JPH::Array<JPH::Vec3> points;
						for (const MeshVertex& v : mesh.Vertices)
							points.push_back(JPH::Vec3(v.Position.X * scale.X, v.Position.Y * scale.Y, v.Position.Z * scale.Z));
						result = JPH::ConvexHullShapeSettings(points).Create();
					}
				}
				if (!result.IsValid())
					FP_LOG_WARN("[Physics] Mesh collider %s could not be built, so it uses a box", recipe.Mesh.CStr());
			}

			if (!result.IsValid() || result.HasError())
			{
				// Box, and the fallback for anything that failed.
				const JPH::Vec3 half = JPH::Vec3::sMax(ToJolt(collider.Size * 0.5f) * ToJolt(scale), JPH::Vec3::sReplicate(0.005f));
				const float rounding = std::min(JPH::cDefaultConvexRadius, half.ReduceMin() * 0.5f);
				result = JPH::BoxShapeSettings(half, rounding).Create();
			}

			JPH::ShapeRefC shape = result.Get();
			if (collider.Offset.LengthSquared() > 0)
			{
				const Vec3 offset{ collider.Offset.X * scale.X, collider.Offset.Y * scale.Y, collider.Offset.Z * scale.Z };
				shape = JPH::RotatedTranslatedShapeSettings(ToJolt(offset), JPH::Quat::sIdentity(), shape).Create().Get();
			}
			return shape;
		}

		bool Near(Vec3 a, Vec3 b) { return (a - b).LengthSquared() < 1e-8f; }
		bool Near(const Quat& a, const Quat& b) { return std::fabs(Quat::Dot(a, b)) > 0.99999f; }
	}

	struct PhysicsWorld::Impl final : public JPH::ContactListener
	{
		BroadPhaseLayers BroadPhase;
		ObjectVsBroadPhase ObjectVsBroad;
		ObjectVsObject ObjectPairs;
		std::unique_ptr<JPH::TempAllocatorImpl> Temp;
		std::unique_ptr<JPH::JobSystemThreadPool> Jobs;
		std::unique_ptr<JPH::PhysicsSystem> System;
		std::unordered_map<uint32, Record> Bodies;
		std::unordered_map<uint32, entt::entity> BodyToEntity; // by body id
		float Accumulator = 0;

		// Contacts arrive on worker threads during a step and are counted per body pair here.
		struct Raw
		{
			bool Added;
			JPH::BodyID A;
			JPH::BodyID B;
			bool Sensor;
			Vec3 Point;
			Vec3 Normal;
		};
		std::mutex Lock;
		std::vector<Raw> Incoming;
		struct Pair
		{
			int32 Contacts = 0;
			bool Sensor = false;
		};
		std::unordered_map<uint64, Pair> Pairs;
		Array<PhysicsEvent> Events;

		static uint64 PairKey(JPH::BodyID a, JPH::BodyID b)
		{
			const uint64 x = a.GetIndexAndSequenceNumber();
			const uint64 y = b.GetIndexAndSequenceNumber();
			return x < y ? (x << 32) | y : (y << 32) | x;
		}

		void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
		{
			const JPH::RVec3 point = manifold.mRelativeContactPointsOn1.empty() ? manifold.mBaseOffset : manifold.GetWorldSpaceContactPointOn1(0);
			std::lock_guard<std::mutex> guard(Lock);
			Incoming.push_back({ true, a.GetID(), b.GetID(), a.IsSensor() || b.IsSensor(),
				Vec3(static_cast<float>(point.GetX()), static_cast<float>(point.GetY()), static_cast<float>(point.GetZ())), FromJolt(manifold.mWorldSpaceNormal) });
		}

		void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
		{
			std::lock_guard<std::mutex> guard(Lock);
			Incoming.push_back({ false, pair.GetBody1ID(), pair.GetBody2ID(), false, {}, {} });
		}

		entt::entity EntityOf(JPH::BodyID id) const
		{
			const auto found = BodyToEntity.find(id.GetIndexAndSequenceNumber());
			return found != BodyToEntity.end() ? found->second : entt::null;
		}

		// Turns per shape contacts into one enter and one exit per pair of bodies.
		void CollectEvents()
		{
			std::vector<Raw> raw;
			{
				std::lock_guard<std::mutex> guard(Lock);
				raw.swap(Incoming);
			}
			for (const Raw& contact : raw)
			{
				Pair& pair = Pairs[PairKey(contact.A, contact.B)];
				if (contact.Added)
				{
					pair.Sensor = contact.Sensor;
					if (pair.Contacts++ == 0)
						Events.Add({ contact.Sensor ? PhysicsEvent::Kind::TriggerEnter : PhysicsEvent::Kind::CollisionEnter, EntityOf(contact.A), EntityOf(contact.B), contact.Point, contact.Normal });
				}
				else if (pair.Contacts > 0 && --pair.Contacts == 0)
				{
					Events.Add({ pair.Sensor ? PhysicsEvent::Kind::TriggerExit : PhysicsEvent::Kind::CollisionExit, EntityOf(contact.A), EntityOf(contact.B), {}, {} });
					Pairs.erase(PairKey(contact.A, contact.B));
				}
			}
		}

		// A removed body ends every contact it had, so scripts still hear an exit.
		void EndContactsOf(JPH::BodyID id, entt::entity entity)
		{
			for (auto it = Pairs.begin(); it != Pairs.end();)
			{
				const uint64 a = it->first >> 32;
				const uint64 b = it->first & 0xffffffffu;
				const uint64 self = id.GetIndexAndSequenceNumber();
				if (a != self && b != self)
				{
					++it;
					continue;
				}
				const entt::entity other = EntityOf(JPH::BodyID(static_cast<JPH::uint32>(a == self ? b : a)));
				Events.Add({ it->second.Sensor ? PhysicsEvent::Kind::TriggerExit : PhysicsEvent::Kind::CollisionExit, entity, other, {}, {} });
				it = Pairs.erase(it);
			}
		}

		void Remove(uint32 key)
		{
			const auto found = Bodies.find(key);
			if (found == Bodies.end())
				return;
			JPH::BodyInterface& bodies = System->GetBodyInterface();
			EndContactsOf(found->second.Id, static_cast<entt::entity>(key));
			bodies.RemoveBody(found->second.Id);
			bodies.DestroyBody(found->second.Id);
			BodyToEntity.erase(found->second.Id.GetIndexAndSequenceNumber());
			Bodies.erase(found);
		}

		JPH::BodyID Find(entt::entity entity) const
		{
			const auto found = Bodies.find(Key(entity));
			return found != Bodies.end() ? found->second.Id : JPH::BodyID();
		}
	};

	PhysicsWorld::PhysicsWorld() : m_Impl(std::make_unique<Impl>())
	{
		StartJolt();
		m_Impl->Temp = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
		const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 2);
		m_Impl->Jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
		m_Impl->System = std::make_unique<JPH::PhysicsSystem>();
		m_Impl->System->Init(65536, 0, 65536, 16384, m_Impl->BroadPhase, m_Impl->ObjectVsBroad, m_Impl->ObjectPairs);
		m_Impl->System->SetContactListener(m_Impl.get());
	}

	PhysicsWorld::~PhysicsWorld()
	{
		Clear();
	}

	void PhysicsWorld::Clear()
	{
		std::vector<uint32> keys;
		for (const auto& [key, record] : m_Impl->Bodies)
			keys.push_back(key);
		for (uint32 key : keys)
			m_Impl->Remove(key);
		m_Impl->Pairs.clear();
		m_Impl->Events.Clear();
		m_Impl->Incoming.clear();
		m_Impl->Accumulator = 0;
	}

	void PhysicsWorld::Sync(World& world)
	{
		Impl& impl = *m_Impl;
		JPH::BodyInterface& bodies = impl.System->GetBodyInterface();

		// Bodies whose entity or collider is gone.
		std::vector<uint32> gone;
		for (const auto& [key, record] : impl.Bodies)
		{
			const entt::entity entity = static_cast<entt::entity>(key);
			if (!world.IsValid(entity) || !world.Has<ColliderComponent>(entity))
				gone.push_back(key);
		}
		for (uint32 key : gone)
			impl.Remove(key);

		bool added = false;
		for (auto [entity, collider] : world.View<ColliderComponent>().each())
		{
			const RigidBodyComponent* rigidBody = world.TryGet<RigidBodyComponent>(entity);
			const MeshComponent* mesh = world.TryGet<MeshComponent>(entity);
			const TransformComponent placed = world.GetWorldTransform(entity);
			const Quat rotation = world.GetWorldRotation(entity);
			Recipe recipe{ collider, rigidBody ? *rigidBody : RigidBodyComponent(), rigidBody != nullptr, placed.Scale, mesh ? mesh->Mesh : String() };

			const uint32 key = Key(entity);
			auto found = impl.Bodies.find(key);
			if (found != impl.Bodies.end() && !(found->second.Made == recipe))
			{
				impl.Remove(key);
				found = impl.Bodies.end();
			}

			if (found == impl.Bodies.end())
			{
				const JPH::EMotionType motion = MotionOf(rigidBody);
				JPH::BodyCreationSettings settings(MakeShape(recipe, motion), JPH::RVec3(placed.Position.X, placed.Position.Y, placed.Position.Z), ToJolt(rotation),
					motion, motion == JPH::EMotionType::Static ? Layers::Still : Layers::Moving);
				settings.mUserData = key;
				settings.mIsSensor = collider.Trigger;
				// Triggers on moving bodies still notice the level.
				settings.mCollideKinematicVsNonDynamic = collider.Trigger;
				if (rigidBody)
				{
					settings.mFriction = rigidBody->Friction;
					settings.mRestitution = rigidBody->Restitution;
					settings.mLinearDamping = rigidBody->LinearDamping;
					settings.mAngularDamping = rigidBody->AngularDamping;
					settings.mGravityFactor = rigidBody->GravityScale;
					settings.mMotionQuality = rigidBody->Continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
					if (rigidBody->LockRotation)
						settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
					if (motion == JPH::EMotionType::Dynamic)
					{
						settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
						settings.mMassPropertiesOverride.mMass = std::max(rigidBody->Mass, 0.001f);
					}
				}
				const JPH::BodyID id = bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
				if (id.IsInvalid())
				{
					FP_LOG_WARN("[Physics] Out of bodies, %s gets none", world.GetEntityName(entity).CStr());
					continue;
				}
				impl.Bodies[key] = { id, recipe, motion, placed.Position, rotation };
				impl.BodyToEntity[id.GetIndexAndSequenceNumber()] = entity;
				added = true;
				continue;
			}

			// Transforms moved by hand: kinematic bodies glide there, the others jump.
			Record& record = found->second;
			if (Near(placed.Position, record.Position) && Near(rotation, record.Rotation))
				continue;
			if (record.Motion == JPH::EMotionType::Kinematic)
				bodies.MoveKinematic(record.Id, JPH::RVec3(placed.Position.X, placed.Position.Y, placed.Position.Z), ToJolt(rotation), FixedStep);
			else
				bodies.SetPositionAndRotation(record.Id, JPH::RVec3(placed.Position.X, placed.Position.Y, placed.Position.Z), ToJolt(rotation), JPH::EActivation::Activate);
			record.Position = placed.Position;
			record.Rotation = rotation;
		}
		if (added)
			impl.System->OptimizeBroadPhase();
	}

	int32 PhysicsWorld::Step(float deltaTime)
	{
		Impl& impl = *m_Impl;
		impl.Accumulator += deltaTime;
		int32 steps = 0;
		while (impl.Accumulator >= FixedStep && steps < 4)
		{
			impl.System->Update(FixedStep, 1, impl.Temp.get(), impl.Jobs.get());
			impl.CollectEvents();
			impl.Accumulator -= FixedStep;
			++steps;
		}
		// After a long stall the rest is dropped rather than caught up later.
		if (impl.Accumulator > FixedStep)
			impl.Accumulator = 0;
		return steps;
	}

	void PhysicsWorld::WriteBack(World& world)
	{
		JPH::BodyInterface& bodies = m_Impl->System->GetBodyInterface();
		for (auto& [key, record] : m_Impl->Bodies)
		{
			if (record.Motion != JPH::EMotionType::Dynamic || !bodies.IsActive(record.Id))
				continue;
			const entt::entity entity = static_cast<entt::entity>(key);
			JPH::RVec3 position;
			JPH::Quat rotation;
			bodies.GetPositionAndRotation(record.Id, position, rotation);
			TransformComponent placed = world.GetWorldTransform(entity);
			placed.Position = { static_cast<float>(position.GetX()), static_cast<float>(position.GetY()), static_cast<float>(position.GetZ()) };
			placed.Rotation = FromJolt(rotation).ToEuler();
			world.SetWorldTransform(entity, placed);
			record.Position = placed.Position;
			record.Rotation = FromJolt(rotation);
		}
	}

	void PhysicsWorld::TakeEvents(Array<PhysicsEvent>& out)
	{
		for (PhysicsEvent& event : m_Impl->Events)
			out.Add(event);
		m_Impl->Events.Clear();
	}

	int32 PhysicsWorld::GetBodyCount() const { return static_cast<int32>(m_Impl->Bodies.size()); }
	bool PhysicsWorld::HasBody(entt::entity entity) const { return !m_Impl->Find(entity).IsInvalid(); }

	void PhysicsWorld::AddForce(entt::entity entity, Vec3 force)
	{
		const JPH::BodyID id = m_Impl->Find(entity);
		if (!id.IsInvalid())
			m_Impl->System->GetBodyInterface().AddForce(id, ToJolt(force));
	}

	void PhysicsWorld::AddImpulse(entt::entity entity, Vec3 impulse)
	{
		const JPH::BodyID id = m_Impl->Find(entity);
		if (!id.IsInvalid())
			m_Impl->System->GetBodyInterface().AddImpulse(id, ToJolt(impulse));
	}

	void PhysicsWorld::AddTorque(entt::entity entity, Vec3 torque)
	{
		const JPH::BodyID id = m_Impl->Find(entity);
		if (!id.IsInvalid())
			m_Impl->System->GetBodyInterface().AddTorque(id, ToJolt(torque));
	}

	Vec3 PhysicsWorld::GetVelocity(entt::entity entity) const
	{
		const JPH::BodyID id = m_Impl->Find(entity);
		return id.IsInvalid() ? Vec3() : FromJolt(m_Impl->System->GetBodyInterface().GetLinearVelocity(id));
	}

	void PhysicsWorld::SetVelocity(entt::entity entity, Vec3 velocity)
	{
		const JPH::BodyID id = m_Impl->Find(entity);
		if (!id.IsInvalid())
			m_Impl->System->GetBodyInterface().SetLinearVelocity(id, ToJolt(velocity));
	}

	Vec3 PhysicsWorld::GetAngularVelocity(entt::entity entity) const
	{
		const JPH::BodyID id = m_Impl->Find(entity);
		return id.IsInvalid() ? Vec3() : FromJolt(m_Impl->System->GetBodyInterface().GetAngularVelocity(id));
	}

	void PhysicsWorld::SetAngularVelocity(entt::entity entity, Vec3 velocity)
	{
		const JPH::BodyID id = m_Impl->Find(entity);
		if (!id.IsInvalid())
			m_Impl->System->GetBodyInterface().SetAngularVelocity(id, ToJolt(velocity));
	}

	bool PhysicsWorld::Raycast(Vec3 origin, Vec3 direction, float maxDistance, RaycastHit& hit, entt::entity ignore) const
	{
		const float length = direction.Length();
		if (length < 1e-8f || maxDistance <= 0)
			return false;
		const Vec3 unit = direction / length;

		// Skips triggers and the body asked to be ignored, like the one casting the ray.
		class Filter final : public JPH::BodyFilter
		{
		public:
			explicit Filter(JPH::BodyID ignore) : m_Ignore(ignore) {}
			bool ShouldCollide(const JPH::BodyID& id) const override { return id != m_Ignore; }
			bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }

		private:
			JPH::BodyID m_Ignore;
		};
		const Filter filter(m_Impl->Find(ignore));

		const JPH::RRayCast ray{ JPH::RVec3(origin.X, origin.Y, origin.Z), ToJolt(unit * maxDistance) };
		JPH::RayCastResult result;
		if (!m_Impl->System->GetNarrowPhaseQuery().CastRay(ray, result, {}, {}, filter))
			return false;

		hit.Entity = m_Impl->EntityOf(result.mBodyID);
		hit.Distance = result.mFraction * maxDistance;
		hit.Point = origin + unit * hit.Distance;
		JPH::BodyLockRead lock(m_Impl->System->GetBodyLockInterface(), result.mBodyID);
		hit.Normal = lock.Succeeded() ? FromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction))) : -unit;
		return true;
	}

	Array<entt::entity> PhysicsWorld::OverlapSphere(Vec3 center, float radius) const
	{
		Array<entt::entity> found;
		if (radius <= 0)
			return found;
		const JPH::SphereShape sphere(radius);
		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
		m_Impl->System->GetNarrowPhaseQuery().CollideShape(&sphere, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(JPH::RVec3(center.X, center.Y, center.Z)),
			JPH::CollideShapeSettings(), JPH::RVec3::sZero(), collector);
		for (const JPH::CollideShapeResult& hit : collector.mHits)
		{
			const entt::entity entity = m_Impl->EntityOf(hit.mBodyID2);
			if (entity != entt::null)
				found.AddUnique(entity);
		}
		return found;
	}

	Vec3 PhysicsWorld::GetGravity() const { return FromJolt(m_Impl->System->GetGravity()); }
	void PhysicsWorld::SetGravity(Vec3 gravity) { m_Impl->System->SetGravity(ToJolt(gravity)); }
}
