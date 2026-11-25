#ifndef CONTENT_BROWSER_WINDOW_H
#define CONTENT_BROWSER_WINDOW_H

#include <KingdomEngine/Core/Application.h>
#include <KingdomEngine/ImGui/ImWindow.h>
#include <KingdomEngine/ImGui/ImWindowManager.h>
#include <KingdomEngine/Renderer/OpenGL/Sprite.h>
#include <KingdomEngine/Renderer/OpenGL/Texture.h>

#include "KingdomEditor/ImWindows/CodeEditorWindow.h"
#include "KingdomEditor/Project/ProjectManager.h"
#include "KingdomEditor/Utils/Globals.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Editor
{
	class ContentBrowserWindow : public KE::ImWindow
	{
	public:
		ContentBrowserWindow() : KE::ImWindow("Content Browser", 0, 355, 172, 242, true, false, KE::DockSide::LEFT_BOTTOM),
			clipboardIsCut(false)
		{
			newItemName[0] = '\0';
			renameItemName[0] = '\0';
			creatingDirectory = false;
		}

		void OnReady() override
		{
			directoryIcon.LoadTexture(KE::Core::ICONS_DIR + "\\DirectoryIcon.png");
			fileIcon.LoadTexture(KE::Core::ICONS_DIR + "\\FileIcon.png");
			cppIcon.LoadTexture(KE::Core::ICONS_DIR + "\\CppIcon.png");
			jsonIcon.LoadTexture(KE::Core::ICONS_DIR + "\\JsonIcon.png");
			sceneIcon.LoadTexture(KE::Core::ICONS_DIR + "\\SceneIcon.png");
			shadersIcon.LoadTexture(KE::Core::ICONS_DIR + "\\ShadersIcon.png");
			fontIcon.LoadTexture(KE::Core::ICONS_DIR + "\\TffIcon.png");
			imageThumbnails.clear();
		}

		void OnRender() override
		{
			const auto* project = ProjectManager::Get().GetLoadedProject();
			if (!project)
			{
				auto* application = KE::Application::GetInstance();
				if (application)
				{
					const auto droppedFiles = application->GetWindow().TakeDroppedFiles();
					if (!droppedFiles.empty())
						LOG_ERROR("Dropped files were ignored because no project is open");
				}
				projectPath.clear();
				currentDirectory.clear();
				selectedItems.clear();
				renamePopupRequested = false;
				createPopupRequested = false;
				imagePreviewPath.clear();
				imagePreviewOpen = false;
				return;
			}

			if (projectPath != project->properties.path)
			{
				projectPath = project->properties.path;
				currentDirectory = projectPath;
				selectedItems.clear();
				selectionAnchor.clear();
				imagePreviewPath.clear();
				imagePreviewOpen = false;
			}

			if (!std::filesystem::exists(currentDirectory))
			{
				LOG_ERROR("Content Browser directory does not exist: " + currentDirectory.string());
				currentDirectory = projectPath;
			}

			HandleKeyboardShortcuts();

			if (currentDirectory != std::filesystem::path(projectPath) && ImGui::Button("<-"))
				NavigateTo(currentDirectory.parent_path());

			auto* application = KE::Application::GetInstance();
			if (application)
			{
				const std::vector<std::string> droppedFiles = application->GetWindow().TakeDroppedFiles();
				if (!droppedFiles.empty())
				{
					if (ImGui::IsWindowHovered())
						CopyExternalFiles(droppedFiles);
					else
						LOG_INFO("Ignored dropped files because the Content Browser was not under the cursor");
				}
			}

			const float thumbnailSize = 60.0f;
			const float cellSize = thumbnailSize + 5.0f;
			const float panelWidth = ImGui::GetContentRegionAvail().x;
			int columnCount = static_cast<int>(panelWidth / cellSize);
			if (columnCount < 1)
				columnCount = 1;

			std::vector<std::filesystem::directory_entry> entries;
			for (const auto& entry : std::filesystem::directory_iterator(currentDirectory))
			{
				if (entry.is_directory() && ToLower(entry.path().filename().string()) == "build")
					continue;
				entries.push_back(entry);
			}

			std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right)
			{
				const bool leftIsDirectory = left.is_directory();
				const bool rightIsDirectory = right.is_directory();
				if (leftIsDirectory != rightIsDirectory)
					return leftIsDirectory;
				return ToLower(left.path().filename().string()) < ToLower(right.path().filename().string());
			});

			ImGui::Columns(columnCount, 0, false);

			bool enteredDirectory = false;
			for (const auto& entry : entries)
			{
				const std::filesystem::path& path = entry.path();
				const std::string filename = path.filename().string();
				const bool isDirectory = entry.is_directory();
				ImGui::PushID(filename.c_str());

				KE::Sprite* icon = isDirectory ? &directoryIcon : GetFileIcon(path);
				ImTextureID textureId = (ImTextureID)(uintptr_t)icon->GetTextureID();
				if (!isDirectory && IsImageFile(path))
				{
					if (KE::Texture* thumbnail = GetImageThumbnail(path))
						textureId = (ImTextureID)(uintptr_t)thumbnail->GetID();
				}

				const bool selected = IsSelected(path);
				ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.20f, 0.40f, 0.65f, 0.8f) : ImVec4(0, 0, 0, 0));
				const bool clicked = ImGui::ImageButton(filename.c_str(), textureId, { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });
				const bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				if (clicked)
				{
					SelectItem(path, entries);
					UpdateImagePreview(path);
				}
				if (doubleClicked)
				{
					if (isDirectory)
					{
						NavigateTo(path);
						enteredDirectory = true;
					}
					else
						OpenFile(path);
				}

				if (ImGui::BeginDragDropSource())
				{
					if (!IsSelected(path))
						SetSingleSelection(path);

					std::wstring payload;
					for (const auto& selectedPath : selectedItems)
					{
						payload.append(selectedPath.wstring());
						payload.push_back(L'\0');
					}
					ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEMS", payload.data(), payload.size() * sizeof(wchar_t));
					ImGui::Text("%zu item(s)", selectedItems.size());
					ImGui::EndDragDropSource();
				}

				if (isDirectory)
					AcceptInternalDrop(path);

				RenderItemContextMenu(path, isDirectory, "IconContext");
				ImGui::PopStyleColor();

				ImGui::TextWrapped("%s", filename.c_str());
				const bool labelClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
				const bool labelDoubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				if (labelClicked)
				{
					SelectItem(path, entries);
					UpdateImagePreview(path);
				}
				if (labelDoubleClicked)
				{
					if (isDirectory)
					{
						NavigateTo(path);
						enteredDirectory = true;
					}
					else
						OpenFile(path);
				}
				if (isDirectory)
					AcceptInternalDrop(path);
				RenderItemContextMenu(path, isDirectory, "LabelContext");

				ImGui::NextColumn();
				ImGui::PopID();

				if (enteredDirectory)
					break;
			}

			ImGui::Columns(1);
			if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				!ImGui::IsAnyItemHovered())
			{
				selectedItems.clear();
				selectionAnchor.clear();
				imagePreviewPath.clear();
				imagePreviewOpen = false;
			}
			if (!selectedItems.empty())
				ImGui::Text("%zu selected", selectedItems.size());

			RenderCreationMenu();
			RenderCreationPopup();
			RenderRenamePopup();
			RenderImagePreview();
		}

	private:
		static std::string ToLower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
			{
				return static_cast<char>(std::tolower(character));
			});
			return value;
		}

		static std::string GetExtension(const std::filesystem::path& path)
		{
			return ToLower(path.extension().string());
		}

		static bool IsImageFile(const std::filesystem::path& path)
		{
			const std::string extension = GetExtension(path);
			return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
				extension == ".bmp" || extension == ".tga" || extension == ".gif";
		}

		static bool IsCodeFile(const std::filesystem::path& path)
		{
			const std::string extension = GetExtension(path);
			return extension == ".c" || extension == ".cc" || extension == ".cpp" ||
				extension == ".cxx" || extension == ".h" || extension == ".hh" ||
				extension == ".hpp" || extension == ".hxx" || extension == ".inl" ||
				extension == ".glsl" || extension == ".vert" || extension == ".frag" ||
				extension == ".txt" || extension == ".json" || extension == ".xml" ||
				extension == ".yaml" || extension == ".yml";
		}

		KE::Sprite* GetFileIcon(const std::filesystem::path& path)
		{
			const std::string extension = GetExtension(path);
			if (extension == ".c" || extension == ".cc" || extension == ".cpp" ||
				extension == ".cxx" || extension == ".h" || extension == ".hh" ||
				extension == ".hpp" || extension == ".hxx" || extension == ".inl")
				return &cppIcon;
			if (extension == ".json")
				return &jsonIcon;
			if (extension == ".scene")
				return &sceneIcon;
			if (extension == ".vert" || extension == ".frag" || extension == ".glsl" ||
				extension == ".geom" || extension == ".tesc" || extension == ".tese" ||
				extension == ".comp")
				return &shadersIcon;
			if (extension == ".ttf" || extension == ".ttc" || extension == ".otf" || extension == ".tff")
				return &fontIcon;
			return &fileIcon;
		}

		KE::Texture* GetImageThumbnail(const std::filesystem::path& path)
		{
			const std::string key = path.string();
			auto it = imageThumbnails.find(key);
			if (it != imageThumbnails.end())
				return it->second.get();

			auto thumbnail = std::make_unique<KE::Texture>();
			thumbnail->LoadTexture(key, "diffuse", 0, GL_UNSIGNED_BYTE);
			if (thumbnail->GetID() == 0)
				return nullptr;

			return imageThumbnails.emplace(key, std::move(thumbnail)).first->second.get();
		}

		bool IsSelected(const std::filesystem::path& path) const
		{
			return std::find(selectedItems.begin(), selectedItems.end(), path) != selectedItems.end();
		}

		void UpdateImagePreview(const std::filesystem::path& path)
		{
			if (IsImageFile(path) && IsSelected(path))
			{
				imagePreviewPath = path;
				imagePreviewOpen = true;
			}
			else
			{
				imagePreviewPath.clear();
				imagePreviewOpen = false;
			}
		}

		void NavigateTo(const std::filesystem::path& path)
		{
			currentDirectory = path;
			selectedItems.clear();
			selectionAnchor.clear();
			imagePreviewPath.clear();
			imagePreviewOpen = false;
		}

		void HandleKeyboardShortcuts()
		{
			if (!ImGui::IsWindowFocused() || ImGui::IsAnyItemActive())
				return;

			const ImGuiIO& io = ImGui::GetIO();
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
				SetClipboard(false);
			else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false))
				SetClipboard(true);
			else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
				PasteItems(currentDirectory);
			else if (!selectedItems.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
				DeleteSelectedItems();
		}

		void SetSingleSelection(const std::filesystem::path& path)
		{
			selectedItems.assign(1, path);
			selectionAnchor = path;
		}

		void SelectItem(const std::filesystem::path& path, const std::vector<std::filesystem::directory_entry>& entries)
		{
			const ImGuiIO& io = ImGui::GetIO();
			if (io.KeyShift && !selectionAnchor.empty())
			{
				auto anchor = std::find_if(entries.begin(), entries.end(), [this](const auto& entry)
				{
					return entry.path() == selectionAnchor;
				});
				auto target = std::find_if(entries.begin(), entries.end(), [&path](const auto& entry)
				{
					return entry.path() == path;
				});
				if (anchor != entries.end() && target != entries.end())
				{
					if (!io.KeyCtrl)
						selectedItems.clear();
					const auto first = std::min(anchor, target);
					const auto last = std::max(anchor, target);
					for (auto it = first; it != std::next(last); ++it)
					{
						if (!IsSelected(it->path()))
							selectedItems.push_back(it->path());
					}
					return;
				}
			}

			if (io.KeyCtrl)
			{
				auto selected = std::find(selectedItems.begin(), selectedItems.end(), path);
				if (selected == selectedItems.end())
					selectedItems.push_back(path);
				else
					selectedItems.erase(selected);
				selectionAnchor = path;
				return;
			}

			SetSingleSelection(path);
		}

		void RenderItemContextMenu(const std::filesystem::path& path, bool isDirectory, const char* popupId)
		{
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsSelected(path))
				SetSingleSelection(path);

			if (!ImGui::BeginPopupContextItem(popupId, ImGuiPopupFlags_MouseButtonRight))
				return;

			if (ImGui::MenuItem("Delete", "Del", false, !selectedItems.empty()))
				DeleteSelectedItems();
			if (ImGui::MenuItem("Rename", nullptr, false, true))
				StartRename(path);
			if (ImGui::MenuItem("Copy", "Ctrl+C", false, !selectedItems.empty()))
				SetClipboard(false);
			if (ImGui::MenuItem("Cut", "Ctrl+X", false, !selectedItems.empty()))
				SetClipboard(true);
			if (ImGui::MenuItem("Paste", "Ctrl+V", false, !clipboardItems.empty()))
				PasteItems(isDirectory ? path : path.parent_path());

			ImGui::EndPopup();
		}

		void RenderCreationMenu()
		{
			if (ImGui::BeginPopupContextWindow("ContentBrowserContextMenu",
				ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
			{
				if (ImGui::MenuItem("Create Folder"))
					StartCreateItem(true);
				if (ImGui::MenuItem("Create File"))
					StartCreateItem(false);
				if (ImGui::MenuItem("Paste", "Ctrl+V", false, !clipboardItems.empty()))
					PasteItems(currentDirectory);
				ImGui::EndPopup();
			}
		}

		void StartCreateItem(bool isDirectory)
		{
			creatingDirectory = isDirectory;
			const char* defaultName = creatingDirectory ? "New Folder" : "NewFile.cpp";
			std::strncpy(newItemName, defaultName, sizeof(newItemName) - 1);
			newItemName[sizeof(newItemName) - 1] = '\0';
			createPopupRequested = true;
		}

		void RenderCreationPopup()
		{
			if (createPopupRequested)
			{
				ImGui::OpenPopup("Create Content Browser Item");
				createPopupRequested = false;
			}

			if (!ImGui::BeginPopupModal("Create Content Browser Item", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
				return;

			ImGui::Text("%s name:", creatingDirectory ? "Folder" : "File");
			ImGui::InputText("##NewItemName", newItemName, sizeof(newItemName));
			if (ImGui::Button("Create") && CreateNewItem())
				ImGui::CloseCurrentPopup();
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		bool IsValidName(const std::string& name) const
		{
			if (name.empty() || name == "." || name == "..")
				return false;
			const std::filesystem::path relativePath(name);
			if (relativePath.is_absolute() || relativePath.has_root_path() ||
				relativePath.has_parent_path() || relativePath.filename() != relativePath)
				return false;
			return name.find_first_of("<>:\"/\\|?*") == std::string::npos;
		}

		bool CreateNewItem()
		{
			const std::string name(newItemName);
			if (!IsValidName(name))
			{
				LOG_ERROR("Enter a valid name without a directory path");
				return false;
			}

			const std::filesystem::path newPath = currentDirectory / name;
			std::error_code error;
			if (std::filesystem::exists(newPath, error))
			{
				LOG_ERROR("An item with that name already exists: " + newPath.string());
				return false;
			}
			if (error)
			{
				LOG_ERROR("Could not check the new item path: " + error.message());
				return false;
			}

			if (creatingDirectory)
			{
				if (!std::filesystem::create_directory(newPath, error))
				{
					LOG_ERROR("Could not create folder: " + (error ? error.message() : newPath.string()));
					return false;
				}
			}
			else
			{
				std::ofstream file(newPath, std::ios::binary);
				if (!file)
				{
					LOG_ERROR("Could not create file: " + newPath.string());
					return false;
				}
				file.close();
				if (!file)
				{
					LOG_ERROR("Could not finish creating file: " + newPath.string());
					return false;
				}
			}

			InvalidateThumbnailCache(newPath);
			LOG_INFO(std::string(creatingDirectory ? "Created folder: " : "Created file: ") + newPath.string());
			return true;
		}

		void StartRename(const std::filesystem::path& path)
		{
			renamePath = path;
			const std::string filename = path.filename().string();
			std::strncpy(renameItemName, filename.c_str(), sizeof(renameItemName) - 1);
			renameItemName[sizeof(renameItemName) - 1] = '\0';
			renamePopupRequested = true;
		}

		void RenderRenamePopup()
		{
			if (renamePopupRequested)
			{
				ImGui::OpenPopup("Rename Content Browser Item");
				renamePopupRequested = false;
			}

			if (!ImGui::BeginPopupModal("Rename Content Browser Item", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
				return;

			ImGui::InputText("##RenameItemName", renameItemName, sizeof(renameItemName));
			if (ImGui::Button("Rename"))
			{
				const std::string newName(renameItemName);
				if (IsValidName(newName))
				{
					const std::filesystem::path newPath = renamePath.parent_path() / newName;
					std::error_code error;
					if (renamePath.lexically_normal() == newPath.lexically_normal())
						ImGui::CloseCurrentPopup();
					else if (std::filesystem::exists(newPath, error))
						LOG_ERROR("An item with that name already exists: " + newPath.string());
					else if (error)
						LOG_ERROR("Could not check the rename path: " + error.message());
					else
					{
						std::filesystem::rename(renamePath, newPath, error);
						if (error)
							LOG_ERROR("Could not rename item: " + error.message());
						else
						{
							InvalidateThumbnailCache(renamePath);
							InvalidateThumbnailCache(newPath);
							LOG_INFO("Renamed item to: " + newPath.string());
							selectedItems.clear();
							selectionAnchor.clear();
							imagePreviewPath.clear();
							imagePreviewOpen = false;
							ImGui::CloseCurrentPopup();
						}
					}
				}
				else
					LOG_ERROR("Enter a valid name without a directory path");
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		void RenderImagePreview()
		{
			if (!imagePreviewOpen || imagePreviewPath.empty())
				return;

			KE::Texture* texture = GetImageThumbnail(imagePreviewPath);
			if (!texture || texture->GetWidth() <= 0 || texture->GetHeight() <= 0)
				return;

			ImGui::SetNextWindowSize(ImVec2(460.0f, 360.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Image Preview", &imagePreviewOpen, ImGuiWindowFlags_AlwaysAutoResize))
			{
				const std::string filename = imagePreviewPath.filename().string();
				ImGui::TextUnformatted(filename.c_str());

				const float maxWidth = 440.0f;
				const float maxHeight = 300.0f;
				const float scale = std::min({ 1.0f, maxWidth / texture->GetWidth(), maxHeight / texture->GetHeight() });
				const ImVec2 imageSize(texture->GetWidth() * scale, texture->GetHeight() * scale);
				ImGui::Image((ImTextureID)(uintptr_t)texture->GetID(), imageSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
			}
			ImGui::End();
			if (!imagePreviewOpen)
				imagePreviewPath.clear();
		}

		void SetClipboard(bool cut)
		{
			clipboardItems = selectedItems;
			clipboardIsCut = cut;
		}

		bool CopyPath(const std::filesystem::path& source, const std::filesystem::path& destination)
		{
			std::error_code error;
			if (std::filesystem::exists(destination, error))
			{
				LOG_ERROR("Paste destination already exists: " + destination.string());
				return false;
			}
			if (error)
			{
				LOG_ERROR("Could not check paste destination: " + error.message());
				return false;
			}

			if (std::filesystem::is_directory(source, error))
				std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive, error);
			else if (!error)
				std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);

			if (error)
			{
				LOG_ERROR("Could not copy " + source.string() + ": " + error.message());
				return false;
			}
			InvalidateThumbnailCache(destination);
			LOG_INFO("Copied " + source.string() + " to " + destination.string());
			return true;
		}

		void InvalidateThumbnailCache(const std::filesystem::path& path)
		{
			const std::filesystem::path normalizedPath = path.lexically_normal();
			for (auto it = imageThumbnails.begin(); it != imageThumbnails.end();)
			{
				const std::filesystem::path cachedPath = std::filesystem::path(it->first).lexically_normal();
				const std::filesystem::path relativePath = cachedPath.lexically_relative(normalizedPath);
				const bool matches = cachedPath == normalizedPath ||
					(!relativePath.empty() && !relativePath.is_absolute() && *relativePath.begin() != "..");
				if (matches)
				{
					it->second->Delete();
					it = imageThumbnails.erase(it);
				}
				else
					++it;
			}
		}

		bool MovePath(const std::filesystem::path& source, const std::filesystem::path& destination)
		{
			if (source.lexically_normal() == destination.lexically_normal())
				return false;

			std::error_code error;
			if (std::filesystem::exists(destination, error))
			{
				LOG_ERROR("Move destination already exists: " + destination.string());
				return false;
			}
			if (error)
			{
				LOG_ERROR("Could not check move destination: " + error.message());
				return false;
			}

			if (std::filesystem::is_directory(source, error) && !error)
			{
				const std::filesystem::path relativeTarget = std::filesystem::relative(destination, source, error);
				if (!error && !relativeTarget.empty() && *relativeTarget.begin() != ".." && !relativeTarget.is_absolute())
				{
					LOG_ERROR("Cannot move a folder into itself");
					return false;
				}
			}
			if (error)
			{
				LOG_ERROR("Could not inspect move source: " + error.message());
				return false;
			}

			std::filesystem::rename(source, destination, error);
			if (error)
			{
				error.clear();
				if (!CopyPath(source, destination))
					return false;
				std::filesystem::remove_all(source, error);
				if (error)
				{
					LOG_ERROR("Copied item but could not remove its original: " + error.message());
					return false;
				}
			}

			InvalidateThumbnailCache(source);
			InvalidateThumbnailCache(destination);
			LOG_INFO("Moved " + source.string() + " to " + destination.string());
			return true;
		}

		void DeleteSelectedItems()
		{
			for (const auto& path : selectedItems)
			{
				InvalidateThumbnailCache(path);
				std::error_code error;
				std::filesystem::remove_all(path, error);
				if (error)
					LOG_ERROR("Could not delete " + path.string() + ": " + error.message());
				else
					LOG_INFO("Deleted " + path.string());
			}
			selectedItems.clear();
			selectionAnchor.clear();
			imagePreviewPath.clear();
			imagePreviewOpen = false;
			if (!std::filesystem::exists(currentDirectory))
				currentDirectory = projectPath;
		}

		void PasteItems(const std::filesystem::path& destinationDirectory)
		{
			std::vector<std::filesystem::path> remaining;
			for (const auto& source : clipboardItems)
			{
				const std::filesystem::path destination = destinationDirectory / source.filename();
				if (clipboardIsCut)
				{
					if (!MovePath(source, destination))
						remaining.push_back(source);
				}
				else
					CopyPath(source, destination);
			}
			if (clipboardIsCut)
				clipboardItems = std::move(remaining);
		}

		void AcceptInternalDrop(const std::filesystem::path& destinationDirectory)
		{
			if (!ImGui::BeginDragDropTarget())
				return;

			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEMS");
			if (payload && payload->Data && payload->DataSize % sizeof(wchar_t) == 0)
			{
				const wchar_t* item = static_cast<const wchar_t*>(payload->Data);
				const wchar_t* end = item + payload->DataSize / sizeof(wchar_t);
				while (item < end && *item)
				{
					const std::filesystem::path source(item);
					MovePath(source, destinationDirectory / source.filename());
					item += std::wcslen(item) + 1;
				}
			}
			ImGui::EndDragDropTarget();
		}

		void CopyExternalFiles(const std::vector<std::string>& droppedFiles)
		{
			for (const auto& droppedFile : droppedFiles)
			{
				const std::filesystem::path source = std::filesystem::u8path(droppedFile);
				std::error_code error;
				if (!std::filesystem::is_regular_file(source, error))
				{
					LOG_ERROR("Dropped item is not a readable file: " + droppedFile);
					continue;
				}
				CopyPath(source, currentDirectory / source.filename());
			}
		}

		void OpenFile(const std::filesystem::path& path)
		{
			if (GetExtension(path) == ".kep")
			{
				Globals::PROJECT_CHANGED = true;
				SET_IM_WINDOW_VISIBLE("Project Configuration", true);
				return;
			}

			if (!IsCodeFile(path))
				return;

			auto* codeEditor = dynamic_cast<CodeEditorWindow*>(KE::ImWindowManager::Get().GetWindow("Code Editor"));
			if (!codeEditor)
			{
				LOG_ERROR("Code Editor window was not found");
				return;
			}

			if (codeEditor->OpenFile(path.string()))
				KE::ImWindowManager::Get().FocusWindow("Code Editor");
		}

		std::filesystem::path currentDirectory;
		std::filesystem::path projectPath;
		std::filesystem::path selectionAnchor;
		std::filesystem::path renamePath;
		std::filesystem::path imagePreviewPath;
		std::vector<std::filesystem::path> selectedItems;
		std::vector<std::filesystem::path> clipboardItems;
		KE::Sprite directoryIcon;
		KE::Sprite fileIcon;
		KE::Sprite cppIcon;
		KE::Sprite jsonIcon;
		KE::Sprite sceneIcon;
		KE::Sprite shadersIcon;
		KE::Sprite fontIcon;
		std::unordered_map<std::string, std::unique_ptr<KE::Texture>> imageThumbnails;
		char newItemName[256];
		char renameItemName[256];
		bool creatingDirectory;
		bool clipboardIsCut;
		bool renamePopupRequested = false;
		bool createPopupRequested = false;
		bool imagePreviewOpen = false;
	};
}

#endif
