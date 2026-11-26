#ifndef CODE_EDITOR_WINDOW_H
#define CODE_EDITOR_WINDOW_H

#include <KingdomEngine/ImGui/ImWindow.h>
#include <KingdomEngine/Core/IO/File.h>
#include <ImGui/TextEditor.h>

#include "KingdomEditor/Project/ProjectManager.h"
#include "KingdomEditor/Utils/Globals.h"

#include <filesystem>

namespace Editor
{
	class CodeEditorWindow : public KE::ImWindow
	{
	public:
		CodeEditorWindow() : KE::ImWindow("Code Editor", 640, 480, 1366, 98, true, false, KE::DockSide::MAIN)
		{
			properties.flags |= ImGuiWindowFlags_HorizontalScrollbar;
			fileIsLoaded = false;
			fileModified = false;
			cppFile = "";
		}

		void OnReady() override
		{
			std::vector<const char*> identifiers;
			identifiers.push_back("KE");
			
			//load from json file KE identifiers
			KE::File identifiersFile;
			KE::JSON identifiersJson;
			
			std::string jsonFilePath = KE::Core::RESOURCES_DIR + "\\ke_identifiers.json";
			if (identifiersFile.Exists(jsonFilePath))
			{
				//Open KE identifiers json file
				identifiersFile.Open(jsonFilePath, KE::ModeFlags::READ);
				identifiersJson.Parse(identifiersFile.Read(), true);
				identifiersFile.Close();
				
				//load identifiers
				for (auto it = identifiersJson.GetData().MemberBegin(); it != identifiersJson.GetData().MemberEnd(); ++it)
				{
					if (it->value.IsArray())
					{
						auto &obj = it->value;
						for (rapidjson::SizeType i = 0; i < obj.Size(); ++i)
						{
							const rapidjson::Value& identifier = obj[i];
							if (identifier.IsString())
							{
								identifiers.push_back(identifier.GetString());
							}
							else
							{
								LOG_ERROR("Array contains non-string element");
							}
						}

					}
					else
					{
						LOG_ERROR("No array with identifiers was found");
					}
				}
			}
			else
			{
				LOG_ERROR("Unable to load KE API identifiers");
			}
			
			//Load cpp language
			auto lang = TextEditor::LanguageDefinition::CPlusPlus(identifiers);
			cppLanguageDefinition = lang;
			editor.SetLanguageDefinition(cppLanguageDefinition);
		}

		bool OpenFile(const std::string& filePath)
		{
			KE::File file;
			file.Open(filePath, KE::ModeFlags::READ);
			if (!file.IsOpen())
				return false;

			std::string contents = file.Read();
			file.Close();
			if (file.GetFile().bad())
			{
				LOG_ERROR("Could not read file: " + filePath);
				return false;
			}

			if (std::filesystem::path(filePath).extension() == ".lua")
				editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
			else
				editor.SetLanguageDefinition(cppLanguageDefinition);

			editor.SetText(contents);
			cppFile = std::filesystem::path(filePath).string();
			fileIsLoaded = true;
			fileModified = false;
			return true;
		}

		bool SaveFile()
		{
			if (!fileIsLoaded || cppFile.empty())
				return false;

			KE::File file;
			file.Open(cppFile, KE::ModeFlags::WRITE);
			if (!file.IsOpen())
				return false;

			file.Write(editor.GetText());
			file.GetFile().flush();
			const bool writeSucceeded = file.GetFile().good();
			file.Close();
			if (!writeSucceeded)
			{
				LOG_ERROR("Could not save file: " + cppFile);
				return false;
			}

			fileModified = false;
			LOG_INFO("Saved file: " + cppFile);
			return true;
		}

		void OnRender() override
		{
			//load project cpp file
			if (ProjectManager::Get().GetLoadedProject() && !fileIsLoaded)
			{
				std::string projectName = ProjectManager::Get().GetLoadedProject()->properties.name;
				projectName.erase(
					std::remove(projectName.begin(), projectName.end(), ' '),
					projectName.end()
				);
				const std::string mainFile = ProjectManager::Get().GetLoadedProject()->properties.path + "\\src\\" + projectName + ".cpp";
				if (!OpenFile(mainFile))
					return;
			}
			
			//unload cpp file
			if (!ProjectManager::Get().GetLoadedProject() && fileIsLoaded)
			{
				fileIsLoaded = false;
				fileModified = false;
			}
			
			ImGuiIO& io = ImGui::GetIO();
			const bool saveRequested = Globals::CAN_SAVE_FILE ||
				(fileIsLoaded && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
					io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false));
			Globals::CAN_SAVE_FILE = false;
			
			if (KE::ImGuiManager::Get().GetTheme() == KE::DEFAULT)
				editor.SetPalette(TextEditor::GetDarkPalette());
			if (KE::ImGuiManager::Get().GetTheme() == KE::DARK)
				editor.SetPalette(TextEditor::GetDarkPalette());
			if (KE::ImGuiManager::Get().GetTheme() == KE::LIGHT)
				editor.SetPalette(TextEditor::GetLightPalette());
			
			auto cpos = editor.GetCursorPosition();
				const std::string fileName = std::filesystem::path(cppFile).filename().string();
			
			ImGui::Text("%6d/%-6d %6d lines  | %s | %s | %s | %s", cpos.mLine + 1, cpos.mColumn + 1, editor.GetTotalLines(),
			editor.IsOverwrite() ? "Ovr" : "Ins",
			fileModified ? "*" : " ",
				editor.GetLanguageDefinition().mName.c_str(), fileName.c_str());

			editor.Render("TextEditor");
			if (editor.IsTextChanged())
				fileModified = true;
			if (saveRequested)
				SaveFile();
		}
		
	private:
		TextEditor editor;
		bool fileIsLoaded;
		bool fileModified;
		std::string cppFile;
		TextEditor::LanguageDefinition cppLanguageDefinition;
		
	};
}

#endif
