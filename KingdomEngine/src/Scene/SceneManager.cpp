#include "KingdomEngine/Scene/SceneManager.h"

namespace KE
{
	SceneManager::SceneManager()
		: activeScene(new Scene())
	{
	}

	Scene& SceneManager::CreateScene(const std::string& name)
	{
		activeScene.reset(new Scene(name));
		activeScene->SetAssetRoot(assetRoot);
		activeScenePath.clear();
		return *activeScene;
	}

	void SceneManager::SetAssetRoot(const std::filesystem::path& path)
	{
		assetRoot = path;
		if (activeScene)
			activeScene->SetAssetRoot(assetRoot);
	}

	bool SceneManager::LoadScene(const std::filesystem::path& path, std::string& error)
	{
		std::unique_ptr<Scene> loadedScene = Scene::LoadFromFile(path, error);
		if (!loadedScene)
			return false;

		activeScene = std::move(loadedScene);
		activeScene->SetAssetRoot(assetRoot);
		activeScenePath = path;
		return true;
	}

	bool SceneManager::SaveScene(const std::filesystem::path& path, std::string& error)
	{
		if (!activeScene)
		{
			error = "There is no active scene to save";
			return false;
		}
		if (!activeScene->SaveToFile(path, error))
			return false;

		activeScenePath = path;
		activeScene->ClearDirty();
		return true;
	}

	bool SceneManager::SaveActiveScene(std::string& error)
	{
		if (activeScenePath.empty())
		{
			error = "The active scene does not have a file path";
			return false;
		}
		return SaveScene(activeScenePath, error);
	}

	void SceneManager::RelocateActiveScenePath(const std::filesystem::path& oldPath,
		const std::filesystem::path& newPath)
	{
		if (activeScenePath.empty())
			return;

		const std::filesystem::path normalizedOldPath = oldPath.lexically_normal();
		const std::filesystem::path normalizedActivePath = activeScenePath.lexically_normal();
		if (normalizedActivePath == normalizedOldPath)
		{
			activeScenePath = newPath;
			return;
		}

		const std::filesystem::path relative =
			normalizedActivePath.lexically_relative(normalizedOldPath);
		if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
			activeScenePath = newPath / relative;
	}
}
