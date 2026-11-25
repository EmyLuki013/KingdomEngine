#include "KingdomEditor/EditorApplication.h"
#include "KingdomEditor/Project/ProjectManager.h"
#include "KingdomEditor/DialogBox/SaveProjectDialogBox.h"
#include "KingdomEditor/DialogBox/ExcludeProjectDialogBox.h"
#include "KingdomEditor/DialogBox/BuildErrorDialogBox.h"
#include "KingdomEditor/Utils/Globals.h"
#include "KingdomEditor/Utils/SceneEditorContext.h"
#include "KingdomEditor/Utils/SceneFileUtils.h"
#include "KingdomEditor/Project/ProjectBuild.h"

namespace Editor
{
    bool SaveProjectDialog::showing = false;
    bool ExcludeProjectDialog::showing = false;
    bool BuildErrorDialog::showing = false;
    std::string ExcludeProjectDialog::projectNameToRemove = "";

    EditorApplication::EditorApplication() : KE::Application(false)
    {
        closeAfterSave = false;

        ActivateImGui();

        //search fonts and load default font
        KE::FontManager::Get().FindFonts(KE::Core::FONTS_DIR, ".ttf");

        for (const auto &font : KE::FontManager::Get().GetFonts())
        {
            if (font->properties.filePath == KE::Core::FONTS_DIR + "\\Roboto-Regular.ttf")
            {
                font->properties.name = "Default";
                if (KE::ImGuiManager::Get().IsEnabled())
                    KE::ImGuiManager::Get().LoadFont(font);
            }
            else if (font->properties.filePath == KE::Core::FONTS_DIR + "\\Roboto-Bold.ttf")
            {
                font->properties.name = "Default Bold";
            }
        }
    }

    EditorApplication::~EditorApplication()
    {

    }

    void EditorApplication::OnReady()
    {
        //check KE dir
        KE::Directory keDir;
        if (!keDir.DirExists(KE::Core::KE_DIRECTORY))
        {
            keDir.Create(KE::Core::KE_DIRECTORY);
            LOG_INFO("KE directory created");
        }

        //check projects dir
        KE::Directory projectsDir;
        if (!projectsDir.DirExists(Globals::KE_PROJECTS_DIRECTORY))
        {
            projectsDir.Create(Globals::KE_PROJECTS_DIRECTORY);
            LOG_INFO("Projects directory created");
        }

        LOG_WARN(keDir.GetCurrentDir());

        //open and load projects data file
        ProjectManager::Get().OpenProjectsFile();

        if (KE::ImGuiManager::Get().IsEnabled())
            ONREADY_ALL_IM_WINDOW();
    }

