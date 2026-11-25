#include "KingdomEditor/Project/ProjectManager.h"
#include "KingdomEditor/Utils/Globals.h"
#include "KingdomEditor/Utils/FileGenerator.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <rapidjson/document.h>

namespace Editor
{
    ProjectManager &ProjectManager::Get()
    {
        static ProjectManager *instance = new ProjectManager();
        return *instance;
    }

    void ProjectManager::SaveProjectsFile()
    {
        WriteProjectFile();
    }

    void ProjectManager::OpenProjectsFile()
    {
        if (!projectsFile.Exists(Globals::PROJECTS_FILE))
        {
            WriteProjectFile();
        }
        else
        {
            projectsFile.Open(Globals::PROJECTS_FILE, KE::ModeFlags::READ);
            std::string fileContent = projectsFile.Read();
            projectsFile.Close();

            if (fileContent.empty())
            {
                WriteProjectFile();
            }
        }

        projectsFile.Open(Globals::PROJECTS_FILE, KE::ModeFlags::READ);
        projectsJson.Parse(projectsFile.Read(), true);
        projectsFile.Close();

        for (auto it = projectsJson.GetData().MemberBegin(); it != projectsJson.GetData().MemberEnd(); ++it)
        {
            if (it->value.IsObject())
            {
                auto &obj = it->value;

                std::string engineCoreVersion = "";
                std::string kepFile = "";
                std::string path = "";

                if (obj.HasMember("path") && obj["path"].IsString() &&
                    obj.HasMember("kep_file") && obj["kep_file"].IsString() &&
                    obj.HasMember("engine_core_version") && obj["engine_core_version"].IsString())
                {
                    engineCoreVersion = obj["engine_core_version"].GetString();
                    kepFile = obj["kep_file"].GetString();
                    path = obj["path"].GetString();

                    Project project(it->name.GetString(), path, kepFile);
                    project.properties.engineCoreVersion = engineCoreVersion;
                    PushProject(project);
                }
                else
                {
                    std::string errMsg = "Object is missing members: " + std::string(it->name.GetString());
                    LOG_ERROR(errMsg);
                }
            }
            else
            {
                std::string errMsg = "it is not a json object: " + std::string(it->name.GetString());
                LOG_ERROR(errMsg);
            }
        }
    }

    void ProjectManager::AddProject(Project &project)
    {
        //create project dictionary
        KE::Dictionary projectDict;
        projectDict.Add("kep_file", project.properties.kepFile);
        projectDict.Add("engine_core_version", project.properties.engineCoreVersion);
        projectDict.Add("path", project.properties.path);
        projectsJson.AddDicionary(project.properties.name, projectDict);

        //create project directory
        KE::Directory projDir;
        if (!projDir.DirExists(project.properties.path))
        {
            projDir.Create(project.properties.path);
            LOG_INFO("Project directory created");

            //create project kep file
            project.CreateKepFile();

            //create project sub-directories
            KE::Directory subDir;
            subDir.Create(project.properties.path + "\\src");
            subDir.Create(project.properties.path + "\\build");

            //create project main cpp file
            KE::File cppFile;
            std::string projectName = project.properties.name;
            projectName.erase(
                std::remove(projectName.begin(), projectName.end(), ' '),
                projectName.end()
            );
            std::string cppFileName = project.properties.path + "\\src\\" + projectName + ".cpp";
            cppFile.Open(cppFileName, KE::ModeFlags::WRITE);
            cppFile.Write(FileGenerator::GenerateProjectCppFile(project.properties.name));
            cppFile.Close();
        }
        else
        {
            LOG_ERROR("Project directory already exists");
        }

        PushProject(project);
    }

