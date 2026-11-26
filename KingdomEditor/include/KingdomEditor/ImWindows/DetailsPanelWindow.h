#ifndef DETAILS_PANEL_WINDOW_H
#define DETAILS_PANEL_WINDOW_H

#include <KingdomEngine/Core/Application.h>
#include <KingdomEngine/ImGui/ImWindow.h>
#include <KingdomEngine/Scene/Scene.h>
#include <KingdomEngine/Scene/ScriptComponent.h>
#include <KingdomEngine/Scene/SpriteComponent.h>

#include "KingdomEditor/Project/ProjectManager.h"
#include "KingdomEditor/Utils/SceneEditorContext.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <vector>

namespace Editor
{
	class DetailsPanelWindow : public KE::ImWindow
	{
	public:
		DetailsPanelWindow() : KE::ImWindow("Properties", 1184, 19, 182, 578, true, false, KE::DockSide::RIGHT)
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

			KE::Scene* scene = application->GetSceneManager().GetActiveScene();
			KE::GameObject* object = scene ? scene->FindObject(selectedSceneObjectId) : nullptr;
			static KE::ObjectId editingObjectId = 0;
			static const KE::Scene* editingScene = nullptr;
			static char nameBuffer[256] = "";
			static std::filesystem::path texturePickerRoot;
			static std::filesystem::path texturePickerDirectory;
			static bool refreshTexturePicker = false;
			static std::filesystem::path scriptPickerRoot;
			static std::vector<std::filesystem::path> scriptPickerScripts;
			static std::string scriptPickerError;
			static bool refreshScriptPicker = false;
			if (!object)
			{
				selectedSceneObjectId = 0;
				editingObjectId = 0;
				editingScene = nullptr;
				ImGui::TextDisabled("Select an object to inspect its properties");
				return;
			}

			if (editingScene != scene || editingObjectId != object->GetId())
			{
				editingScene = scene;
				editingObjectId = object->GetId();
				std::strncpy(nameBuffer, object->GetName().c_str(), sizeof(nameBuffer) - 1);
				nameBuffer[sizeof(nameBuffer) - 1] = '\0';
			}

			if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
				object->SetName(nameBuffer);

			bool active = object->IsActive();
			if (ImGui::Checkbox("Active", &active))
				object->SetActive(active);

