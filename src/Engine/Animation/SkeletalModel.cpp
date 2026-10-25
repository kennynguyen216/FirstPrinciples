#include "Engine/Animation/SkeletalModel.h"
#include "Engine/Paths.h"
#include "Engine/Assets/AssetDatabase.h"

#include <cgltf.h>

#include <cmath>

namespace FP
{
	namespace
	{
		Mat4 ToMat4(const float m[16])
		{
			Mat4 out;
			for (int i = 0; i < 16; ++i)
				out.M[i] = m[i];
			return out;
		}

		BoneTransform LocalTransform(const cgltf_node* node)
		{
			BoneTransform out;
			if (!node->has_matrix)
			{
				if (node->has_translation)
					out.Translation = { node->translation[0], node->translation[1], node->translation[2] };
				if (node->has_rotation)
					out.Rotation = Quat{ node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3] }.Normalized();
				if (node->has_scale)
					out.Scale = { node->scale[0], node->scale[1], node->scale[2] };
				return out;
			}
			// Split a matrix into parts so it can be animated and blended like the others.
			Mat4 m = ToMat4(node->matrix);
			out.Translation = { m.At(3, 0), m.At(3, 1), m.At(3, 2) };
			for (int c = 0; c < 3; ++c)
			{
				const float length = Vec3(m.At(c, 0), m.At(c, 1), m.At(c, 2)).Length();
				(&out.Scale.X)[c] = length;
				for (int r = 0; r < 3; ++r)
					m.At(c, r) /= length > 0 ? length : 1;
			}
			out.Rotation = Quat::FromMatrix(m);
			return out;
		}

		void ReadSkeleton(const cgltf_skin* skin, Skeleton& out, Array<int32>& jointToBone, HashMap<const cgltf_node*, int32>& nodeToBone)
		{
			const int32 count = static_cast<int32>(skin->joints_count);
			// Parent joint of each joint, skipping any plain nodes in between.
			Array<int32> parents;
			Array<int32> depths;
			for (int32 i = 0; i < count; ++i)
			{
				int32 parent = -1;
				int32 depth = 0;
				for (const cgltf_node* node = skin->joints[i]->parent; node && parent < 0; node = node->parent)
				{
					for (int32 j = 0; j < count; ++j)
					{
						if (skin->joints[j] == node)
							parent = j;
					}
				}
				for (const cgltf_node* node = skin->joints[i]->parent; node; node = node->parent)
					++depth;
				parents.Add(parent);
				depths.Add(depth);
			}

			// Parents before children, so poses can be resolved in one pass.
			Array<int32> order;
			for (int32 i = 0; i < count; ++i)
				order.Add(i);
			order.Sort([&](int32 a, int32 b) { return depths[a] != depths[b] ? depths[a] < depths[b] : a < b; });
			jointToBone.Resize(count);
			for (int32 i = 0; i < count; ++i)
				jointToBone[order[i]] = i;

			out.Bones.Clear();
			for (int32 i = 0; i < count; ++i)
			{
				const int32 joint = order[i];
				const cgltf_node* node = skin->joints[joint];
				Bone bone;
				bone.Name = node->name ? node->name : "";
				bone.Parent = parents[joint] >= 0 ? jointToBone[parents[joint]] : -1;
				bone.Rest = LocalTransform(node);
				if (skin->inverse_bind_matrices)
				{
					float m[16];
					cgltf_accessor_read_float(skin->inverse_bind_matrices, static_cast<cgltf_size>(joint), m, 16);
					bone.InverseBind = ToMat4(m);
				}
				// Plain nodes above the first root still move it, so keep their transform.
				if (bone.Parent < 0 && node->parent && i == 0)
				{
					float m[16];
					cgltf_node_transform_world(node->parent, m);
					out.Root = ToMat4(m);
				}
				out.Bones.Add(bone);
				nodeToBone.Add(node, i);
			}
		}

