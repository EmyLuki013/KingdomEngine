#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "KingdomEngine/Scene/Scene.h"

namespace KE
{
	class SceneManager
	{
	public:
		SceneManager();

		Scene& CreateScene(const std::string& name = "Untitled");
		void SetAssetRoot(const std::filesystem::path& path);
		bool LoadScene(const std::filesystem::path& path, std::string& error);
		bool SaveScene(const std::filesystem::path& path, std::string& error);
		bool SaveActiveScene(std::string& error);
		void RelocateActiveScenePath(const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath);

		Scene* GetActiveScene() { return activeScene.get(); }
		const Scene* GetActiveScene() const { return activeScene.get(); }
		const std::filesystem::path& GetActiveScenePath() const { return activeScenePath; }

	private:
		std::unique_ptr<Scene> activeScene;
		std::filesystem::path activeScenePath;
		std::filesystem::path assetRoot;
	};
}
