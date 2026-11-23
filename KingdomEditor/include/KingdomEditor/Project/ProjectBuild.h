#ifndef BUILD_PROGRESS_H
#define BUILD_PROGRESS_H

#include <KingdomEngine/ImGui/ImWindowManager.h>

#include "KingdomEditor/Project/ProjectManager.h"

#include <thread>
#include <atomic>
#include <string>

namespace Editor
{
    class ProjectBuild
    {
    public:
        static void Build();
        static void Compile(const std::string& projectPath, std::atomic<bool>& compileDone);

        static float buildProgress;
        static bool buildDone;
        static bool buildStarted;
        static std::atomic<bool> compileDone;

    };
}

#endif