		bool ReadMesh(const cgltf_mesh* mesh, const Mat4* transform, const Array<int32>& jointToBone, SkeletalModel& out)
		{
			bool hasTangents = true;
			for (cgltf_size p = 0; p < mesh->primitives_count; ++p)
			{
				const cgltf_primitive& primitive = mesh->primitives[p];
				const cgltf_accessor* positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
				if (primitive.type != cgltf_primitive_type_triangles || !positions)
					continue;
				const cgltf_accessor* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
				const cgltf_accessor* uvs = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
				const cgltf_accessor* tangents = cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0);
				const cgltf_accessor* joints = cgltf_find_accessor(&primitive, cgltf_attribute_type_joints, 0);
				const cgltf_accessor* weights = cgltf_find_accessor(&primitive, cgltf_attribute_type_weights, 0);
				hasTangents &= tangents != nullptr;
				// With a skeleton every vertex gets skin data so the buffers line up. Primitives
				// without weights follow the first bone.
				const bool hasSkeleton = !jointToBone.IsEmpty();
				const bool weighted = hasSkeleton && joints && weights;

				const uint32 base = static_cast<uint32>(out.Mesh.Vertices.Num());
				const Mat4 normalMatrix = transform ? transform->Inverse().Transposed() : Mat4();
				for (cgltf_size v = 0; v < positions->count; ++v)
				{
					MeshVertex vertex;
					cgltf_accessor_read_float(positions, v, &vertex.Position.X, 3);
					if (normals)
						cgltf_accessor_read_float(normals, v, &vertex.Normal.X, 3);
					if (uvs)
						cgltf_accessor_read_float(uvs, v, &vertex.UV.X, 2);
					if (tangents)
						cgltf_accessor_read_float(tangents, v, &vertex.Tangent.X, 4);
					if (transform)
					{
						vertex.Position = transform->TransformPoint(vertex.Position);
						vertex.Normal = normalMatrix.TransformVector(vertex.Normal).Normalized();
						vertex.Tangent = { transform->TransformVector(vertex.Tangent.XYZ()).Normalized(), vertex.Tangent.W };
					}
					out.Mesh.Vertices.Add(vertex);

					if (!hasSkeleton)
						continue;
					SkinVertex skin;
					if (!weighted)
					{
						out.Skin.Add(skin);
						continue;
					}
					cgltf_uint bones[4] = {};
					cgltf_accessor_read_uint(joints, v, bones, 4);
					cgltf_accessor_read_float(weights, v, skin.Weights, 4);
					float total = 0;
					for (int i = 0; i < 4; ++i)
					{
						const bool valid = bones[i] < static_cast<cgltf_uint>(jointToBone.Num());
						skin.Bones[i] = static_cast<uint16>(valid ? jointToBone[static_cast<int32>(bones[i])] : 0);
						if (!valid)
							skin.Weights[i] = 0;
						total += skin.Weights[i];
					}
					for (float& weight : skin.Weights)
						weight = total > 0 ? weight / total : 0;
					if (total <= 0)
						skin.Weights[0] = 1;
					out.Skin.Add(skin);
				}

				MeshSection section;
				section.Name = primitive.material && primitive.material->name ? primitive.material->name : "Default";
				section.FirstIndex = static_cast<uint32>(out.Mesh.Indices.Num());
				if (primitive.indices)
				{
					for (cgltf_size i = 0; i < primitive.indices->count; ++i)
						out.Mesh.Indices.Add(base + static_cast<uint32>(cgltf_accessor_read_index(primitive.indices, i)));
				}
				else
				{
					for (cgltf_size i = 0; i < positions->count; ++i)
						out.Mesh.Indices.Add(base + static_cast<uint32>(i));
				}
				section.IndexCount = static_cast<uint32>(out.Mesh.Indices.Num()) - section.FirstIndex;
				out.Mesh.Sections.Add(section);
			}
			if (out.Mesh.Indices.IsEmpty())
				return false;
			if (!hasTangents)
				out.Mesh.ComputeTangents();
			out.Mesh.ComputeBounds();
			return true;
		}

		void ReadClips(const cgltf_data* data, const HashMap<const cgltf_node*, int32>& nodeToBone, SkeletalModel& out)
		{
			for (cgltf_size a = 0; a < data->animations_count; ++a)
			{
				const cgltf_animation& animation = data->animations[a];
				AnimationClip clip;
				clip.Name = animation.name ? String(animation.name) : String::Format("Clip%d", static_cast<int>(a));
				for (cgltf_size c = 0; c < animation.channels_count; ++c)
				{
					const cgltf_animation_channel& channel = animation.channels[c];
					const int32* bone = channel.target_node ? nodeToBone.Find(channel.target_node) : nullptr;
					if (!bone || !channel.sampler || !channel.sampler->input || !channel.sampler->output)
						continue;
					int32 trackIndex = clip.Tracks.FindIf([&](const BoneTrack& t) { return t.Bone == *bone; });
					if (trackIndex < 0)
					{
						trackIndex = clip.Tracks.Num();
						clip.Tracks.Emplace().Bone = *bone;
					}
					BoneTrack& track = clip.Tracks[trackIndex];

					const cgltf_accessor* input = channel.sampler->input;
					const cgltf_accessor* output = channel.sampler->output;
					// Cubic splines store in tangent, value, out tangent per key. Only the value is used.
					const bool cubic = channel.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
					const cgltf_size stride = cubic ? 3 : 1;
					const cgltf_size offset = cubic ? 1 : 0;
					Array<float> times;
					for (cgltf_size k = 0; k < input->count; ++k)
					{
						float time = 0;
						cgltf_accessor_read_float(input, k, &time, 1);
						times.Add(time);
						clip.Duration = time > clip.Duration ? time : clip.Duration;
					}
					for (cgltf_size k = 0; k < input->count && k * stride + offset < output->count; ++k)
					{
						float value[4] = {};
						cgltf_accessor_read_float(output, k * stride + offset, value, 4);
						if (channel.target_path == cgltf_animation_path_type_translation)
							track.Translations.Add({ value[0], value[1], value[2] });
						else if (channel.target_path == cgltf_animation_path_type_rotation)
							track.Rotations.Add(Quat{ value[0], value[1], value[2], value[3] }.Normalized());
						else if (channel.target_path == cgltf_animation_path_type_scale)
							track.Scales.Add({ value[0], value[1], value[2] });
					}
					if (channel.target_path == cgltf_animation_path_type_translation)
						track.TranslationTimes = times;
					else if (channel.target_path == cgltf_animation_path_type_rotation)
						track.RotationTimes = times;
					else if (channel.target_path == cgltf_animation_path_type_scale)
						track.ScaleTimes = times;
				}
				out.Clips.Add(clip);
			}
		}

		bool Convert(const cgltf_data* data, SkeletalModel& out)
		{
			out = SkeletalModel();
			const cgltf_skin* skin = data->skins_count > 0 ? &data->skins[0] : nullptr;

			// The node that draws the mesh, preferring one bound to the skin.
			const cgltf_node* meshNode = nullptr;
			for (cgltf_size i = 0; i < data->nodes_count; ++i)
			{
				const cgltf_node* node = &data->nodes[i];
				if (node->mesh && (!meshNode || (skin && node->skin == skin && meshNode->skin != skin)))
					meshNode = node;
			}
			const cgltf_mesh* mesh = meshNode ? meshNode->mesh : data->meshes_count > 0 ? &data->meshes[0] : nullptr;
			if (!mesh)
				return false;

			Array<int32> jointToBone;
			HashMap<const cgltf_node*, int32> nodeToBone;
			const bool skinned = skin && meshNode && meshNode->skin == skin;
			if (skinned)
				ReadSkeleton(skin, out.Bones, jointToBone, nodeToBone);

			// A skinned mesh is placed by its bones. Anything else keeps its node's transform.
			Mat4 transform;
			if (meshNode && !skinned)
			{
				float m[16];
				cgltf_node_transform_world(meshNode, m);
				transform = ToMat4(m);
			}
			if (!ReadMesh(mesh, meshNode && !skinned ? &transform : nullptr, jointToBone, out))
				return false;
			if (skinned)
				ReadClips(data, nodeToBone, out);
			return true;
		}

		bool Finish(cgltf_result result, cgltf_data* data, const cgltf_options& options, const char* path, SkeletalModel& out)
		{
			bool ok = result == cgltf_result_success;
			ok = ok && cgltf_load_buffers(&options, data, path) == cgltf_result_success;
			ok = ok && cgltf_validate(data) == cgltf_result_success;
			ok = ok && Convert(data, out);
			if (data)
				cgltf_free(data);
			return ok;
		}
	}

	const AnimationClip* SkeletalModel::FindClip(std::string_view name) const
	{
		const int32 index = Clips.FindIf([&](const AnimationClip& clip) { return clip.Name == name; });
		return index >= 0 ? &Clips[index] : nullptr;
	}

	bool LoadGltf(const char* fullPath, SkeletalModel& out)
	{
		cgltf_options options = {};
		cgltf_data* data = nullptr;
		const cgltf_result result = cgltf_parse_file(&options, fullPath, &data);
		return Finish(result, data, options, fullPath, out);
	}

	bool ParseGltf(std::string_view bytes, SkeletalModel& out)
	{
		cgltf_options options = {};
		cgltf_data* data = nullptr;
		const cgltf_result result = cgltf_parse(&options, bytes.data(), bytes.size(), &data);
		return Finish(result, data, options, nullptr, out);
	}

	bool IsGltfPath(std::string_view path)
	{
		const String lower = Paths::AssetPath(path).ToLower();
		return lower.EndsWith(".gltf") || lower.EndsWith(".glb");
	}

	SkeletalModelLibrary& SkeletalModelLibrary::Get()
	{
		static SkeletalModelLibrary library;
		return library;
	}

	const SkeletalModel* SkeletalModelLibrary::Load(const String& reference)
	{
		const String path = Paths::AssetReference(reference);
		if (path.IsEmpty())
			return nullptr;
		const uint64 revision = Paths::GetAssets().Fingerprint(path);
		if (!m_Revisions.Contains(path) || m_Revisions.FindRef(path) != revision)
		{
			m_Revisions.Add(path, revision);
			m_Failed.Remove(path);
			if (auto* cached = m_Models.Find(path))
			{
				SkeletalModel updated;
				if (LoadGltf(Paths::Content(path).CStr(), updated))
					**cached = std::move(updated);
				return cached->get();
			}
		}
		if (std::shared_ptr<SkeletalModel>* cached = m_Models.Find(path))
			return cached->get();
		if (m_Failed.Contains(path))
			return nullptr;

		std::shared_ptr<SkeletalModel> model = std::make_shared<SkeletalModel>();
		if (!LoadGltf(Paths::Content(path.View()).CStr(), *model))
		{
			FP_LOG_WARN("Could not load glTF %s", path.CStr());
			m_Failed.Add(path);
			return nullptr;
		}
		if (model->Bones.Bones.Num() > MaxSkinBones)
			FP_LOG_WARN("%s has %d bones. Only the first %d move the mesh.", path.CStr(), model->Bones.Bones.Num(), MaxSkinBones);
		m_Models.Add(path, model);
		return model.get();
	}

	void SkeletalModelLibrary::Clear()
	{
		m_Models.Clear();
		m_Revisions.Clear();
		m_Failed.Clear();
	}
}
