#include "KingdomEditor/Project/ProjectBuild.h"
#include "KingdomEditor/DialogBox/BuildErrorDialogBox.h"
#include "KingdomEditor/Utils/Globals.h"
#include "KingdomEditor/Utils/FileGenerator.h"

namespace Editor
{
    float ProjectBuild::buildProgress = 0.0f;
    bool ProjectBuild::buildDone = false;
    bool ProjectBuild::buildStarted = false;
    std::atomic<bool> ProjectBuild::compileDone = false;

    void ProjectBuild::Build()
    {
        if (ProjectManager::Get().GetLoadedProject())
        {
            LOG_WARN("Starting project build");
            buildStarted = true;
            buildDone = false;
            compileDone = false;
            SET_IM_WINDOW_VISIBLE("BuildProgressBar", true);

            const auto* project = ProjectManager::Get().GetLoadedProject();
            const std::string projectPath = project->properties.path;
            const std::string buildConfig = FileGenerator::GenerateBuildConfigFile(project->properties.name, project->properties.path);
            if (buildConfig.empty())
            {
                buildStarted = false;
                buildDone = true;
                SET_IM_WINDOW_VISIBLE("BuildProgressBar", false);
                BuildErrorDialog::SetShow();
                return;
            }

            KE::File buildConfigFile;
            buildConfigFile.Open(projectPath + "\\build.json", KE::ModeFlags::WRITE);
            if (!buildConfigFile.IsOpen())
            {
                buildStarted = false;
                buildDone = true;
                SET_IM_WINDOW_VISIBLE("BuildProgressBar", false);
                BuildErrorDialog::SetShow();
                return;
            }
            buildConfigFile.Write(buildConfig);
            buildConfigFile.GetFile().flush();
            const bool configWritten = buildConfigFile.GetFile().good();
            buildConfigFile.Close();
            if (!configWritten)
            {
                LOG_ERROR("Could not write King Builder configuration");
                buildStarted = false;
                buildDone = true;
                SET_IM_WINDOW_VISIBLE("BuildProgressBar", false);
                BuildErrorDialog::SetShow();
                return;
            }

            std::thread compileThread(Compile, projectPath, std::ref(compileDone));
            compileThread.detach();
        }
    }

    void ProjectBuild::Compile(const std::string& projectPath, std::atomic<bool> &compileDone)
    {
        const std::string command = "cd /d \"" + projectPath + "\" && \"" +
            KE::Core::WINDOWS_TOOLS_DIR + "KingBuilder.exe\"";

        int result = std::system(command.c_str());
        if (result != 0)
        {
            LOG_ERROR("King Builder failed to build the project");
            BuildErrorDialog::SetShow();
        }

        const std::string buildConfigPath = projectPath + "\\build.json";
        if (KE::File::Exists(buildConfigPath))
        {
            KE::File buildConfigFile;
            buildConfigFile.Open(buildConfigPath, KE::ModeFlags::READ);
            buildConfigFile.Remove();
        }

        compileDone = true;
    }
}
