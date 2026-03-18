#include <windows.h>
#include <shlobj_core.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cwctype>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")

namespace
{
    constexpr const wchar_t* kCompanyFolderName = L"Win11Theme";
    constexpr const wchar_t* kLogFileName       = L"ThemeRefresh.log";
    constexpr const wchar_t* kConfigFileName    = L"theme.ini";

    constexpr const wchar_t* kMarkerKey =
        L"Software\\Win11Theme";
    constexpr const wchar_t* kMarkerValue =
        L"Completed";

    constexpr const wchar_t* kMutexName =
        L"Local\\Win11Theme_ApplyOnce";

    constexpr const wchar_t* kExplorerAccentKey =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent";
    constexpr const wchar_t* kThemesPersonalizeKey =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    constexpr const wchar_t* kDwmKey =
        L"Software\\Microsoft\\Windows\\DWM";

    std::wofstream g_log;
    HANDLE g_mutex = nullptr;

    struct AppOptions
    {
        bool force = false;
        std::wstring iniPath;
    };

    struct RegValueSpec
    {
        const wchar_t* section;
        const wchar_t* name;
        const wchar_t* regPath;
        DWORD type;
    };

    const RegValueSpec kRegSpecs[] =
    {
        { L"ExplorerAccent",    L"AccentColorMenu",            kExplorerAccentKey,    REG_DWORD  },
        { L"ExplorerAccent",    L"AccentPalette",              kExplorerAccentKey,    REG_BINARY },
        { L"ExplorerAccent",    L"StartColorMenu",             kExplorerAccentKey,    REG_DWORD  },
        { L"ExplorerAccent",    L"UseNewAutoColorAccent",      kExplorerAccentKey,    REG_DWORD  },

        { L"ThemesPersonalize", L"AppsUseLightTheme",          kThemesPersonalizeKey, REG_DWORD  },
        { L"ThemesPersonalize", L"ColorPrevalence",            kThemesPersonalizeKey, REG_DWORD  },
        { L"ThemesPersonalize", L"EnableTransparency",         kThemesPersonalizeKey, REG_DWORD  },
        { L"ThemesPersonalize", L"SystemUsesLightTheme",       kThemesPersonalizeKey, REG_DWORD  },

        { L"DWM",               L"AccentColor",                kDwmKey,               REG_DWORD  },
        { L"DWM",               L"ColorizationAfterglow",      kDwmKey,               REG_DWORD  },
        { L"DWM",               L"ColorPrevalence",            kDwmKey,               REG_DWORD  },
        { L"DWM",               L"Composition",                kDwmKey,               REG_DWORD  },
        { L"DWM",               L"EnableWindowColorization",   kDwmKey,               REG_DWORD  },
        { L"DWM",               L"ColorizationColor",          kDwmKey,               REG_DWORD  },
        { L"DWM",               L"ColorizationColorBalance",   kDwmKey,               REG_DWORD  }
    };

