#ifndef DETAILS_PANEL_WINDOW_H
#define DETAILS_PANEL_WINDOW_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commdlg.h>

#include <KingdomEngine/Core/Application.h>
#include <KingdomEngine/ImGui/ImWindow.h>
#include <KingdomEngine/Scene/Scene.h>
#include <KingdomEngine/Scene/SpriteComponent.h>

#include "KingdomEditor/Project/ProjectManager.h"
#include "KingdomEditor/Utils/SceneEditorContext.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>

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
			if (ImGui::Button("Add Component"))
				ImGui::OpenPopup("AddComponentPopup");
			if (ImGui::BeginPopup("AddComponentPopup"))
			{
				ImGui::MenuItem("Transform (required)", nullptr, true, false);
				if (ImGui::MenuItem("Sprite", nullptr, false, sprite == nullptr))
					sprite = object->AddComponent<KE::SpriteComponent>();
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
					SelectSpriteTexture(*sprite);
				ImGui::SameLine();
				if (ImGui::Button("Remove Sprite"))
					object->RemoveComponent<KE::SpriteComponent>();
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

		static void SelectSpriteTexture(KE::SpriteComponent& sprite)
		{
			std::array<char, 32768> selectedFile = {};
			const char filter[] = "Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.tga)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0";

			OPENFILENAMEA dialog = {};
			dialog.lStructSize = sizeof(dialog);
			dialog.lpstrFilter = filter;
			dialog.lpstrFile = selectedFile.data();
			dialog.nMaxFile = static_cast<DWORD>(selectedFile.size());
			dialog.lpstrDefExt = "png";
			dialog.lpstrTitle = "Select Sprite Texture";
			dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

			const Project* project = ProjectManager::Get().GetLoadedProject();
			const std::string projectPath = project ? project->properties.path : std::string();
			if (!projectPath.empty())
				dialog.lpstrInitialDir = projectPath.c_str();

			if (GetOpenFileNameA(&dialog))
			{
				const std::filesystem::path texturePath(selectedFile.data());
				if (!IsSupportedTexture(texturePath))
				{
					LOG_WARN("Unsupported sprite texture format: " + texturePath.extension().string());
					return;
				}

				sprite.SetTexturePath(texturePath.lexically_normal().string());
				return;
			}

			const DWORD error = CommDlgExtendedError();
			if (error != 0)
				LOG_ERROR("Could not open the texture picker. Common dialog error: " + std::to_string(error));
		}
	};
}

#endif
