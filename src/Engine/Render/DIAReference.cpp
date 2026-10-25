#include "Engine/Render/DIA.h"

#include <algorithm>
#include <atomic>
#include <thread>

namespace FP
{
	namespace
	{
		uint32 Bits(uint32& state)
		{
			state = state * 747796405u + 2891336453u;
			const uint32 word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
			return (word >> 22u) ^ word;
		}
		float Random(uint32& state) { return TransportUnitSample(Bits(state)); }
		float Saturate(float value) { return std::clamp(value, 0.0f, 1.0f); }
		float PowerWeight(float a, float b)
		{
			const float scale = std::max({ a, b, 1e-20f });
			a /= scale;
			b /= scale;
			return a * a / std::max(a * a + b * b, 1e-20f);
		}
		Vec3 Fresnel(float cosine, Vec3 f0) { return f0 + (Vec3(1, 1, 1) - f0) * std::pow(Saturate(1 - cosine), 5.0f); }
		float Distribution(float nh, float roughness)
		{
			const float a = std::max(roughness * roughness, 0.0004f);
			const float a2 = a * a;
			const float d = nh * nh * (a2 - 1) + 1;
			return a2 / std::max(Pi * d * d, 1e-20f);
		}
		float Geometry(float nv, float nl, float roughness)
		{
			const float a = std::max(roughness * roughness, 0.0004f);
			const float a2 = a * a;
			return 2 * nv / std::max(nv + std::sqrt(a2 + (1 - a2) * nv * nv), 1e-8f) *
				2 * nl / std::max(nl + std::sqrt(a2 + (1 - a2) * nl * nl), 1e-8f);
		}
		Vec3 Basis(Vec3 n, Vec3 local)
		{
			const Vec3 tangent = Vec3::Cross(std::fabs(n.Z) < 0.999f ? Vec3(0, 0, 1) : Vec3(0, 1, 0), n).Normalized();
			return tangent * local.X + Vec3::Cross(n, tangent) * local.Y + n * local.Z;
		}
	}

	Array<Vec4> DIA::CaptureGeometry(const RenderView& originalView, bool reflected) const
	{
		Array<Vec4> pixels;
		if (!GetOutput()) return pixels;
		RenderView view = originalView;
		view.Width = GetOutput()->GetWidth();
		view.Height = GetOutput()->GetHeight();
		pixels.Resize(static_cast<int32>(view.Width * view.Height));
		for (uint32 y = 0; y < view.Height; ++y) for (uint32 x = 0; x < view.Width; ++x)
		{
			float farDistance;
			Ray ray = view.ScreenToRay({ static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f }, &farDistance);
			RayHit hit = m_Scene.Trace(ray, farDistance);
			Vec4 pixel{ hit.Position, hit.Class == RayResultClass::Surface ? static_cast<float>(hit.Instance + 1) : 0 };
			if (reflected && hit.Class == RayResultClass::Surface)
			{
				const auto& instance = m_Instances[static_cast<int32>(hit.Instance)];
				const auto& material = m_Materials[static_cast<int32>(instance.Metadata[0])];
				if (material.Parameters.Y > 0.02f || material.Textures[1][1] || material.Textures[4][1]) pixel = { 0, 0, 0, -1 };
				else
				{
					Vec3 n = hit.ShadingNormal;
					Vec3 g = hit.GeometricNormal;
					if (material.Flags[0] && Vec3::Dot(g, -ray.Direction) < 0) { n = -n; g = -g; }
					const Vec3 direction = (ray.Direction - n * (2 * Vec3::Dot(n, ray.Direction))).Normalized();
					hit = m_Scene.Trace({ hit.Position + g * 0.0002f, direction }, m_Uniforms.Settings.X);
					pixel = { hit.Position, hit.Class == RayResultClass::Surface ? static_cast<float>(hit.Instance + 1) : 0 };
				}
			}
			pixels[static_cast<int32>(y * view.Width + x)] = pixel;
		}
		return pixels;
	}