			ImGui::Separator();
			KE::Transform& transform = object->GetTransform();
			bool transformChanged = false;
			transformChanged |= ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
			transformChanged |= ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.5f);
			transformChanged |= ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);
			if (transformChanged)
				scene->MarkDirty();

			bool transformEnabled = transform.IsEnabled();
			if (ImGui::Checkbox("Transform Enabled", &transformEnabled))
				transform.SetEnabled(transformEnabled);

			ImGui::Separator();
			KE::SpriteComponent* sprite = object->GetComponent<KE::SpriteComponent>();
			KE::ScriptComponent* script = object->GetComponent<KE::ScriptComponent>();
			if (ImGui::Button("Add Component"))
				ImGui::OpenPopup("AddComponentPopup");
			if (ImGui::BeginPopup("AddComponentPopup"))
			{
				ImGui::MenuItem("Transform (required)", nullptr, true, false);
				if (ImGui::MenuItem("Sprite", nullptr, false, sprite == nullptr))
					sprite = object->AddComponent<KE::SpriteComponent>();
				if (ImGui::MenuItem("Lua Script", nullptr, false, script == nullptr))
					script = object->AddComponent<KE::ScriptComponent>();
				ImGui::EndPopup();
			}

			if (sprite)
			{
				bool spriteEnabled = sprite->IsEnabled();
				if (ImGui::Checkbox("Sprite Enabled", &spriteEnabled))
					sprite->SetEnabled(spriteEnabled);
				ImGui::TextWrapped("Texture: %s",
					sprite->GetTexturePath().empty() ? "(none)" : sprite->GetTexturePath().c_str());
				if (ImGui::Button("Select Texture"))
				{
					const Project* project = ProjectManager::Get().GetLoadedProject();
					if (project)
					{
						texturePickerRoot = std::filesystem::path(project->properties.path).lexically_normal();
						texturePickerDirectory = texturePickerRoot;
						refreshTexturePicker = true;
						ImGui::OpenPopup("Select Sprite Texture");
					}
					else
						LOG_WARN("Open a project before selecting a sprite texture");
				}
				RenderTexturePicker(*sprite, texturePickerRoot, texturePickerDirectory, refreshTexturePicker);
				ImGui::SameLine();
				if (ImGui::Button("Remove Sprite"))
					object->RemoveComponent<KE::SpriteComponent>();
			}

			if (script)
			{
				bool scriptEnabled = script->IsEnabled();
				if (ImGui::Checkbox("Script Enabled", &scriptEnabled))
					script->SetEnabled(scriptEnabled);
				ImGui::TextWrapped("Script: %s",
					script->GetScriptPath().empty() ? "(none)" : script->GetScriptPath().c_str());
				if (ImGui::Button("Select Script"))
				{
					const Project* project = ProjectManager::Get().GetLoadedProject();
					if (project)
					{
						scriptPickerRoot = std::filesystem::path(project->properties.path).lexically_normal();
						refreshScriptPicker = true;
						ImGui::OpenPopup("Select Lua Script");
					}
					else
						LOG_WARN("Open a project before selecting a Lua script");
				}
				RenderScriptPicker(*script, scriptPickerRoot, scriptPickerScripts,
					scriptPickerError, refreshScriptPicker);
				ImGui::SameLine();
				if (ImGui::Button("Remove Script"))
					object->RemoveComponent<KE::ScriptComponent>();
				ImGui::TextDisabled("Scripts run only while the project is playing");
			}
		}

	private:
		static bool IsSupportedTexture(const std::filesystem::path& path)
		{
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
			{
				return static_cast<char>(std::tolower(character));
			});
			return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
				extension == ".bmp" || extension == ".tga";
		}

		static void RenderScriptPicker(KE::ScriptComponent& script,
			const std::filesystem::path& root, std::vector<std::filesystem::path>& scripts,
			std::string& listingError, bool& refreshRequested)
		{
			if (!ImGui::IsPopupOpen("Select Lua Script"))
				return;

			ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_Appearing);
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
				ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (!ImGui::BeginPopupModal("Select Lua Script", nullptr, ImGuiWindowFlags_NoResize))
				return;

			if (ImGui::Button("Refresh"))
				refreshRequested = true;
			ImGui::TextWrapped("%s", root.string().c_str());
			ImGui::Separator();

			if (refreshRequested)
			{
				scripts.clear();
				listingError.clear();
				std::error_code iteratorError;
				std::filesystem::recursive_directory_iterator iterator(
					root, std::filesystem::directory_options::skip_permission_denied, iteratorError);
				const std::filesystem::recursive_directory_iterator end;
				while (!iteratorError && iterator != end)
				{
					std::error_code entryError;
					if (iterator->is_directory(entryError))
					{
						std::string directoryName = iterator->path().filename().string();
						std::transform(directoryName.begin(), directoryName.end(), directoryName.begin(),
							[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
						if (directoryName == "build" || directoryName == ".git")
							iterator.disable_recursion_pending();
					}
					else if (!entryError && iterator->is_regular_file(entryError) && !entryError)
					{
						std::string extension = iterator->path().extension().string();
						std::transform(extension.begin(), extension.end(), extension.begin(),
							[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
						if (extension == ".lua")
							scripts.push_back(iterator->path());
					}

					if (entryError && listingError.empty())
						listingError = "Could not inspect script picker entry: " + entryError.message();
					iterator.increment(iteratorError);
				}
				if (iteratorError)
					listingError = "Could not enumerate project scripts: " + iteratorError.message();

				std::sort(scripts.begin(), scripts.end(), [&root](const auto& left, const auto& right)
				{
					return left.lexically_relative(root).generic_string() <
						right.lexically_relative(root).generic_string();
				});
				refreshRequested = false;
				if (!listingError.empty())
					LOG_ERROR(listingError);
			}

			if (!listingError.empty())
				ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", listingError.c_str());

			ImGui::BeginChild("LuaScriptPickerEntries", ImVec2(530.0f, 290.0f), true);
			for (const auto& path : scripts)
			{
				const std::string relativePath = path.lexically_relative(root).generic_string();
				if (ImGui::Selectable(relativePath.c_str(), relativePath == script.GetScriptPath()))
				{
					script.SetScriptPath(relativePath);
					ImGui::CloseCurrentPopup();
				}
			}
			if (scripts.empty() && listingError.empty())
				ImGui::TextDisabled("No Lua scripts found in this project");
			ImGui::EndChild();

			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		static bool IsWithinDirectory(const std::filesystem::path& root, const std::filesystem::path& path)
		{
			const std::filesystem::path relative = path.lexically_normal().lexically_relative(root.lexically_normal());
			if (relative.empty() || relative.is_absolute())
				return false;
			const auto first = relative.begin();
			return first == relative.end() || *first != "..";
		}

		static void RenderTexturePicker(KE::SpriteComponent& sprite,
			std::filesystem::path& root, std::filesystem::path& currentDirectory, bool& refreshRequested)
		{
			if (!ImGui::IsPopupOpen("Select Sprite Texture"))
				return;

			ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_Appearing);
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (!ImGui::BeginPopupModal("Select Sprite Texture", nullptr, ImGuiWindowFlags_NoResize))
				return;

			if (root.empty())
			{
				ImGui::TextDisabled("No project is open");
			}
			else
			{
				std::error_code directoryError;
				if (!IsWithinDirectory(root, currentDirectory) ||
					!std::filesystem::is_directory(currentDirectory, directoryError))
				{
					currentDirectory = root;
				}

				if (currentDirectory.lexically_normal() != root.lexically_normal())
				{
					if (ImGui::Button("Up"))
						currentDirectory = currentDirectory.parent_path();
					ImGui::SameLine();
				}
				if (ImGui::Button("Refresh"))
					refreshRequested = true;
				ImGui::TextWrapped("%s", currentDirectory.string().c_str());
				ImGui::Separator();

				static std::filesystem::path cachedDirectory;
				static std::vector<std::filesystem::path> cachedDirectories;
				static std::vector<std::filesystem::path> cachedImages;
				static std::string listingError;
				static bool cacheIsValid = false;
				if (refreshRequested || !cacheIsValid || cachedDirectory != currentDirectory)
				{
					cachedDirectory = currentDirectory;
					cachedDirectories.clear();
					cachedImages.clear();
					listingError.clear();

					std::error_code iteratorError;
					std::filesystem::directory_iterator iterator(currentDirectory, iteratorError);
					const std::filesystem::directory_iterator end;
					if (iteratorError)
						listingError = "Could not enumerate texture picker directory: " + iteratorError.message();
					while (!iteratorError && iterator != end)
					{
						std::error_code entryError;
						if (iterator->is_directory(entryError))
							cachedDirectories.push_back(iterator->path());
						else if (!entryError && iterator->is_regular_file(entryError) && !entryError &&
							IsSupportedTexture(iterator->path()))
							cachedImages.push_back(iterator->path());

						if (entryError && listingError.empty())
							listingError = "Could not inspect texture picker entry: " + entryError.message();
						iterator.increment(iteratorError);
					}
					if (iteratorError && listingError.empty())
						listingError = "Could not enumerate texture picker directory: " + iteratorError.message();

					const auto sortByName = [](const std::filesystem::path& left, const std::filesystem::path& right)
					{
						return left.filename().string() < right.filename().string();
					};
					std::sort(cachedDirectories.begin(), cachedDirectories.end(), sortByName);
					std::sort(cachedImages.begin(), cachedImages.end(), sortByName);
					cacheIsValid = true;
					refreshRequested = false;
					if (!listingError.empty())
						LOG_ERROR(listingError);
				}
				if (!listingError.empty())
					ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", listingError.c_str());

				std::filesystem::path nextDirectory;
				std::filesystem::path selectedTexture;
				ImGui::BeginChild("TexturePickerEntries", ImVec2(530.0f, 290.0f), true);
				for (const auto& directory : cachedDirectories)
				{
					const std::string label = "[Folder] " + directory.filename().string();
					if (ImGui::Selectable(label.c_str()))
						nextDirectory = directory;
				}
				for (const auto& image : cachedImages)
				{
					if (ImGui::Selectable(image.filename().string().c_str()))
						selectedTexture = image;
				}
				if (cachedDirectories.empty() && cachedImages.empty() && listingError.empty())
					ImGui::TextDisabled("No image files in this directory");
				ImGui::EndChild();

				if (!nextDirectory.empty())
					currentDirectory = nextDirectory;
				if (!selectedTexture.empty())
				{
					const std::filesystem::path relative =
						selectedTexture.lexically_normal().lexically_relative(root.lexically_normal());
					std::string texturePath = IsWithinDirectory(root, selectedTexture)
						? relative.generic_string()
						: selectedTexture.lexically_normal().string();
					sprite.SetTexturePath(texturePath);
					ImGui::CloseCurrentPopup();
				}
			}

			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	};
}

#endif
