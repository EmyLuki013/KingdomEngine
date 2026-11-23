#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>

#include <KingdomEngine/ImGui/ImMenu.h>
#include <KingdomEngine/Core/Log.h>

#include "KingdomEditor/DialogBox/SaveProjectDialogBox.h"
#include "KingdomEditor/Project/ProjectManager.h"
#include "KingdomEditor/Utils/Globals.h"

namespace Editor
{
	class FileMenu : public KE::ImMenu
	{
	public:
		FileMenu() : KE::ImMenu("File", true)
		{

		}

		void OnRender() override
		{
		    if (ImGui::BeginMenu("New"))
            {
                if (ImGui::MenuItem("Project"))
                {
                    if (ProjectManager::Get().GetLoadedProject())
                    {
                        SaveProjectDialog::SetShow();
                    }
                    else
                    {
                        SET_IM_WINDOW_VISIBLE("Projects", true);
                    }
                }
                ImGui::EndMenu();
            }

		    Separate();

		    if (Item("Open"))
			{

			}

			const auto* project = ProjectManager::Get().GetLoadedProject();
			if (ImGui::MenuItem("Open Project Directory", nullptr, false, project != nullptr))
			{
				const std::string& projectPath = project->properties.path;
				const HINSTANCE result = ShellExecuteA(nullptr, "explore", projectPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				const INT_PTR resultCode = reinterpret_cast<INT_PTR>(result);
				if (resultCode <= 32)
				{
					LOG_ERROR("Failed to open project directory in File Explorer. ShellExecute error: " + std::to_string(resultCode));
				}
			}

			if (Item("Save Project"))
			{
				ProjectManager::Get().GetLoadedProject()->SaveKepFile();
			}
			
			if (Item("Save File"))
			{
				Globals::CAN_SAVE_FILE = true;
			}

			Separate();

			if (Item("Close Project"))
			{
                if (ProjectManager::Get().GetLoadedProject())
                {
                    SaveProjectDialog::SetShow();
                }
                else
                {
                    SET_IM_WINDOW_VISIBLE("Projects", true);
                }
			}

		    Separate();

		    if (Item("Project Config"))
			{
                SET_IM_WINDOW_VISIBLE("Project Configuration", true);
			}

			if (Item("Engine Config"))
			{
				SET_IM_WINDOW_VISIBLE("Engine Configuration", true);
			}

			Separate();

			if (Item("Exit", "Alt+F4"))
			{
				DISPATCH_EVENT(KE::EventType::WindowClose);
			}
		}
	};

	FileMenu fileMenu;
}
