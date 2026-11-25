#ifndef SAVE_PROJECT_DIALOG_BOX_H
#define SAVE_PROJECT_DIALOG_BOX_H

#include <KingdomEngine/ImGui/ImDialogBox.h>
#include <KingdomEngine/ImGui/ImWindowManager.h>
#include <KingdomEngine/Core/Application.h>
#include <KingdomEngine/Core/Log.h>

#include "KingdomEditor/Project/ProjectManager.h"
#include "KingdomEditor/Utils/SceneFileUtils.h"

namespace Editor
{
    class SaveProjectDialog
    {
    public:
        static KE::DialogResult Show()
        {
            if (showing)
            {
                KE::DialogResult dialogResult = KE::DialogResult::None;

                ImGui::OpenPopup("Save Changes?");
                KE::DialogResult result = KE::ImDialogBox::ShowSaveDialog("Do you want to save changes to this project?");
                if (result != KE::DialogResult::None)
                {
                    dialogResult = result;
                }

                switch (dialogResult)
                {
                case KE::DialogResult::Save:
                {
                    Project* project = ProjectManager::Get().GetLoadedProject();
                    KE::Application* application = KE::Application::GetInstance();
                    if (project && application)
                    {
                        std::string error;
                        if (!SceneFileUtils::SaveActiveScene(application->GetSceneManager(),
                            project->properties.path, project->properties.startupScene, error))
                        {
                            LOG_ERROR("Could not save active scene with the project: " + error);
                            showing = false;
                            return KE::DialogResult::Cancel;
                        }
                        project->SaveKepFile();
                    }
                    ProjectManager::Get().UnloadProject();
                    showing = false;
                    SET_IM_WINDOW_VISIBLE("Projects", true);
                    break;
                }
                case KE::DialogResult::DontSave:
                    if (KE::Application* application = KE::Application::GetInstance())
                        application->GetSceneManager().CreateScene("Untitled");
                    ProjectManager::Get().UnloadProject();
                    showing = false;
                    SET_IM_WINDOW_VISIBLE("Projects", true);
                    break;
                case KE::DialogResult::Cancel:
                    showing = false;
                    break;
                default:
                    break;
                }

                return dialogResult;
            }

            return KE::DialogResult::None;
        }

        static void SetShow()
        {
            showing = true;
        }

        static bool IsShowing() { return showing; }

    private:
        static bool showing;

    };
}

#endif
