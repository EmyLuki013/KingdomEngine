#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#include <KingdomEngine/Scene/SceneManager.h>

namespace Editor
{
	namespace SceneFileUtils
	{
		inline std::string ToLower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
			{
				return static_cast<char>(std::tolower(character));
			});
			return value;
		}

		inline bool IsSceneFile(const std::filesystem::path& path)
		{
			return ToLower(path.extension().string()) == ".scene";
		}

		inline bool IsLegacySceneFile(const std::filesystem::path& path)
		{
			const std::string filename = ToLower(path.filename().string());
			const std::string suffix = ".scene.json";
			return filename.size() >= suffix.size() &&
				filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
		}

		inline std::string ProjectRelativePath(const std::filesystem::path& path,
			const std::filesystem::path& projectPath)
		{
			const std::filesystem::path relativePath = path.lexically_relative(projectPath);
			return relativePath.empty() ? path.generic_string() : relativePath.generic_string();
		}

		inline bool SaveActiveScene(KE::SceneManager& sceneManager,
			const std::filesystem::path& projectPath, std::string& startupScene, std::string& error)
		{
			error.clear();
			if (!sceneManager.GetActiveScene())
			{
				error = "There is no active scene to save";
				return false;
			}

			std::filesystem::path scenePath = sceneManager.GetActiveScenePath();
			if (scenePath.empty())
			{
				const std::filesystem::path sceneDirectory = projectPath / "scenes";
				std::error_code filesystemError;
				std::filesystem::create_directories(sceneDirectory, filesystemError);
				if (filesystemError)
				{
					error = "Could not create scene directory: " + filesystemError.message();
					return false;
				}

				std::string stem = sceneManager.GetActiveScene()->GetName();
				for (char& character : stem)
				{
					if (std::string("<>:\"/\\|?*").find(character) != std::string::npos)
						character = '_';
				}
				while (!stem.empty() &&
					(stem.back() == '.' || std::isspace(static_cast<unsigned char>(stem.back()))))
					stem.pop_back();
				if (stem.empty() || stem == "." || stem == "..")
					stem = "Scene";

				for (unsigned suffix = 1; suffix < 10000; ++suffix)
				{
					const std::string filename = suffix == 1
						? stem + ".scene"
						: stem + " " + std::to_string(suffix) + ".scene";
					scenePath = sceneDirectory / filename;
					if (!std::filesystem::exists(scenePath, filesystemError))
					{
						if (filesystemError)
						{
							error = "Could not check scene path: " + filesystemError.message();
							return false;
						}
						break;
					}
					if (suffix == 9999)
					{
						error = "Could not find an unused scene filename";
						return false;
					}
				}
			}

			if (!sceneManager.SaveScene(scenePath, error))
				return false;

			startupScene = ProjectRelativePath(scenePath, projectPath);
			return true;
		}

		inline bool MigrateLegacySceneFile(const std::filesystem::path& path,
			std::filesystem::path& migratedPath, std::string& error)
		{
			migratedPath = path;
			error.clear();
			if (!IsLegacySceneFile(path))
				return true;

			std::filesystem::path target = path;
			target.replace_extension();
			std::error_code filesystemError;
			if (std::filesystem::exists(target, filesystemError))
			{
				error = "A scene file already exists at " + target.string();
				return false;
			}
			if (filesystemError)
			{
				error = "Could not check the new scene path: " + filesystemError.message();
				return false;
			}

			std::filesystem::rename(path, target, filesystemError);
			if (filesystemError)
			{
				error = "Could not rename the legacy scene file: " + filesystemError.message();
				return false;
			}

			migratedPath = target;
			return true;
		}
	}
}