    bool ProjectManager::ImportProject(const std::string& kepFilePath)
    {
        namespace fs = std::filesystem;
        const fs::path importedFile = fs::path(kepFilePath).lexically_normal();
        std::string extension = importedFile.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        if (extension != ".kep")
        {
            LOG_ERROR("Selected file is not a Kingdom Engine project (.kep): " + kepFilePath);
            return false;
        }

        std::error_code error;
        if (!fs::is_regular_file(importedFile, error))
        {
            LOG_ERROR("Could not read project file: " + importedFile.string() +
                (error ? " (" + error.message() + ")" : ""));
            return false;
        }

        std::ifstream file(importedFile, std::ios::binary);
        if (!file)
        {
            LOG_ERROR("Could not open project file: " + importedFile.string());
            return false;
        }

        const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (contents.empty() || file.bad())
        {
            LOG_ERROR("Project file is empty or could not be read: " + importedFile.string());
            return false;
        }

        rapidjson::Document projectJson;
        projectJson.Parse(contents.c_str());
        if (projectJson.HasParseError() || !projectJson.IsObject() ||
            !projectJson.HasMember("project") || !projectJson["project"].IsObject() ||
            !projectJson["project"].HasMember("name") || !projectJson["project"]["name"].IsString())
        {
            LOG_ERROR("Project file does not contain valid project metadata: " + importedFile.string());
            return false;
        }

        const std::string projectName = projectJson["project"]["name"].GetString();
        if (projectName.empty())
        {
            LOG_ERROR("Project file has an empty project name: " + importedFile.string());
            return false;
        }

        for (const Project* existingProject : projects)
        {
            if (existingProject->properties.name != projectName)
                continue;

            const fs::path existingFile = fs::path(existingProject->properties.path) /
                existingProject->properties.kepFile;
            error.clear();
            if (fs::equivalent(existingFile, importedFile, error) && !error)
            {
                LOG_INFO("Project is already in the project list: " + projectName);
                return true;
            }

            LOG_ERROR("A different project with this name is already registered: " + projectName);
            return false;
        }

        const std::string projectDirectory = importedFile.parent_path().string();
        const std::string projectFileName = importedFile.filename().string();
        Project importedProject(projectName, projectDirectory, projectFileName);

        KE::Dictionary projectDictionary;
        projectDictionary.Add("kep_file", projectFileName);
        projectDictionary.Add("engine_core_version", importedProject.properties.engineCoreVersion);
        projectDictionary.Add("path", projectDirectory);
        projectsJson.AddDicionary(projectName, projectDictionary);
        PushProject(importedProject);
        SaveProjectsFile();

        LOG_INFO("Imported project: " + projectName + " from " + importedFile.string());
        return true;
    }

    void ProjectManager::LoadProject(const std::string &projectName)
    {
        //unload project if other project is loaded
        if (loadedProject)
        {
            UnloadProject();
        }

        //search project and load it
        auto it = projectsMap.find(projectName);
        if (it != projectsMap.end())
        {
            Globals::PROJECT_CHANGED = true;
            loadedProject = it->second;
            loadedProject->LoadKepFile();
        }
        else
        {
            LOG_ERROR("Project not found");
        }

        //open project dir
        KE::OS::SetCurrentDir(loadedProject->properties.path);
    }

    void ProjectManager::UnloadProject()
    {
        loadedProject = nullptr;
        KE::OS::SetCurrentDir(KE::OS::GetCurrentDir() + "\\..\\");
    }

    void ProjectManager::RunProject()
    {
        std::thread runProjectThread(&ProjectManager::RunProjectThread, this);
        runProjectThread.detach();
    }

    void ProjectManager::RemoveProject(const std::string &projectName)
    {
        if (loadedProject && loadedProject->properties.name == projectName)
        {
            UnloadProject();
        }

        auto it = projectsMap.find(projectName);
        if (it != projectsMap.end())
        {
            Project *projectToRemove = it->second;
            projectsMap.erase(it);

            auto projIt = std::find(projects.begin(), projects.end(), projectToRemove);
            if (projIt != projects.end())
            {
                projects.erase(projIt);
            }

            if (projectsJson.GetData().HasMember(projectName.c_str()))
            {
                projectsJson.GetData().RemoveMember(projectName.c_str());
            }

            delete projectToRemove;

            WriteProjectFile();
        }
        else
        {
            LOG_ERROR("Project not found");
        }
    }

    void ProjectManager::UpdateLoadedProjectJsonData()
    {
        KE::Dictionary projectDict;
        projectDict.Add("kep_file", loadedProject->properties.kepFile);
        projectDict.Add("engine_core_version", loadedProject->properties.engineCoreVersion);
        projectDict.Add("path", loadedProject->properties.path);
        projectsJson.AddDicionary(loadedProject->properties.name, projectDict);
        ProjectManager::Get().SaveProjectsFile();
    }

    void ProjectManager::WriteProjectFile()
    {
        projectsFile.Open(Globals::PROJECTS_FILE, KE::ModeFlags::WRITE);
        projectsFile.Write(projectsJson.Stringify());
        projectsFile.Close();
    }

    void ProjectManager::PushProject(Project &project)
    {
        Project *proj = new Project(project.properties.name, project.properties.path, project.properties.kepFile);
        proj->properties = project.properties;
        projects.push_back(proj);
        projectsMap[project.properties.name] = proj;
    }

    void ProjectManager::RunProjectThread()
    {
        std::string projectName = ProjectManager::Get().GetLoadedProject()->properties.name;
        std::string projectBuildPath = ProjectManager::Get().GetLoadedProject()->properties.path + "\\build\\";

        KE::OS::SetCurrentDir(projectBuildPath);

        std::string projectExePath = "call \"" + projectBuildPath + projectName + ".exe\" \"../" + projectName + ".kep\"";
		LOG_INFO(projectExePath);
        int result = std::system(projectExePath.c_str());
        if (result != 0)
        {
            LOG_ERROR("Error to run project");
            KE::OS::SetCurrentDir(KE::OS::GetCurrentDir() + "\\..");
            return;
        }

        KE::OS::SetCurrentDir(KE::OS::GetCurrentDir() + "\\..");
    }
}
