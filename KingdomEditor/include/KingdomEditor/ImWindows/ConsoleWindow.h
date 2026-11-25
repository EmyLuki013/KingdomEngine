#ifndef CONSOLE_WINDOW_H
#define CONSOLE_WINDOW_H

#include <KingdomEngine/ImGui/ImWindow.h>
#include <KingdomEngine/Core/Log.h>

#include "KingdomEditor/Project/ProjectManager.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Editor
{
	class ConsoleWindow : public KE::ImWindow
	{
	public:
		ConsoleWindow()
			: KE::ImWindow("Console", 0, 599, 1366, 98, true, false, KE::DockSide::DOWN),
			  shellStarted(false),
			  shellStartupAttempted(false)
		{
			commandInput[0] = '\0';
#ifdef _WIN32
			ZeroMemory(&processInfo, sizeof(processInfo));
			inputWrite = nullptr;
			outputRead = nullptr;
			readerThread = nullptr;
#endif
		}

		~ConsoleWindow() override
		{
#ifdef _WIN32
			StopShell();
#endif
		}

		void OnReady() override
		{
		}

		void OnRender() override
		{
#ifdef _WIN32
			if (!shellStarted && !shellStartupAttempted)
			{
				shellStartupAttempted = true;
				if (StartShell())
					RenderShell();
				else
					ImGui::TextUnformatted("Could not start the system command shell. See Logs for details.");
				return;
			}
			if (!shellStarted)
			{
				ImGui::TextUnformatted("Could not start the system command shell. See Logs for details.");
				if (ImGui::Button("Retry"))
					shellStartupAttempted = false;
				return;
			}

			RenderShell();
#else
			ImGui::TextUnformatted("Embedded system console is only available on Windows.");
#endif
		}

	private:
#ifdef _WIN32
		void RenderShell()
		{
			if (processInfo.hProcess && WaitForSingleObject(processInfo.hProcess, 0) == WAIT_OBJECT_0)
			{
				ImGui::TextUnformatted("System command shell has exited.");
				if (ImGui::Button("Restart Shell"))
				{
					StopShell();
					shellStarted = false;
					shellStartupAttempted = true;
					StartShell();
				}
				RenderOutput();
				return;
			}

			RenderOutput();
			if (ImGui::InputText("##ConsoleCommand", commandInput, sizeof(commandInput), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				SendCommand(commandInput);
				commandInput[0] = '\0';
			}
			ImGui::SameLine();
			if (ImGui::Button("Run"))
			{
				SendCommand(commandInput);
				commandInput[0] = '\0';
			}
		}

		static DWORD WINAPI ReadOutput(void* parameter)
		{
			auto* console = static_cast<ConsoleWindow*>(parameter);
			char buffer[4096];
			DWORD bytesRead = 0;
			while (ReadFile(console->outputRead, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
				console->AppendOutput(ConsoleBytesToUtf8(buffer, bytesRead));
			return 0;
		}

		static std::string ConsoleBytesToUtf8(const char* bytes, DWORD length)
		{
			const int wideLength = MultiByteToWideChar(CP_OEMCP, 0, bytes, static_cast<int>(length), nullptr, 0);
			if (wideLength <= 0)
				return std::string(bytes, length);

			std::vector<wchar_t> wideText(wideLength);
			MultiByteToWideChar(CP_OEMCP, 0, bytes, static_cast<int>(length), wideText.data(), wideLength);
			const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideText.data(), wideLength, nullptr, 0, nullptr, nullptr);
			if (utf8Length <= 0)
				return std::string(bytes, length);

			std::string utf8Text(utf8Length, '\0');
			WideCharToMultiByte(CP_UTF8, 0, wideText.data(), wideLength, &utf8Text[0], utf8Length, nullptr, nullptr);
			return utf8Text;
		}

		static std::string Utf8ToConsoleBytes(const char* text)
		{
			const int wideLength = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
			if (wideLength <= 0)
				return std::string(text);

			std::vector<wchar_t> wideText(wideLength);
			MultiByteToWideChar(CP_UTF8, 0, text, -1, wideText.data(), wideLength);
			const int byteLength = WideCharToMultiByte(CP_OEMCP, 0, wideText.data(), -1, nullptr, 0, nullptr, nullptr);
			if (byteLength <= 0)
				return std::string(text);

			std::string consoleText(byteLength, '\0');
			WideCharToMultiByte(CP_OEMCP, 0, wideText.data(), -1, &consoleText[0], byteLength, nullptr, nullptr);
			consoleText.resize(byteLength - 1);
			return consoleText;
		}

		bool StartShell()
		{
			ZeroMemory(&processInfo, sizeof(processInfo));
			SECURITY_ATTRIBUTES securityAttributes = {};
			securityAttributes.nLength = sizeof(securityAttributes);
			securityAttributes.bInheritHandle = TRUE;

			HANDLE childInput = nullptr;
			HANDLE childOutput = nullptr;
			if (!CreatePipe(&childInput, &inputWrite, &securityAttributes, 0) ||
				!CreatePipe(&outputRead, &childOutput, &securityAttributes, 0))
			{
				LOG_ERROR("Could not create console pipes. Windows error: " + std::to_string(GetLastError()));
				CloseHandleIfValid(childInput);
				CloseHandleIfValid(inputWrite);
				CloseHandleIfValid(outputRead);
				CloseHandleIfValid(childOutput);
				inputWrite = nullptr;
				outputRead = nullptr;
				return false;
			}

			SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
			SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

			STARTUPINFOW startupInfo = {};
			startupInfo.cb = sizeof(startupInfo);
			startupInfo.dwFlags = STARTF_USESTDHANDLES;
			startupInfo.hStdInput = childInput;
			startupInfo.hStdOutput = childOutput;
			startupInfo.hStdError = childOutput;

			wchar_t shellPath[MAX_PATH] = {};
			const DWORD shellPathLength = GetEnvironmentVariableW(L"COMSPEC", shellPath, MAX_PATH);
			if (shellPathLength == 0 || shellPathLength >= MAX_PATH)
			{
				LOG_ERROR("Could not locate the Windows command shell.");
				CloseHandleIfValid(childInput);
				CloseHandleIfValid(childOutput);
				CloseHandleIfValid(inputWrite);
				CloseHandleIfValid(outputRead);
				inputWrite = nullptr;
				outputRead = nullptr;
				return false;
			}

			std::wstring commandLine = L"\"" + std::wstring(shellPath) + L"\" /Q /D";
			std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
			mutableCommandLine.push_back(L'\0');

			const auto* project = ProjectManager::Get().GetLoadedProject();
			const std::wstring workingDirectory = project
				? std::filesystem::path(project->properties.path).wstring()
				: std::wstring();

			const BOOL created = CreateProcessW(
				shellPath,
				mutableCommandLine.data(),
				nullptr,
				nullptr,
				TRUE,
				CREATE_NO_WINDOW,
				nullptr,
				workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
				&startupInfo,
				&processInfo);

			CloseHandleIfValid(childInput);
			CloseHandleIfValid(childOutput);
			if (!created)
			{
				LOG_ERROR("Could not start the Windows command shell. Windows error: " + std::to_string(GetLastError()));
				CloseHandleIfValid(inputWrite);
				CloseHandleIfValid(outputRead);
				inputWrite = nullptr;
				outputRead = nullptr;
				return false;
			}

			CloseHandle(processInfo.hThread);
			processInfo.hThread = nullptr;
			readerThread = CreateThread(nullptr, 0, ReadOutput, this, 0, nullptr);
			if (!readerThread)
			{
				LOG_ERROR("Could not start the console output reader. Windows error: " + std::to_string(GetLastError()));
				TerminateProcess(processInfo.hProcess, 1);
				StopShell();
				return false;
			}

			shellStarted = true;
			AppendOutput("Windows command shell started.\n");
			return true;
		}

		void SendCommand(const char* command)
		{
			if (!command || command[0] == '\0' || !inputWrite)
				return;

			AppendOutput(std::string("> ") + command + "\n");
			std::string bytes = Utf8ToConsoleBytes(command);
			bytes.append("\r\n");
			DWORD bytesWritten = 0;
			if (!WriteFile(inputWrite, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesWritten, nullptr))
				LOG_ERROR("Could not write command to the system console. Windows error: " + std::to_string(GetLastError()));
			else if (bytesWritten != bytes.size())
				LOG_ERROR("The system console accepted only part of the command.");
		}

		void StopShell()
		{
			if (inputWrite)
			{
				const char exitCommand[] = "exit\r\n";
				DWORD bytesWritten = 0;
				WriteFile(inputWrite, exitCommand, sizeof(exitCommand) - 1, &bytesWritten, nullptr);
				CloseHandle(inputWrite);
				inputWrite = nullptr;
			}

			if (processInfo.hProcess)
			{
				if (WaitForSingleObject(processInfo.hProcess, 2000) == WAIT_TIMEOUT)
				{
					TerminateProcess(processInfo.hProcess, 0);
					WaitForSingleObject(processInfo.hProcess, INFINITE);
				}
				CloseHandle(processInfo.hProcess);
				processInfo.hProcess = nullptr;
			}

			if (readerThread)
			{
				WaitForSingleObject(readerThread, INFINITE);
				CloseHandle(readerThread);
				readerThread = nullptr;
			}

			CloseHandleIfValid(outputRead);
			outputRead = nullptr;
			shellStarted = false;
		}

		static void CloseHandleIfValid(HANDLE handle)
		{
			if (handle)
				CloseHandle(handle);
		}
#endif

		void AppendOutput(const std::string& text)
		{
			std::lock_guard<std::mutex> lock(outputMutex);
			output.append(text);
			const size_t maxOutputSize = 250000;
			if (output.size() > maxOutputSize)
				output.erase(0, output.size() - maxOutputSize);
		}

		void RenderOutput()
		{
			std::string outputSnapshot;
			{
				std::lock_guard<std::mutex> lock(outputMutex);
				outputSnapshot = output;
			}

			ImGui::BeginChild("ConsoleOutput", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true,
				ImGuiWindowFlags_HorizontalScrollbar);
			const bool scrollToBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
			ImGui::TextUnformatted(outputSnapshot.c_str());
			if (scrollToBottom)
				ImGui::SetScrollHereY(1.0f);
			ImGui::EndChild();
		}

		std::mutex outputMutex;
		std::string output;
		char commandInput[2048];
		bool shellStarted;
		bool shellStartupAttempted;
#ifdef _WIN32
		PROCESS_INFORMATION processInfo;
		HANDLE inputWrite;
		HANDLE outputRead;
		HANDLE readerThread;
#endif
	};
}

#endif