    std::wstring GetTimestamp()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);

        std::wstringstream ss;
        ss << std::setfill(L'0')
           << st.wYear << L"-"
           << std::setw(2) << st.wMonth << L"-"
           << std::setw(2) << st.wDay << L" "
           << std::setw(2) << st.wHour << L":"
           << std::setw(2) << st.wMinute << L":"
           << std::setw(2) << st.wSecond;
        return ss.str();
    }

    std::wstring Trim(const std::wstring& input)
    {
        const wchar_t* whitespace = L" \t\r\n";
        size_t start = input.find_first_not_of(whitespace);
        if (start == std::wstring::npos)
            return L"";

        size_t end = input.find_last_not_of(whitespace);
        return input.substr(start, end - start + 1);
    }

    std::wstring ToLower(const std::wstring& s)
    {
        std::wstring result = s;
        for (auto& ch : result)
            ch = static_cast<wchar_t>(towlower(ch));
        return result;
    }

    std::wstring FormatErrorMessage(DWORD error)
    {
        if (error == 0)
            return L"No error";

        LPWSTR buffer = nullptr;
        DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS;

        DWORD len = FormatMessageW(
            flags,
            nullptr,
            error,
            0,
            reinterpret_cast<LPWSTR>(&buffer),
            0,
            nullptr);

        std::wstring result = (len && buffer) ? std::wstring(buffer, len) : L"Unknown error";

        if (buffer)
            LocalFree(buffer);

        while (!result.empty() &&
               (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' '))
        {
            result.pop_back();
        }

        return result;
    }

    std::wstring GetLastErrorString()
    {
        return FormatErrorMessage(GetLastError());
    }

    void Log(const std::wstring& msg)
    {
        if (g_log.is_open())
        {
            g_log << L"[" << GetTimestamp() << L"] " << msg << std::endl;
            g_log.flush();
        }
    }

    bool GetKnownFolderPathString(REFKNOWNFOLDERID folderId, std::wstring& outPath)
    {
        PWSTR raw = nullptr;
        HRESULT hr = SHGetKnownFolderPath(folderId, 0, nullptr, &raw);
        if (FAILED(hr))
            return false;

        outPath = raw;
        CoTaskMemFree(raw);
        return true;
    }

    bool EnsureDirectoryExists(const std::wstring& folderPath)
    {
        int rc = SHCreateDirectoryExW(nullptr, folderPath.c_str(), nullptr);
        if (rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS)
            return true;

        SetLastError(static_cast<DWORD>(rc));
        return false;
    }

    bool BuildLogPath(std::wstring& outLogPath)
    {
        std::wstring localAppData;
        if (!GetKnownFolderPathString(FOLDERID_LocalAppData, localAppData))
            return false;

        std::wstring folder = localAppData + L"\\" + kCompanyFolderName;
        if (!EnsureDirectoryExists(folder))
            return false;

        outLogPath = folder + L"\\" + kLogFileName;
        return true;
    }

    bool BuildDefaultConfigPath(std::wstring& outConfigPath)
    {
        std::wstring programData;
        if (!GetKnownFolderPathString(FOLDERID_ProgramData, programData))
            return false;

        std::wstring folder = programData + L"\\" + kCompanyFolderName;
        if (!EnsureDirectoryExists(folder))
            return false;

        outConfigPath = folder + L"\\" + kConfigFileName;
        return true;
    }

    bool InitLogging()
    {
        std::wstring logPath;
        if (!BuildLogPath(logPath))
            return false;

        g_log.open(logPath, std::ios::app);
        if (!g_log.is_open())
            return false;

        Log(L"==== Application Start ====");
        Log(L"Log path: " + logPath);
        return true;
    }

    bool AcquireSingleInstanceMutex()
    {
        g_mutex = CreateMutexW(nullptr, FALSE, kMutexName);
        if (g_mutex == nullptr)
        {
            Log(L"CreateMutexW failed: " + GetLastErrorString());
            return false;
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            Log(L"Another instance is already running. Exiting.");
            return false;
        }

        Log(L"Single instance mutex acquired.");
        return true;
    }

    void Cleanup()
    {
        if (g_mutex)
        {
            CloseHandle(g_mutex);
            g_mutex = nullptr;
        }

        if (g_log.is_open())
        {
            Log(L"==== Application End ====");
            g_log.close();
        }
    }

    bool ReadDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD& value)
    {
        HKEY hKey = nullptr;
        LONG rc = RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE, &hKey);
        if (rc != ERROR_SUCCESS)
            return false;

        DWORD type = 0;
        DWORD size = sizeof(value);
        rc = RegQueryValueExW(
            hKey,
            valueName,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(&value),
            &size);

        RegCloseKey(hKey);
        return (rc == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(DWORD));
    }

    bool WriteDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD value)
    {
        HKEY hKey = nullptr;
        LONG rc = RegCreateKeyExW(
            root,
            subKey,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &hKey,
            nullptr);

        if (rc != ERROR_SUCCESS)
        {
            SetLastError(static_cast<DWORD>(rc));
            Log(L"RegCreateKeyExW failed for [" + std::wstring(subKey) + L"]: " + GetLastErrorString());
            return false;
        }

        rc = RegSetValueExW(
            hKey,
            valueName,
            0,
            REG_DWORD,
            reinterpret_cast<const BYTE*>(&value),
            sizeof(value));

        RegCloseKey(hKey);

        if (rc != ERROR_SUCCESS)
        {
            SetLastError(static_cast<DWORD>(rc));
            Log(L"RegSetValueExW failed for [" + std::wstring(valueName) + L"]: " + GetLastErrorString());
            return false;
        }

        return true;
    }

    bool WriteBinary(HKEY root, const wchar_t* subKey, const wchar_t* valueName, const std::vector<BYTE>& data)
    {
        HKEY hKey = nullptr;
        LONG rc = RegCreateKeyExW(
            root,
            subKey,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &hKey,
            nullptr);

        if (rc != ERROR_SUCCESS)
        {
            SetLastError(static_cast<DWORD>(rc));
            Log(L"RegCreateKeyExW failed for [" + std::wstring(subKey) + L"]: " + GetLastErrorString());
            return false;
        }

        rc = RegSetValueExW(
            hKey,
            valueName,
            0,
            REG_BINARY,
            data.empty() ? nullptr : data.data(),
            static_cast<DWORD>(data.size()));

        RegCloseKey(hKey);

        if (rc != ERROR_SUCCESS)
        {
            SetLastError(static_cast<DWORD>(rc));
            Log(L"RegSetValueExW(REG_BINARY) failed for [" + std::wstring(valueName) + L"]: " + GetLastErrorString());
            return false;
        }

        return true;
    }

    bool MarkerExists()
    {
        DWORD value = 0;
        bool exists = ReadDword(HKEY_CURRENT_USER, kMarkerKey, kMarkerValue, value) && value == 1;

        Log(exists ? L"Completion marker already present. Exiting."
                   : L"Completion marker not present. Continuing.");

        return exists;
    }

    bool WriteMarker()
    {
        Log(L"Writing completion marker.");
        bool ok = WriteDword(HKEY_CURRENT_USER, kMarkerKey, kMarkerValue, 1);
        Log(ok ? L"Completion marker written successfully."
               : L"Failed to write completion marker.");
        return ok;
    }

    bool ReadIniValue(const std::wstring& iniPath,
                      const wchar_t* section,
                      const wchar_t* key,
                      std::wstring& outValue)
    {
        wchar_t buffer[4096]{};
        DWORD chars = GetPrivateProfileStringW(
            section,
            key,
            L"",
            buffer,
            static_cast<DWORD>(std::size(buffer)),
            iniPath.c_str());

        outValue.assign(buffer, chars);
        outValue = Trim(outValue);
        return !outValue.empty();
    }

    bool TryParseHexDword(const std::wstring& text, DWORD& outValue)
    {
        std::wstring s = Trim(text);
        if (s.empty())
            return false;

        if (s.rfind(L"0x", 0) == 0 || s.rfind(L"0X", 0) == 0)
            s = s.substr(2);

        s = Trim(s);

        if (s.empty() || s.size() > 8)
            return false;

        for (wchar_t ch : s)
        {
            if (!iswxdigit(ch))
                return false;
        }

        wchar_t* end = nullptr;
        unsigned long value = wcstoul(s.c_str(), &end, 16);
        if (end == s.c_str() || *end != L'\0')
            return false;

        outValue = static_cast<DWORD>(value);
        return true;
    }

    bool TryParseHexByteList(const std::wstring& text, std::vector<BYTE>& outBytes)
    {
        outBytes.clear();

        std::wstringstream ss(text);
        std::wstring token;

        while (std::getline(ss, token, L','))
        {
            token = Trim(token);
            if (token.empty())
                return false;

            if (token.rfind(L"0x", 0) == 0 || token.rfind(L"0X", 0) == 0)
                token = token.substr(2);

            token = Trim(token);

            if (token.empty() || token.size() > 2)
                return false;

            for (wchar_t ch : token)
            {
                if (!iswxdigit(ch))
                    return false;
            }

            wchar_t* end = nullptr;
            unsigned long value = wcstoul(token.c_str(), &end, 16);
            if (end == token.c_str() || *end != L'\0' || value > 0xFF)
                return false;

            outBytes.push_back(static_cast<BYTE>(value));
        }

        return !outBytes.empty();
    }

    bool ApplyConfigValue(const std::wstring& configPath, const RegValueSpec& spec)
    {
        std::wstring rawValue;
        if (!ReadIniValue(configPath, spec.section, spec.name, rawValue))
        {
            Log(L"Skipping [" + std::wstring(spec.section) + L"] " + spec.name + L" (blank or missing).");
            return true;
        }

        if (spec.type == REG_DWORD)
        {
            DWORD dw = 0;
            if (!TryParseHexDword(rawValue, dw))
            {
                Log(L"Invalid DWORD for [" + std::wstring(spec.section) + L"] " + spec.name + L": " + rawValue);
                return false;
            }

            if (!WriteDword(HKEY_CURRENT_USER, spec.regPath, spec.name, dw))
                return false;

            Log(L"Set DWORD [" + std::wstring(spec.regPath) + L"]\\" + spec.name + L" = " + rawValue);
            return true;
        }

        if (spec.type == REG_BINARY)
        {
            std::vector<BYTE> bytes;
            if (!TryParseHexByteList(rawValue, bytes))
            {
                Log(L"Invalid binary byte list for [" + std::wstring(spec.section) + L"] " + spec.name + L": " + rawValue);
                return false;
            }

            if (!WriteBinary(HKEY_CURRENT_USER, spec.regPath, spec.name, bytes))
                return false;

            Log(L"Set BINARY [" + std::wstring(spec.regPath) + L"]\\" + spec.name + L" = " + rawValue);
            return true;
        }

        Log(L"Unsupported registry type encountered.");
        return false;
    }

    bool ApplyAllConfigValues(const std::wstring& configPath)
    {
        bool overall = true;

        for (const auto& spec : kRegSpecs)
        {
            if (!ApplyConfigValue(configPath, spec))
                overall = false;
        }

        return overall;
    }

    bool WaitForExplorer(DWORD timeoutMs)
    {
        Log(L"Waiting for Explorer window (Shell_TrayWnd).");

        DWORD start = GetTickCount();
        while ((GetTickCount() - start) < timeoutMs)
        {
            if (FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr)
            {
                Log(L"Explorer detected.");
                return true;
            }

            Sleep(500);
        }

        Log(L"Explorer not detected within timeout.");
        return false;
    }

    bool BroadcastSettingChange(const wchar_t* area)
    {
        DWORD_PTR result = 0;

        BOOL ok = SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            0,
            reinterpret_cast<LPARAM>(area),
            SMTO_ABORTIFHUNG | SMTO_NORMAL,
            5000,
            &result);

        std::wstring areaName = area ? area : L"(null)";

        if (!ok)
        {
            Log(L"SendMessageTimeoutW failed for area [" + areaName + L"]: " + GetLastErrorString());
            return false;
        }

        Log(L"Broadcast succeeded for area [" + areaName + L"].");
        return true;
    }

    bool FileExists(const std::wstring& path)
    {
        DWORD attrs = GetFileAttributesW(path.c_str());
        return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool ParseCommandLine(AppOptions& options)
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
            return false;

        bool ok = true;

        for (int i = 1; i < argc; ++i)
        {
            std::wstring arg = ToLower(argv[i]);

            if (arg == L"-force" || arg == L"/force")
            {
                options.force = true;
            }
            else if (arg == L"-ini" || arg == L"/ini")
            {
                if (i + 1 < argc)
                {
                    options.iniPath = argv[++i];
                }
                else
                {
                    ok = false;
                    break;
                }
            }
            else
            {
                ok = false;
                break;
            }
        }

        LocalFree(argv);
        return ok;
    }

    void LogUsage()
    {
        Log(L"Usage:");
        Log(L"  Win11Theme.exe");
        Log(L"  Win11Theme.exe -force");
        Log(L"  Win11Theme.exe -ini \"C:\\Path\\theme.ini\"");
        Log(L"  Win11Theme.exe -force -ini \"C:\\Path\\theme.ini\"");
        Log(L"  Win11Theme.exe /force /ini \"C:\\Path\\theme.ini\"");
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    if (!InitLogging())
        return 1;

    if (!AcquireSingleInstanceMutex())
    {
        Cleanup();
        return 0;
    }

    AppOptions options;
    if (!ParseCommandLine(options))
    {
        Log(L"Invalid command line.");
        LogUsage();
        Cleanup();
        return 2;
    }

    if (options.force)
        Log(L"Force mode enabled. Completion marker will be ignored.");

    if (!options.force && MarkerExists())
    {
        Cleanup();
        return 0;
    }

    std::wstring configPath;
    if (!options.iniPath.empty())
    {
        configPath = options.iniPath;
        Log(L"Custom INI supplied via command line.");
    }
    else
    {
        if (!BuildDefaultConfigPath(configPath))
        {
            Log(L"Failed to resolve default config path.");
            Cleanup();
            return 3;
        }

        Log(L"No custom INI supplied. Using default config path.");
    }

    Log(L"Config path: " + configPath);

    if (!FileExists(configPath))
    {
        Log(L"Config file not found.");
        LogUsage();
        Cleanup();
        return 4;
    }

    WaitForExplorer(20000);

    bool applyOk = ApplyAllConfigValues(configPath);

    BroadcastSettingChange(L"ImmersiveColorSet");
    BroadcastSettingChange(L"WindowsThemeElement");
    BroadcastSettingChange(L"Environment");

    Sleep(1500);

    BroadcastSettingChange(L"ImmersiveColorSet");
    BroadcastSettingChange(L"WindowsThemeElement");

    if (applyOk)
    {
        if (!options.force)
        {
            WriteMarker();
        }
        else
        {
            Log(L"Force mode used. Completion marker not updated.");
        }
    }
    else
    {
        Log(L"One or more config values failed to apply. Marker will not be written.");
    }

    Cleanup();
    return applyOk ? 0 : 5;
}