	Array<Vec4> DIA::RenderReference(const RenderView& originalView, uint32 width, uint32 height, uint32 samples, uint32 seed, uint32 bounces) const
	{
		Array<Vec4> pixels;
		if (width == 0 || height == 0 || width > 256 || height > 256 || samples == 0 || samples > 16384 || bounces == 0 || bounces > 16) return pixels;
		pixels.Resize(static_cast<int32>(width * height));
		RenderView view = originalView;
		view.Width = width;
		view.Height = height;
		auto sampleMap = [&](const uint32* descriptor, Vec2 uv, Vec3 fallback) {
			return SampleTransportTexture(descriptor, uv, fallback);
		};
		auto emitterDensity = [&](const RayHit& hit, Vec3 origin) {
			const DIAInstance& instance = m_Instances[static_cast<int32>(hit.Instance)];
			const DIAMaterial& material = m_Materials[static_cast<int32>(instance.Metadata[0])];
			const uint32 grid = material.Flags[3];
			if (!material.Flags[2] || !grid) return 0.0f;
			const float r = std::max(1 - hit.Barycentrics.X, 1e-8f);
			const uint32 x = static_cast<uint32>(std::clamp(r * r * grid, 0.0f, static_cast<float>(grid - 1)));
			const uint32 y = static_cast<uint32>(std::clamp(hit.Barycentrics.Y / r * grid, 0.0f, static_cast<float>(grid - 1)));
			const uint32 selected = material.Flags[2] - 1 + hit.Triangle * grid * grid + y * grid + x;
			const DIAEmitter& emitter = m_Emitters[static_cast<int32>(selected)];
			const float previous = selected ? m_Emitters[static_cast<int32>(selected - 1)].Distribution.X : 0;
			const float uniformWeight = m_EmitterWeight > 0 ? 0.05f : 1.0f;
			const float probability = uniformWeight / m_Emitters.Num() + (m_EmitterWeight > 0 ? (1 - uniformWeight) * (emitter.Distribution.X - previous) / m_EmitterWeight : 0);
			const RayTriangle& triangle = m_Triangles[static_cast<int32>(instance.Geometry[1] + hit.Triangle)];
			const float area = Vec3::Cross(instance.Transform.TransformVector(triangle.Position[1].XYZ() - triangle.Position[0].XYZ()), instance.Transform.TransformVector(triangle.Position[2].XYZ() - triangle.Position[0].XYZ())).Length() * 0.5f;
			const Vec3 delta = hit.Position - origin;
			float cosine = Vec3::Dot(hit.GeometricNormal, -delta.Normalized());
			if (material.Flags[0]) cosine = std::fabs(cosine);
			return cosine > 0 ? probability * grid * grid * delta.LengthSquared() / std::max(area * cosine, 1e-20f) : 0.0f;
		};
		std::atomic<uint32> nextRow{ 0 };
		auto traceRows = [&]() {
		for (uint32 y = nextRow.fetch_add(1); y < height; y = nextRow.fetch_add(1))
		{
			for (uint32 x = 0; x < width; ++x)
			{
				Vec3 total;
				uint32 state = seed + x * 1973 + y * 9277;
				float farDistance;
				const Ray primary = view.ScreenToRay({ static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f }, &farDistance);
				for (uint32 sample = 0; sample < samples; ++sample)
				{
					Ray ray = primary;
					Vec3 throughput{ 1, 1, 1 };
					bool includeEmission = true;
					float emissionPdf = 0;
					Vec3 emissionOrigin;
					for (uint32 depth = 0; depth < bounces; ++depth)
					{
						const RayHit hit = m_Scene.Trace(ray, depth == 0 ? farDistance : m_Uniforms.Settings.X);
						if (hit.Class == RayResultClass::Escaped) { total += throughput * m_Uniforms.Sky.XYZ(); break; }
						if (hit.Class != RayResultClass::Surface) break;
						const RayInstance& instance = m_Scene.GetInstances()[static_cast<int32>(hit.Instance)];
						const DIAInstance& packed = m_Instances[static_cast<int32>(hit.Instance)];
						const DIAMaterial& material = m_Materials[static_cast<int32>(instance.Material)];
						const RayTriangle& triangle = instance.Mesh->Triangles[static_cast<int32>(hit.Triangle)];
						Vec3 geometric = hit.GeometricNormal;
						Vec3 normal = hit.ShadingNormal;
						if (Vec3::Dot(geometric, -ray.Direction) < 0)
						{
							if (!material.Flags[0]) break;
							geometric = -geometric;
							normal = -normal;
						}
						const Vec2 uv = hit.UV * Vec2(material.UVTransform.X, material.UVTransform.Y) + Vec2(material.UVTransform.Z, material.UVTransform.W);
						const Vec4 tangent{ triangle.Tangent[0].XYZ() * hit.Barycentrics.X + triangle.Tangent[1].XYZ() * hit.Barycentrics.Y + triangle.Tangent[2].XYZ() * hit.Barycentrics.Z,
							triangle.Tangent[0].W * hit.Barycentrics.X + triangle.Tangent[1].W * hit.Barycentrics.Y + triangle.Tangent[2].W * hit.Barycentrics.Z };
						Vec3 t = instance.Transform.TransformVector(tangent.XYZ());
						t = (t - normal * Vec3::Dot(t, normal)).Normalized();
						const float determinant = Vec3::Dot(instance.Transform.TransformVector({ 1, 0, 0 }), Vec3::Cross(instance.Transform.TransformVector({ 0, 1, 0 }), instance.Transform.TransformVector({ 0, 0, 1 })));
						const Vec3 bitangent = Vec3::Cross(normal, t) * tangent.W * (determinant < 0 ? -1.0f : 1.0f);
						Vec3 mapped = sampleMap(material.Textures[1], uv, { 0.5f, 0.5f, 1 }) * 2 - Vec3(1, 1, 1);
						mapped.X *= material.Parameters.W;
						mapped.Y *= material.Parameters.W;
						normal = (t * mapped.X + bitangent * mapped.Y + normal * mapped.Z).Normalized();
						if (Vec3::Dot(normal, geometric) < 0.001f) normal = geometric;
						const Vec3 base = sampleMap(material.Textures[0], uv, { 1, 1, 1 }) * material.BaseColor.XYZ() * packed.Tint.XYZ();
						const Vec3 emission = sampleMap(material.Textures[6], uv, { 1, 1, 1 }) * material.Emission.XYZ();
						const float metallic = Saturate(material.Parameters.X * sampleMap(material.Textures[3], uv, { 1, 1, 1 }).X);
						const float roughness = Saturate(material.Parameters.Y * sampleMap(material.Textures[4], uv, { 1, 1, 1 }).X);
						const float specular = Saturate(material.Parameters.Z * sampleMap(material.Textures[5], uv, { 1, 1, 1 }).X);
						const Vec3 f0 = Vec3(1, 1, 1) * (0.08f * specular * (1 - metallic)) + base * metallic;
						const Vec3 diffuse = base * (1 - metallic) * (Vec3(1, 1, 1) - f0);
						const float ao = 1 + (sampleMap(material.Textures[2], uv, { 1, 1, 1 }).X - 1) * Saturate(material.Parameters2.X);
						if (includeEmission || m_Emitters.IsEmpty()) total += throughput * emission * (emissionPdf > 0 && !m_Emitters.IsEmpty() ? PowerWeight(emissionPdf, emitterDensity(hit, emissionOrigin)) : 1.0f);
						if (material.Flags[1]) { if (depth == 0) total += base; break; }
						const Vec3 viewDirection = -ray.Direction;
						const float nv = std::max(Vec3::Dot(normal, viewDirection), 0.0001f);
						const Vec3 specularEnergy = Fresnel(nv, f0);
						const float specularWeight = std::max(specularEnergy.X, std::max(specularEnergy.Y, specularEnergy.Z));
						const float diffuseWeight = std::max(diffuse.X, std::max(diffuse.Y, diffuse.Z)) * ao;
						const float specularProbability = diffuseWeight <= 0 ? 1.0f : std::clamp(specularWeight / (specularWeight + diffuseWeight), 0.05f, 0.95f);
						for (const DIALight& light : m_Lights)
						{
							const bool segment = light.Position.W > 0.5f;
							Vec3 direction = -light.Position.XYZ().Normalized();
							Vec3 radiance = light.ColorRange.XYZ();
							float maximum = m_Uniforms.Settings.X;
							if (segment)
							{
								const Vec3 delta = light.Position.XYZ() - hit.Position;
								const float distance = delta.Length();
								 direction = delta / std::max(distance, 1e-8f);
								radiance *= PunctualAttenuation(distance, light.ColorRange.W);
								if (light.Position.W > 1.5f)
								{
									const float cone = Saturate((Vec3::Dot(-direction, light.DirectionInner.XYZ()) - light.OuterRadius.X) / (light.DirectionInner.W - light.OuterRadius.X));
									radiance *= cone * cone * (3 - 2 * cone);
								}
							}
							const float nl = std::max(0.0f, Vec3::Dot(normal, direction));
							if (nl <= 0 || Vec3::Dot(geometric, direction) <= 0) continue;
							Ray shadow{ hit.Position + geometric * 0.0002f, direction };
							if (segment)
							{
								const Vec3 connection = light.Position.XYZ() - shadow.Origin;
								const float shadowDistance = connection.Length();
								if (shadowDistance > 0.0001f) shadow.Direction = connection / shadowDistance;
								maximum = std::max(0.0001f, shadowDistance - 0.0001f);
							}
							const RayResultClass visibility = m_Scene.Trace(shadow, maximum, segment).Class;
							if (visibility != (segment ? RayResultClass::ClearSegment : RayResultClass::Escaped)) continue;
							const Vec3 half = (direction + viewDirection).Normalized();
							const Vec3 brdf = roughness <= 0.02f ? Vec3() : Fresnel(std::max(0.0f, Vec3::Dot(viewDirection, half)), f0) *
								(Distribution(std::max(0.0f, Vec3::Dot(normal, half)), roughness) * Geometry(nv, nl, roughness) / (4 * nv * nl));
							total += throughput * radiance * (diffuse / Pi + brdf) * nl;
						}
						if (!m_Emitters.IsEmpty())
						{
							const uint32 count = static_cast<uint32>(m_Emitters.Num());
							const float uniformWeight = m_EmitterWeight > 0 ? 0.05f : 1.0f;
							uint32 selected;
							if (Random(state) < uniformWeight) selected = std::min(count - 1, static_cast<uint32>(Random(state) * count));
							else
							{
								const float target = Random(state) * m_EmitterWeight;
								uint32 low = 0, high = count - 1;
								while (low < high)
								{
									const uint32 middle = (low + high) / 2;
									if (m_Emitters[static_cast<int32>(middle)].Distribution.X > target) high = middle;
									else low = middle + 1;
								}
								selected = low;
							}
							const DIAEmitter& emitter = m_Emitters[static_cast<int32>(selected)];
							const float previous = selected ? m_Emitters[static_cast<int32>(selected - 1)].Distribution.X : 0;
							const float probability = uniformWeight / count + (m_EmitterWeight > 0 ? (1 - uniformWeight) * (emitter.Distribution.X - previous) / m_EmitterWeight : 0);
							const float grid = static_cast<float>(emitter.Geometry[3]);
							const float root = std::sqrt((static_cast<float>(emitter.Geometry[2] & 65535u) + Random(state)) / grid);
							const float v = (static_cast<float>(emitter.Geometry[2] >> 16) + Random(state)) / grid;
							const Vec3 bary{ 1 - root, v * root, (1 - v) * root };
							const DIAInstance& source = m_Instances[static_cast<int32>(emitter.Geometry[0])];
							const DIAMaterial& sourceMaterial = m_Materials[static_cast<int32>(source.Metadata[0])];
							const RayTriangle& sourceTriangle = m_Triangles[static_cast<int32>(emitter.Geometry[1])];
							const Vec3 position = source.Transform.TransformPoint(sourceTriangle.Position[0].XYZ() * bary.X + sourceTriangle.Position[1].XYZ() * bary.Y + sourceTriangle.Position[2].XYZ() * bary.Z);
							const Vec3 delta = position - hit.Position;
							const float distance = delta.Length();
							const Vec3 direction = delta / std::max(distance, 1e-8f);
							const Vec3 areaVector = Vec3::Cross(source.Transform.TransformVector(sourceTriangle.Position[1].XYZ() - sourceTriangle.Position[0].XYZ()), source.Transform.TransformVector(sourceTriangle.Position[2].XYZ() - sourceTriangle.Position[0].XYZ()));
							const float area = areaVector.Length() * 0.5f;
							float sourceCosine = Vec3::Dot(areaVector.Normalized(), -direction);
							if (sourceMaterial.Flags[0]) sourceCosine = std::fabs(sourceCosine);
							const float receiverCosine = std::max(0.0f, Vec3::Dot(normal, direction));
							const Vec3 shadowOrigin = hit.Position + geometric * 0.0002f;
							const Vec3 shadowDelta = position - shadowOrigin;
							const float shadowDistance = shadowDelta.Length();
							if (distance > 0.0004f && sourceCosine > 0 && receiverCosine > 0 && Vec3::Dot(geometric, direction) > 0 && m_Scene.Trace({ shadowOrigin, shadowDelta / shadowDistance }, shadowDistance - 0.0001f, true).Class == RayResultClass::ClearSegment)
							{
								Vec2 sourceUV = Vec2(sourceTriangle.UV01.X, sourceTriangle.UV01.Y) * bary.X + Vec2(sourceTriangle.UV01.Z, sourceTriangle.UV01.W) * bary.Y + Vec2(sourceTriangle.UV2.X, sourceTriangle.UV2.Y) * bary.Z;
								sourceUV = sourceUV * Vec2(sourceMaterial.UVTransform.X, sourceMaterial.UVTransform.Y) + Vec2(sourceMaterial.UVTransform.Z, sourceMaterial.UVTransform.W);
								const Vec3 radiance = sampleMap(sourceMaterial.Textures[6], sourceUV, { 1, 1, 1 }) * sourceMaterial.Emission.XYZ();
								const float pdf = probability * grid * grid / std::max(area, 1e-20f);
								Vec3 brdf = diffuse / Pi;
								if (roughness > 0.02f)
								{
									const Vec3 half = (viewDirection + direction).Normalized();
									const float vh = std::max(Vec3::Dot(viewDirection, half), 1e-8f);
									const float nh = std::max(Vec3::Dot(normal, half), 0.0f);
									const float bsdfPdf = depth + 1 < bounces ? specularProbability * Distribution(nh, roughness) * nh / (4 * vh) : 0.0f;
									const float mis = PowerWeight(pdf * distance * distance / sourceCosine, bsdfPdf);
									brdf += Fresnel(vh, f0) * (mis * Distribution(nh, roughness) * Geometry(nv, receiverCosine, roughness) / (4 * nv * receiverCosine));
								}
								total += throughput * brdf * radiance * (receiverCosine * sourceCosine / std::max(distance * distance * pdf, 1e-20f));
							}
						}
						Vec3 next;
						const bool specularSample = specularProbability == 1 || Random(state) < specularProbability;
						includeEmission = specularSample;
						emissionPdf = 0;
						emissionOrigin = hit.Position;
						if (specularSample)
						{
							if (roughness <= 0.02f)
							{
								next = normal * (2 * nv) - viewDirection;
								if (Vec3::Dot(geometric, next) <= 0) break;
								throughput = throughput * Fresnel(nv, f0) / specularProbability;
								ray = { hit.Position + geometric * 0.0002f, next.Normalized() };
								continue;
							}
							const float a = roughness * roughness;
							const float u = Random(state);
							const float cosine = std::sqrt((1 - u) / std::max(1 + (a * a - 1) * u, 1e-8f));
							const float angle = 2 * Pi * Random(state);
							const float sine = std::sqrt(std::max(0.0f, 1 - cosine * cosine));
							const Vec3 half = Basis(normal, { sine * std::cos(angle), sine * std::sin(angle), cosine });
							next = half * (2 * Vec3::Dot(viewDirection, half)) - viewDirection;
						}
						else
						{
							const float r = std::sqrt(Random(state));
							const float angle = 2 * Pi * Random(state);
							next = Basis(normal, { r * std::cos(angle), r * std::sin(angle), std::sqrt(std::max(0.0f, 1 - r * r)) });
						}
						const float nl = Vec3::Dot(normal, next);
						if (nl <= 0 || Vec3::Dot(geometric, next) <= 0) break;
						const Vec3 half = (viewDirection + next).Normalized();
						const float nh = std::max(0.0f, Vec3::Dot(normal, half));
						const float vh = std::max(1e-8f, Vec3::Dot(viewDirection, half));
						if (specularSample) emissionPdf = specularProbability * Distribution(nh, roughness) * nh / (4 * vh);
						if (specularSample) throughput = throughput * Fresnel(vh, f0) * (Geometry(nv, nl, roughness) * vh / std::max(nv * nh * specularProbability, 1e-8f));
						else throughput = throughput * diffuse * ao / (1 - specularProbability);
						if (depth >= 3)
						{
							const float probability = std::clamp(std::max(throughput.X, std::max(throughput.Y, throughput.Z)), 0.05f, 0.95f);
							if (Random(state) > probability) break;
							throughput = throughput / probability;
						}
						ray = { hit.Position + geometric * 0.0002f, next.Normalized() };
					}
				}
				pixels[static_cast<int32>(y * width + x)] = { total / static_cast<float>(samples), 1 };
			}
			if (y % 16 == 0) FP_LOG("Reference row %u/%u", y + 1, height);
		}
		};
		{
			Array<std::jthread> workers;
			const uint32 count = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
			for (uint32 i = 0; i < count; ++i) workers.Emplace(traceRows);
		}
		return pixels;
	}
}
