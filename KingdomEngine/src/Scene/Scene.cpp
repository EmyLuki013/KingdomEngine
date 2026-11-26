#include "KingdomEngine/Scene/Scene.h"
#include "KingdomEngine/Scene/ScriptComponent.h"
#include "KingdomEngine/Scene/SpriteComponent.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <unordered_set>

#include <glad/glad.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

namespace KE
{
	namespace
	{
		constexpr const char* SceneFormat = "KingdomEngineScene";
		constexpr unsigned SceneFormatVersion = 3;
		constexpr unsigned PreviousSceneFormatVersion = 2;
		constexpr unsigned LegacySceneFormatVersion = 1;
		constexpr unsigned MaxHierarchyDepth = 256;

		template<typename Writer>
		void WriteVector3(Writer& writer, const char* name, const glm::vec3& value)
		{
			writer.Key(name);
			writer.StartArray();
			writer.Double(value.x);
			writer.Double(value.y);
			writer.Double(value.z);
			writer.EndArray();
		}

		template<typename Writer>
		void WriteGameObject(Writer& writer, const GameObject& object)
		{
			writer.StartObject();
			writer.Key("id");
			writer.Uint64(object.GetId());
			writer.Key("name");
			writer.String(object.GetName().c_str(), static_cast<rapidjson::SizeType>(object.GetName().size()));
			writer.Key("active");
			writer.Bool(object.IsActive());

			writer.Key("components");
			writer.StartArray();
			for (const auto& component : object.GetComponents())
			{
				if (const Transform* transform = dynamic_cast<const Transform*>(component.get()))
				{
					writer.StartObject();
					writer.Key("type");
					writer.String("Transform");
					writer.Key("enabled");
					writer.Bool(transform->IsEnabled());
					WriteVector3(writer, "position", transform->position);
					WriteVector3(writer, "rotation", transform->rotation);
					WriteVector3(writer, "scale", transform->scale);
					writer.EndObject();
				}
				else if (const SpriteComponent* sprite = dynamic_cast<const SpriteComponent*>(component.get()))
				{
					writer.StartObject();
					writer.Key("type");
					writer.String("Sprite");
					writer.Key("enabled");
					writer.Bool(sprite->IsEnabled());
					writer.Key("texturePath");
					writer.String(sprite->GetTexturePath().c_str(),
						static_cast<rapidjson::SizeType>(sprite->GetTexturePath().size()));
					writer.EndObject();
				}
				else if (const ScriptComponent* script = dynamic_cast<const ScriptComponent*>(component.get()))
				{
					writer.StartObject();
					writer.Key("type");
					writer.String("Script");
					writer.Key("enabled");
					writer.Bool(script->IsEnabled());
					writer.Key("scriptPath");
					writer.String(script->GetScriptPath().c_str(),
						static_cast<rapidjson::SizeType>(script->GetScriptPath().size()));
					writer.EndObject();
				}
			}
			writer.EndArray();

			writer.Key("children");
			writer.StartArray();
			for (const auto& child : object.GetChildren())
				WriteGameObject(writer, *child);
			writer.EndArray();
			writer.EndObject();
		}

		bool ReadVector3(const rapidjson::Value& object, const char* key, glm::vec3& output, std::string& error)
		{
			if (!object.HasMember(key) || !object[key].IsArray() || object[key].Size() != 3)
			{
				error = std::string("Expected a 3-number array for transform.") + key;
				return false;
			}

			float values[3];
			for (rapidjson::SizeType i = 0; i < 3; ++i)
			{
				if (!object[key][i].IsNumber())
				{
					error = std::string("Transform value is not numeric: ") + key;
					return false;
				}
				const double value = object[key][i].GetDouble();
				values[i] = static_cast<float>(value);
				if (!std::isfinite(value) || !std::isfinite(values[i]))
				{
					error = std::string("Transform value is not finite: ") + key;
					return false;
				}
			}

			output = glm::vec3(values[0], values[1], values[2]);
			return true;
		}
	}

	Scene::Scene(const std::string& sceneName)
		: name(sceneName)
		, nextObjectId(1)
		, dirty(false)
	{
	}

	void Scene::SetName(const std::string& value)
	{
		if (name == value)
			return;

		name = value;
		MarkDirty();
	}

	GameObject* Scene::CreateObject(const std::string& objectName, ObjectId parentId)
	{
		if (nextObjectId == std::numeric_limits<ObjectId>::max())
			return nullptr;

		GameObject* parent = parentId == 0 ? nullptr : FindObject(parentId);
		if (parentId != 0 && !parent)
			return nullptr;

		std::unique_ptr<GameObject> object(new GameObject(this, nextObjectId++, objectName));
		GameObject* result = object.get();
		if (parent)
		{
			result->parent = parent;
			parent->children.push_back(std::move(object));
		}
		else
		{
			roots.push_back(std::move(object));
		}
		MarkDirty();
		return result;
	}

