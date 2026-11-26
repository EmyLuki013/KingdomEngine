#ifndef FILE_GENERATOR_H
#define FILE_GENERATOR_H

#include <KingdomEngine/Core/Core.h>
#include <KingdomEngine/Core/Log.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace Editor
{
    class FileGenerator
    {
    public:
        static std::string GenerateBuildConfigFile(const std::string& projectName, const std::string& projectPath)
        {
            namespace fs = std::filesystem;

            const fs::path projectDirectory(projectPath);
            const fs::path sourceDirectory = projectDirectory / "src";
            std::error_code error;
            if (!fs::is_directory(sourceDirectory, error) || error)
            {
                LOG_ERROR("Project source directory does not exist: " + sourceDirectory.string());
                return "";
            }

            std::vector<std::string> sourceDirectories = {"src/"};
            fs::recursive_directory_iterator current(sourceDirectory, fs::directory_options::none, error);
            const fs::recursive_directory_iterator end;
            while (!error && current != end)
            {
                if (current->is_directory(error))
                {
                    if (error)
                        break;

                    sourceDirectories.push_back(current->path().lexically_relative(projectDirectory).generic_string() + "/");
                }
                current.increment(error);
            }

            if (error)
            {
                LOG_ERROR("Could not enumerate project source directories: " + error.message());
                return "";
            }

            const fs::path thirdPartyDirectory(KE::Core::THIRDPARTY_DIR);
            std::vector<std::string> includeDirectories = {
                KE::Core::KE_INCLUDE_DIR,
                (thirdPartyDirectory / "spdlog/include").string(),
                (thirdPartyDirectory / "ImGui/include").string(),
                (thirdPartyDirectory / "GLEW/include").string(),
                (thirdPartyDirectory / "GLM/include").string(),
                (thirdPartyDirectory / "SDL2/include").string(),
                (thirdPartyDirectory / "stb/include").string(),
                (thirdPartyDirectory / "rapidjson/include").string(),
                (thirdPartyDirectory / "glad/include").string(),
                (thirdPartyDirectory / "Lua/include").string()
            };
            std::vector<std::string> libraryDirectories = {
                KE::Core::EXE_DIR,
                (thirdPartyDirectory / "SDL2/lib").string(),
                (thirdPartyDirectory / "GLEW/lib").string(),
                (thirdPartyDirectory / "Lua").string()
            };
            for (std::string& directory : includeDirectories)
                directory = MakeProjectRelativePath(directory, projectDirectory);
            for (std::string& directory : libraryDirectories)
                directory = MakeProjectRelativePath(directory, projectDirectory);

            const std::vector<std::string> libraries = {
                "KingdomEngine", "lua55", "DearImGui", "SDL2main", "SDL2", "opengl32", "gdi32",
                "winmm", "imm32", "version", "setupapi", "ole32", "oleaut32", "shell32",
                "dinput8", "dxguid", "user32", "kernel32", "advapi32"
            };
            const std::vector<std::string> defines = {
                "GLM_ENABLE_EXPERIMENTAL", "SDL_MAIN_HANDLED", "WIN32", "_WIN32", "_WINDOWS",
                "_CRT_SECURE_NO_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS",
                "_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING", "GAME_APPLICATION"
            };

            std::ostringstream config;
            config << "{\n"
                   << "    \"type\": \"executable\",\n"
                   << "    \"buildType\": \"distribution\",\n"
                   << "    \"architecture\": \"x64\",\n"
                   << "    \"subsystem\": \"console\",\n"
                   << "    \"outputName\": \"" << EscapeJson(projectName) << "\",\n"
                   << "    \"outputDir\": \"build/\",\n"
                   << "    \"cStandard\": \"c++17\",\n"
                   << "    \"compiler\": \"g++\",\n";
            AppendJsonArray(config, "filesDir", sourceDirectories);
            AppendJsonArray(config, "includesDir", includeDirectories);
            AppendJsonArray(config, "libsDir", libraryDirectories);
            AppendJsonArray(config, "libs", libraries);
            AppendJsonArray(config, "defines", defines);

            const std::string executableName = EscapeJson(projectName);
            config << "    \"postBuildCommands\": [\n"
                   << "        \"copy /Y \\\"build\\\\distribution\\\\x64\\\\" << executableName
                   << ".exe\\\" \\\"build\\\\" << executableName << ".exe\\\"\"\n"
                   << "    ]\n"
                   << "}\n";
            return config.str();
        }

    private:
        static std::string MakeProjectRelativePath(const std::string& path, const std::filesystem::path& projectDirectory)
        {
            const std::filesystem::path normalizedProject = projectDirectory.lexically_normal();
            const std::filesystem::path normalizedPath = std::filesystem::path(path).lexically_normal();
            const std::filesystem::path relativePath = normalizedPath.lexically_relative(normalizedProject);
            return (relativePath.empty() ? normalizedPath : relativePath).generic_string();
        }

        static void AppendJsonArray(std::ostringstream& output, const std::string& name, const std::vector<std::string>& values)
        {
            output << "    \"" << name << "\": [";
            if (!values.empty())
                output << "\n";

            for (size_t i = 0; i < values.size(); ++i)
            {
                output << "        \"" << EscapeJson(std::filesystem::path(values[i]).generic_string()) << "\"";
                if (i + 1 < values.size())
                    output << ",";
                output << "\n";
            }

            output << "    ],\n";
        }

        static std::string EscapeJson(const std::string& value)
        {
            std::string escaped;
            for (char character : value)
            {
                switch (character)
                {
                    case '\\': escaped += "\\\\"; break;
                    case '"': escaped += "\\\""; break;
                    case '\b': escaped += "\\b"; break;
                    case '\f': escaped += "\\f"; break;
                    case '\n': escaped += "\\n"; break;
                    case '\r': escaped += "\\r"; break;
                    case '\t': escaped += "\\t"; break;
                    default: escaped += character; break;
                }
            }

            return escaped;
        }

    };
}

#endif