    void EditorApplication::OnUpdate()
    {
        auto* project = ProjectManager::Get().GetLoadedProject();
        if (project)
        {
            std::string newTitle = "Kingdom Engine - " + project->properties.name;
            GetWindow().SetTitle(newTitle);
            KE::Renderer::GetClearColor() = project->properties.backgroundColor;

            const std::filesystem::path newProjectPath = project->properties.path;
            if (sceneProjectPath != newProjectPath)
            {
                auto& sceneManager = GetSceneManager();
                if (!sceneProjectPath.empty())
                {
                    const KE::Scene* currentScene = sceneManager.GetActiveScene();
                    if (currentScene && currentScene->IsDirty() && !sceneManager.GetActiveScenePath().empty())
                    {
                        std::string saveError;
                        if (!sceneManager.SaveActiveScene(saveError))
                            LOG_ERROR("Could not save the previous scene: " + saveError);
                    }
                }

                sceneProjectPath = newProjectPath;
                selectedSceneObjectId = 0;
                const std::filesystem::path sceneDirectory = sceneProjectPath / "scenes";
                std::error_code error;
                std::filesystem::create_directories(sceneDirectory, error);
                if (error)
                {
                    LOG_ERROR("Could not create project scene directory: " + error.message());
                    sceneManager.CreateScene("Main");
                }
                else
                {
                    const std::filesystem::path mainScenePath = sceneDirectory / "Main.scene";
                    std::filesystem::path defaultScenePath = project->properties.startupScene.empty()
                        ? mainScenePath
                        : std::filesystem::path(project->properties.startupScene);
                    if (!defaultScenePath.is_absolute())
                        defaultScenePath = sceneProjectPath / defaultScenePath;

                    std::error_code startupSceneError;
                    if (!std::filesystem::exists(defaultScenePath, startupSceneError) && !startupSceneError)
                    {
                        if (defaultScenePath.lexically_normal() != mainScenePath.lexically_normal())
                        {
                            LOG_WARN("Configured startup scene was not found; opening Main.scene instead");
                            project->properties.startupScene =
                                SceneFileUtils::ProjectRelativePath(mainScenePath, sceneProjectPath);
                            project->SaveKepFile();
                        }
                        defaultScenePath = mainScenePath;
                    }
                    else if (startupSceneError)
                    {
                        LOG_ERROR("Could not check the startup scene path: " + startupSceneError.message());
                        defaultScenePath = mainScenePath;
                    }

                    const bool sceneExists = std::filesystem::exists(defaultScenePath, error);
                    if (error)
                    {
                        LOG_ERROR("Could not check the default scene path: " + error.message());
                        sceneManager.CreateScene("Main");
                    }
                    else if (sceneExists)
                    {
                        std::string loadError;
                        if (!sceneManager.LoadScene(defaultScenePath, loadError))
                        {
                            LOG_ERROR("Could not load the project's default scene: " + loadError);
                            sceneManager.CreateScene("Main");
                        }
                    }
                    else
                    {
                        const std::filesystem::path legacyScenePath = sceneDirectory / "Main.scene.json";
                        const bool legacySceneExists = std::filesystem::exists(legacyScenePath, error);
                        if (error)
                        {
                            LOG_ERROR("Could not check the legacy default scene path: " + error.message());
                            sceneManager.CreateScene("Main");
                        }
                        else if (legacySceneExists)
                        {
                            std::filesystem::path scenePath = legacyScenePath;
                            std::string migrationError;
                            if (!SceneFileUtils::MigrateLegacySceneFile(legacyScenePath, scenePath, migrationError))
                                LOG_WARN("Could not migrate the legacy default scene: " + migrationError);

                            std::string loadError;
                            if (!sceneManager.LoadScene(scenePath, loadError))
                            {
                                LOG_ERROR("Could not load the project's default scene: " + loadError);
                                sceneManager.CreateScene("Main");
                            }
                        }
                        else
                        {
                            sceneManager.CreateScene("Main");
                            std::string saveError;
                            if (!sceneManager.SaveScene(defaultScenePath, saveError))
                                LOG_ERROR("Could not create the project's default scene: " + saveError);
                        }
                    }
                }
            }
        }
        else
        {
            KE::Renderer::GetClearColor() = KE::Color(0.20f, 0.20f, 0.20f);
            if (!sceneProjectPath.empty())
            {
                const KE::Scene* currentScene = GetSceneManager().GetActiveScene();
                if (currentScene && currentScene->IsDirty() && !GetSceneManager().GetActiveScenePath().empty())
                {
                    std::string saveError;
                    if (!GetSceneManager().SaveActiveScene(saveError))
                        LOG_ERROR("Could not save the unloaded project scene: " + saveError);
                }
                GetSceneManager().CreateScene("Untitled");
                sceneProjectPath.clear();
                selectedSceneObjectId = 0;
            }
        }

        if (!ProjectManager::Get().GetLoadedProject())
        {
            GetWindow().SetTitle("Kingdom Engine - No Project Loaded");
        }

        static float progress = 0.0f;
        if (ProjectBuild::buildStarted && progress <= 0.90f)
        {
            ProjectBuild::buildProgress = progress;
            progress += 0.001f;
        }

        if (ProjectBuild::compileDone)
        {
            LOG_WARN("Project build finished");

            ProjectBuild::buildDone = true;
            ProjectBuild::buildStarted = false;
            ProjectBuild::compileDone = false;
            ProjectBuild::buildProgress = 0.0f;
            progress = 0.0f;

            SET_IM_WINDOW_VISIBLE("BuildProgressBar", false);
        }
    }

    void EditorApplication::OnEvent(KE::EventType evType)
    {
        if (evType == KE::EventType::WindowClose)
        {
            if (ProjectManager::Get().GetLoadedProject())
            {
                SaveProjectDialog::SetShow();
                closeAfterSave = true;

            }
            else
            {
                Quit();
            }
        }
    }

    void EditorApplication::OnImGuiRender()
    {
        UPDATE_ALL_IM_WINDOW();

        //process save project dialog box
        KE::DialogResult saveProjectResult = SaveProjectDialog::Show();
        if (saveProjectResult == KE::DialogResult::Save)
        {
            if (closeAfterSave)
                Quit();
        }
        else if (saveProjectResult == KE::DialogResult::DontSave)
        {
            if (closeAfterSave)
                Quit();
        }

        ExcludeProjectDialog::Show();
        BuildErrorDialog::Show();
    }

    void EditorApplication::OnMenuBarRender()
    {
        UPDATE_ALL_IM_MENU();
    }

    void EditorApplication::OnDockspaceUpdate()
    {
        UPDATE_ALL_IM_WINDOW_DOCKSPACE(KE::ImGuiManager::Get());
    }
}

KE::Application *KE::CreateApplication()
{
    return new Editor::EditorApplication();
}