	bool Scene::DestroyObject(ObjectId objectId)
	{
		if (!DestroyInTree(roots, objectId))
			return false;

		MarkDirty();
		return true;
	}

	GameObject* Scene::FindObject(ObjectId objectId)
	{
		return FindInTree(roots, objectId);
	}

	const GameObject* Scene::FindObject(ObjectId objectId) const
	{
		return FindInTree(roots, objectId);
	}

	GameObject* Scene::FindObjectByName(const std::string& objectName)
	{
		auto findByName = [&](auto&& self, std::vector<std::unique_ptr<GameObject>>& objects) -> GameObject*
		{
			for (auto& object : objects)
			{
				if (object->GetName() == objectName)
					return object.get();
				if (GameObject* child = self(self, object->children))
					return child;
			}
			return nullptr;
		};
		return findByName(findByName, roots);
	}

	void Scene::Update(float deltaTime)
	{
		for (const auto& object : roots)
			object->Update(deltaTime);
	}

	void Scene::Render(const glm::mat4& viewProjection)
	{
		const GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
		glDisable(GL_DEPTH_TEST);
		for (const auto& object : roots)
			object->Render(viewProjection, glm::mat4(1.0f));
		if (depthTestEnabled)
			glEnable(GL_DEPTH_TEST);
	}

