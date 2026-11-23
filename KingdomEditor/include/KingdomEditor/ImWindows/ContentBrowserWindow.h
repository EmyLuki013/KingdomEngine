#ifndef CONTENT_BROWSER_WINDOW_H
#define CONTENT_BROWSER_WINDOW_H

#include <KingdomEngine/ImGui/ImWindow.h>
#include <KingdomEngine/Renderer/OpenGL/Sprite.h>

#include "KingdomEditor/Project/ProjectManager.h"

namespace Editor
{
	class ContentBrowserWindow : public KE::ImWindow
	{
	public:
		ContentBrowserWindow() : KE::ImWindow("Content Browser", 0, 355, 172, 242, true, false, KE::DockSide::LEFT_BOTTOM)
		{
			currentDirectory = "";
			projectPath = "";
		}

		void OnReady() override
		{
			//directoryIcon.LoadTexture(KE::Core::ICONS_DIR + "\\DirectoryIcon.png", "diffuse", 0, GL_UNSIGNED_BYTE);
			//fileIcon.LoadTexture(KE::Core::ICONS_DIR + "\\FileIcon.png", "diffuse", 0, GL_UNSIGNED_BYTE);
			directoryIcon.LoadTexture(KE::Core::ICONS_DIR + "\\DirectoryIcon.png");
			Globals::fileIcon.LoadTexture(KE::Core::ICONS_DIR + "\\FileIcon.png");
		}

		void OnRender() override
		{
			//load project dir
			if (ProjectManager::Get().GetLoadedProject() && projectPath.empty())
			{
				projectPath = ProjectManager::Get().GetLoadedProject()->properties.path;
				currentDirectory = projectPath;
				
				LOG_INFO("Carregado path do projeto");
				LOG_INFO(currentDirectory);
			}
			
			if (ProjectManager::Get().GetLoadedProject() && !projectPath.empty())
			{
				if (currentDirectory != std::filesystem::path(projectPath))
				{
					if (ImGui::Button("<-"))
					{
						currentDirectory = currentDirectory.parent_path();
					}
				}
				
				static float padding = 5.0f;
				static float thumbnailSize = 60.0f;
				float cellSize = thumbnailSize + padding;

				float panelWidth = ImGui::GetContentRegionAvail().x;
				int columnCount = (int)(panelWidth / cellSize);
				if (columnCount < 1)
					columnCount = 1;

				ImGui::Columns(columnCount, 0, false);
				
				for (auto& directoryEntry : std::filesystem::directory_iterator(currentDirectory))
				{
					const auto& path = directoryEntry.path();
					std::string filenameString = path.filename().string();
					
					ImGui::PushID(filenameString.c_str());
					KE::Sprite* icon = directoryEntry.is_directory()
						? &directoryIcon
						: &Globals::fileIcon;
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
					ImGui::ImageButton(filenameString.c_str(), (ImTextureID)(uintptr_t)icon->GetTextureID(), { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });


					if (ImGui::BeginDragDropSource())
					{
						std::filesystem::path relativePath(path);
						const wchar_t* itemPath = relativePath.c_str();
						ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
						ImGui::EndDragDropSource();
					}

					ImGui::PopStyleColor();
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						if (directoryEntry.is_directory())
							currentDirectory /= path.filename();

					}
					ImGui::TextWrapped(filenameString.c_str());

					ImGui::NextColumn();

					ImGui::PopID();
				}
				
				ImGui::Columns(1);
			}
		}
		
	private:
		std::filesystem::path currentDirectory;
		std::filesystem::path projectPath;
		
		//KE::Texture directoryIcon;
		//KE::Texture fileIcon;
		KE::Sprite directoryIcon;
		
	};
}

#endif
