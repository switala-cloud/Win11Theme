# Win11Theme
Standalone Windows 11 utility that applies Start menu and accent colours from a configurable INI file. Designed for FSLogix and first-logon scenarios, with logging, idempotent execution, and optional force override. No PowerShell or external dependencies.

## 🧰 Prerequisites

### Required Software

- Windows Operating System
- [Visual Studio Code](https://code.visualstudio.com/)
- [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/)
- Windows SDK (installed via Build Tools)

---

## 📦 Installation

### 1. Install VS Code

1. Download and install VS Code  
2. During setup, enable:
   - Add to PATH
   - "Open with Code" (optional)

---

### 2. Install VS Code Extension

Open VS Code → Extensions → install:

- **C/C++ (Microsoft)**

---

### 3. Install Visual Studio Build Tools

1. Download from:  
   https://visualstudio.microsoft.com/downloads/

2. Select:  
   **Build Tools for Visual Studio**

3. In the installer:

#### ✔ Workload:
- Desktop development with C++

#### ✔ Ensure components:
- MSVC v143 (or latest)
- Windows 10/11 SDK
- C++ core build tools

Click **Install**

---

## 🧪 Verify Installation

Open:x64 Native Tools Command Prompt for VS
Run: cl
Expected output:
Microsoft (R) C/C++ Optimizing Compiler...

---

## 📁 Project Setup

### 1. Create project directory

C:\Dev\Win11Theme

### 2. Open in VS Code

From the Native Tools Command Prompt:
code C:\Dev\Win11Theme

⚠️ Important: Always launch VS Code from this prompt so `cl.exe` is available

---

### 3. Create source file

Create:
main.cpp
Paste your application code.

---

### 4. Create build configuration

Create folder:
.vscode
Inside it, create:

#### `tasks.json`

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Build Win11Theme",
      "type": "shell",
      "command": "cl.exe",
      "args": [
        "/EHsc",
        "/MT",
        "/std:c++17",
        "/DUNICODE",
        "/D_UNICODE",
        "/Fe:${workspaceFolder}\\Win11Theme.exe",
        "${workspaceFolder}\\main.cpp",
        "Shell32.lib",
        "User32.lib",
        "Advapi32.lib",
        "Ole32.lib"
      ],
      "group": {
        "kind": "build",
        "isDefault": true
      },
      "problemMatcher": [
        "$msCompile"
      ]
    }
  ]
}
```
---

## 🛠️ Build

In VS Code press:


Ctrl + Shift + B


Select:


Build Win11Theme


---

## ✅ Output


C:\Dev\Win11Theme\Win11Theme.exe


---

## ▶️ Usage

### Default config


Win11Theme.exe


### Custom INI


Win11Theme.exe -ini "C:\ProgramData\Win11Theme\theme.ini"


### Force re-run (App includes logic to only ever run one once. Marker in HKCU Software\Win11Theme\Completed )


Win11Theme.exe -force


### Combined


Win11Theme.exe -force -ini "C:\Path\theme.ini"


---

## 📁 Runtime Requirements

- Windows 11 user session

### Config file (Default Location)


C:\ProgramData\Win11Theme\theme.ini


### Log location


%APPDATA%\Win11Theme\ThemeRefresh.log


---

## ⚠️ Common Issues

### ❌ `cl.exe` not found

Launch VS Code from:


x64 Native Tools Command Prompt


---

### ❌ Linker errors (LNK2019)

Ensure these libraries are included:


Shell32.lib
User32.lib
Advapi32.lib
Ole32.lib
