#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "KingdomEngine/Scene/GameObject.h"

namespace KE
{
	class Scene
	{
	public:
		explicit Scene(const std::string& sceneName = "Untitled");
		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		const std::string& GetName() const { return name; }
		void SetName(const std::string& value);
		const std::filesystem::path& GetAssetRoot() const { return assetRoot; }
		void SetAssetRoot(const std::filesystem::path& value) { assetRoot = value; }
		bool IsDirty() const { return dirty; }
		void MarkDirty() { dirty = true; }
		void ClearDirty() { dirty = false; }

		GameObject* CreateObject(const std::string& objectName, ObjectId parentId = 0);
		bool DestroyObject(ObjectId objectId);
		GameObject* FindObject(ObjectId objectId);
		const GameObject* FindObject(ObjectId objectId) const;
		GameObject* FindObjectByName(const std::string& objectName);
		const std::vector<std::unique_ptr<GameObject>>& GetRootObjects() const { return roots; }
		void Update(float deltaTime);
		void Render(const glm::mat4& viewProjection);

		bool SaveToFile(const std::filesystem::path& path, std::string& error) const;
		static std::unique_ptr<Scene> LoadFromFile(const std::filesystem::path& path, std::string& error);

	private:
		GameObject* FindInTree(const std::vector<std::unique_ptr<GameObject>>& objects, ObjectId objectId);
		const GameObject* FindInTree(const std::vector<std::unique_ptr<GameObject>>& objects, ObjectId objectId) const;
		static bool DestroyInTree(std::vector<std::unique_ptr<GameObject>>& objects, ObjectId objectId);

		std::string name;
		std::filesystem::path assetRoot;
		ObjectId nextObjectId;
		bool dirty;
		std::vector<std::unique_ptr<GameObject>> roots;
	};
}
