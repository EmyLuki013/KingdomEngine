#ifndef GAME_OBJECTS_WINDOW_H
#define GAME_OBJECTS_WINDOW_H

#include <KingdomEngine/Core/Application.h>
#include <KingdomEngine/ImGui/ImWindow.h>
#include <KingdomEngine/Scene/Scene.h>

#include "KingdomEditor/Project/ProjectManager.h"
#include "KingdomEditor/Utils/SceneEditorContext.h"
#include "KingdomEditor/Utils/SceneFileUtils.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace Editor
{
	class GameObjectsWindow : public KE::ImWindow
	{
	public:
		GameObjectsWindow() : KE::ImWindow("Objects", 0, 19, 172, 334, true, false, KE::DockSide::LEFT_TOP)
		{

		}

		void OnReady() override
		{

		}

		void OnRender() override
		{
			KE::Application* application = KE::Application::GetInstance();
			if (!application)
			{
				ImGui::TextDisabled("Engine application is not available");
				return;
			}

			KE::SceneManager& sceneManager = application->GetSceneManager();
			KE::Scene* scene = sceneManager.GetActiveScene();
			if (!scene)
			{
				ImGui::TextDisabled("No active scene");
				return;
			}

			RenderSceneSelector(sceneManager, *scene);
			scene = sceneManager.GetActiveScene();
			if (!scene)
				return;
			ImGui::Text("%s%s", scene->GetName().c_str(), scene->IsDirty() ? " *" : "");
			if (ImGui::Button("New Scene"))
			{
				CreateNewScene(sceneManager);
				scene = sceneManager.GetActiveScene();
				if (!scene)
					return;
			}
			ImGui::SameLine();
			if (ImGui::Button("Save Scene"))
				SaveScene(sceneManager);

			ImGui::Separator();
			if (ImGui::Button("Add Object"))
			{
				if (KE::GameObject* object = scene->CreateObject("GameObject"))
					selectedSceneObjectId = object->GetId();
			}
			ImGui::SameLine();
			KE::GameObject* selectedObject = scene->FindObject(selectedSceneObjectId);
			ImGui::BeginDisabled(selectedObject == nullptr);
			if (ImGui::Button("Add Child"))
			{
				if (KE::GameObject* object = scene->CreateObject("GameObject", selectedSceneObjectId))
					selectedSceneObjectId = object->GetId();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(selectedObject == nullptr);
			if (ImGui::Button("Delete"))
			{
				scene->DestroyObject(selectedSceneObjectId);
				selectedSceneObjectId = 0;
			}
			ImGui::EndDisabled();

			ImGui::Separator();
			if (scene->GetRootObjects().empty())
				ImGui::TextDisabled("Scene is empty");
			else
			{
				for (const auto& object : scene->GetRootObjects())
					RenderObject(*object);
			}
		}

	private:
		static void RenderSceneSelector(KE::SceneManager& sceneManager, KE::Scene& scene)
		{
			const std::filesystem::path activePath = sceneManager.GetActiveScenePath();
			const std::string preview = activePath.empty() ? scene.GetName() : activePath.filename().string();
			if (!ImGui::BeginCombo("Scene", preview.c_str()))
				return;

			Project* project = ProjectManager::Get().GetLoadedProject();
			if (project)
			{
				const std::filesystem::path directory = std::filesystem::path(project->properties.path) / "scenes";
				std::error_code error;
				if (std::filesystem::exists(directory, error) && !error)
				{
					std::vector<std::filesystem::path> paths;
					std::filesystem::directory_iterator iterator(directory, error);
					const std::filesystem::directory_iterator end;
					while (!error && iterator != end)
					{
						std::error_code entryError;
						if (iterator->is_regular_file(entryError) && !entryError)
						{
							if (SceneFileUtils::IsSceneFile(iterator->path()) ||
								SceneFileUtils::IsLegacySceneFile(iterator->path()))
								paths.push_back(iterator->path());
						}
						else if (entryError)
						{
							LOG_ERROR("Could not inspect scene file: " + entryError.message());
						}

						iterator.increment(error);
					}
					if (error)
						LOG_ERROR("Could not enumerate scene files: " + error.message());

					std::sort(paths.begin(), paths.end());
					for (const auto& path : paths)
					{
						const bool selected = path.lexically_normal() == activePath.lexically_normal();
						if (ImGui::Selectable(path.filename().string().c_str(), selected))
						{
							if (scene.IsDirty() && !SaveScene(sceneManager))
							{
								ImGui::EndCombo();
								return;
							}

							std::filesystem::path loadPath = path;
							std::string migrationError;
							if (SceneFileUtils::IsLegacySceneFile(path))
							{
								if (!SceneFileUtils::MigrateLegacySceneFile(path, loadPath, migrationError))
									LOG_WARN("Could not migrate scene file: " + migrationError);
							}

							std::string loadError;
							if (sceneManager.LoadScene(loadPath, loadError))
							{
								project->properties.startupScene =
									SceneFileUtils::ProjectRelativePath(loadPath, project->properties.path);
								project->SaveKepFile();
								selectedSceneObjectId = 0;
								ImGui::EndCombo();
								return;
							}
							else
								LOG_ERROR("Could not load scene: " + loadError);
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
				}
				else if (error)
					LOG_ERROR("Could not access scene directory: " + error.message());
			}
			ImGui::EndCombo();
		}

		static bool SaveScene(KE::SceneManager& sceneManager)
		{
			Project* project = ProjectManager::Get().GetLoadedProject();
			if (!project)
			{
				LOG_ERROR("Could not save scene: no project is open");
				return false;
			}

			std::string error;
			if (!SceneFileUtils::SaveActiveScene(sceneManager, project->properties.path,
				project->properties.startupScene, error))
			{
				LOG_ERROR("Could not save scene: " + error);
				return false;
			}
			project->SaveKepFile();
			LOG_INFO("Saved scene: " + sceneManager.GetActiveScenePath().string());
			return true;
		}

		static void CreateNewScene(KE::SceneManager& sceneManager)
		{
			const KE::Scene* currentScene = sceneManager.GetActiveScene();
			if (currentScene && currentScene->IsDirty())
			{
				std::string saveError;
				if (!sceneManager.SaveActiveScene(saveError))
				{
					LOG_ERROR("Save the current scene before creating another one: " + saveError);
					return;
				}
			}

			sceneManager.CreateScene("New Scene");
			selectedSceneObjectId = 0;
			Project* project = ProjectManager::Get().GetLoadedProject();
			if (!project)
			{
				LOG_ERROR("Could not create scene: no project is open");
				return;
			}

			std::string error;
			if (!SceneFileUtils::SaveActiveScene(sceneManager, project->properties.path,
				project->properties.startupScene, error))
				LOG_ERROR("Could not create scene: " + error);
			else
			{
				project->SaveKepFile();
			}
		}

		static void RenderObject(KE::GameObject& object)
		{
			ImGui::PushID(std::to_string(object.GetId()).c_str());
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
				ImGuiTreeNodeFlags_OpenOnDoubleClick |
				ImGuiTreeNodeFlags_SpanAvailWidth;
			if (object.GetId() == selectedSceneObjectId)
				flags |= ImGuiTreeNodeFlags_Selected;
			const bool hasChildren = !object.GetChildren().empty();
			if (!hasChildren)
				flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

			const bool open = ImGui::TreeNodeEx(object.GetName().c_str(), flags);
			if (ImGui::IsItemClicked())
				selectedSceneObjectId = object.GetId();
			if (hasChildren && open)
			{
				for (const auto& child : object.GetChildren())
					RenderObject(*child);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	};
}

#endif