	bool Scene::SaveToFile(const std::filesystem::path& path, std::string& error) const
	{
		error.clear();
		if (path.empty())
		{
			error = "Scene path is empty";
			return false;
		}

		const std::filesystem::path parentPath = path.parent_path();
		if (!parentPath.empty())
		{
			std::error_code filesystemError;
			std::filesystem::create_directories(parentPath, filesystemError);
			if (filesystemError)
			{
				error = "Could not create scene directory: " + filesystemError.message();
				return false;
			}
		}

		rapidjson::StringBuffer buffer;
		rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
		writer.StartObject();
		writer.Key("format");
		writer.String(SceneFormat);
		writer.Key("version");
		writer.Uint(SceneFormatVersion);
		writer.Key("name");
		writer.String(name.c_str(), static_cast<rapidjson::SizeType>(name.size()));
		writer.Key("objects");
		writer.StartArray();
		for (const auto& object : roots)
			WriteGameObject(writer, *object);
		writer.EndArray();
		writer.EndObject();

		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output)
		{
			error = "Could not open scene for writing: " + path.string();
			return false;
		}
		output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
		output.flush();
		if (!output.good())
		{
			error = "Could not write scene: " + path.string();
			return false;
		}
		return true;
	}

	std::unique_ptr<Scene> Scene::LoadFromFile(const std::filesystem::path& path, std::string& error)
	{
		error.clear();
		std::ifstream input(path, std::ios::binary);
		if (!input)
		{
			error = "Could not open scene: " + path.string();
			return nullptr;
		}

		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (input.bad())
		{
			error = "Could not read scene: " + path.string();
			return nullptr;
		}

		rapidjson::Document document;
		document.Parse(contents.data(), contents.size());
		if (document.HasParseError())
		{
			error = "Invalid scene JSON at byte " + std::to_string(document.GetErrorOffset()) +
				": " + rapidjson::GetParseError_En(document.GetParseError());
			return nullptr;
		}
		if (!document.IsObject() ||
			!document.HasMember("format") || !document["format"].IsString() ||
			std::string(document["format"].GetString()) != SceneFormat ||
			!document.HasMember("version") || !document["version"].IsUint() ||
			!document.HasMember("name") || !document["name"].IsString() ||
			!document.HasMember("objects") || !document["objects"].IsArray())
		{
			error = "Scene has missing, invalid, or unsupported metadata";
			return nullptr;
		}

		const unsigned formatVersion = document["version"].GetUint();
		if (formatVersion != LegacySceneFormatVersion &&
			formatVersion != PreviousSceneFormatVersion &&
			formatVersion != SceneFormatVersion)
		{
			error = "Scene has missing, invalid, or unsupported metadata";
			return nullptr;
		}

		std::unique_ptr<Scene> scene(new Scene(std::string(
			document["name"].GetString(), document["name"].GetStringLength())));
		std::unordered_set<ObjectId> objectIds;
		auto readObject = [&](const rapidjson::Value& source, GameObject* parent, unsigned depth, auto&& self) -> bool
		{
			if (depth > MaxHierarchyDepth)
			{
				error = "Scene hierarchy exceeds the maximum supported depth";
				return false;
			}
			if (!source.IsObject() ||
				!source.HasMember("id") || !source["id"].IsUint64() ||
				!source.HasMember("name") || !source["name"].IsString() ||
				!source.HasMember("active") || !source["active"].IsBool() ||
				(formatVersion == LegacySceneFormatVersion &&
					(!source.HasMember("transform") || !source["transform"].IsObject())) ||
				(formatVersion >= PreviousSceneFormatVersion &&
					(!source.HasMember("components") || !source["components"].IsArray())) ||
				!source.HasMember("children") || !source["children"].IsArray())
			{
				error = "Scene object has missing or invalid fields";
				return false;
			}

			const ObjectId objectId = source["id"].GetUint64();
			if (objectId == 0 || objectId == std::numeric_limits<ObjectId>::max() ||
				!objectIds.insert(objectId).second)
			{
				error = "Scene contains an invalid or duplicate object ID";
				return false;
			}

			std::unique_ptr<GameObject> object(new GameObject(scene.get(), objectId,
				std::string(source["name"].GetString(), source["name"].GetStringLength())));
			object->active = source["active"].GetBool();
			if (formatVersion == LegacySceneFormatVersion)
			{
				Transform* transform = object->GetComponent<Transform>();
				if (!ReadVector3(source["transform"], "position", transform->position, error) ||
					!ReadVector3(source["transform"], "rotation", transform->rotation, error) ||
					!ReadVector3(source["transform"], "scale", transform->scale, error))
					return false;
			}
			else
			{
				bool hasTransform = false;
				bool hasSprite = false;
				bool hasScript = false;
				for (const auto& component : source["components"].GetArray())
				{
					if (!component.IsObject() ||
						!component.HasMember("type") || !component["type"].IsString() ||
						!component.HasMember("enabled") || !component["enabled"].IsBool())
					{
						error = "Scene component has missing or invalid fields";
						return false;
					}

					const std::string type(component["type"].GetString(), component["type"].GetStringLength());
					if (type == "Transform")
					{
						if (hasTransform)
						{
							error = "Scene object contains more than one Transform component";
							return false;
						}
						hasTransform = true;

						Transform* transform = object->GetComponent<Transform>();
						if (!ReadVector3(component, "position", transform->position, error) ||
							!ReadVector3(component, "rotation", transform->rotation, error) ||
							!ReadVector3(component, "scale", transform->scale, error))
							return false;
						transform->SetEnabled(component["enabled"].GetBool());
					}
					else if (type == "Sprite")
					{
						if (hasSprite || !component.HasMember("texturePath") ||
							!component["texturePath"].IsString())
						{
							error = "Scene Sprite component has missing or invalid fields";
							return false;
						}
						hasSprite = true;

						SpriteComponent* sprite = object->AddComponent<SpriteComponent>();
						sprite->SetTexturePath(std::string(
							component["texturePath"].GetString(),
							component["texturePath"].GetStringLength()));
						sprite->SetEnabled(component["enabled"].GetBool());
					}
					else if (type == "Script" && formatVersion == SceneFormatVersion)
					{
						if (hasScript || !component.HasMember("scriptPath") ||
							!component["scriptPath"].IsString())
						{
							error = "Scene Script component has missing or invalid fields";
							return false;
						}
						hasScript = true;

						ScriptComponent* script = object->AddComponent<ScriptComponent>();
						script->SetScriptPath(std::string(
							component["scriptPath"].GetString(),
							component["scriptPath"].GetStringLength()));
						script->SetEnabled(component["enabled"].GetBool());
					}
					else
					{
						error = "Scene contains an unsupported component type: " + type;
						return false;
					}
				}

				if (!hasTransform)
				{
					error = "Scene object is missing its Transform component";
					return false;
				}
			}

			GameObject* objectPointer = object.get();
			if (parent)
			{
				objectPointer->parent = parent;
				parent->children.push_back(std::move(object));
			}
			else
			{
				scene->roots.push_back(std::move(object));
			}
			scene->nextObjectId = std::max(scene->nextObjectId, objectId + 1);

			for (const auto& child : source["children"].GetArray())
			{
				if (!self(child, objectPointer, depth + 1, self))
					return false;
			}
			return true;
		};

		for (const auto& object : document["objects"].GetArray())
		{
			if (!readObject(object, nullptr, 0, readObject))
				return nullptr;
		}

		scene->dirty = false;
		return scene;
	}

	GameObject* Scene::FindInTree(const std::vector<std::unique_ptr<GameObject>>& objects, ObjectId objectId)
	{
		for (const auto& object : objects)
		{
			if (object->id == objectId)
				return object.get();
			if (GameObject* child = FindInTree(object->children, objectId))
				return child;
		}
		return nullptr;
	}

	const GameObject* Scene::FindInTree(const std::vector<std::unique_ptr<GameObject>>& objects, ObjectId objectId) const
	{
		for (const auto& object : objects)
		{
			if (object->id == objectId)
				return object.get();
			if (const GameObject* child = FindInTree(object->children, objectId))
				return child;
		}
		return nullptr;
	}

	bool Scene::DestroyInTree(std::vector<std::unique_ptr<GameObject>>& objects, ObjectId objectId)
	{
		for (auto it = objects.begin(); it != objects.end(); ++it)
		{
			if ((*it)->id == objectId)
			{
				objects.erase(it);
				return true;
			}
			if (DestroyInTree((*it)->children, objectId))
				return true;
		}
		return false;
	}
}
