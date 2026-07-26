# CodeWizard2

A rewrite of CodeWizard. CodeWizard2 is *probably not* a useful project to anybody besides myself.  
*However* it is **much** faster than some IDEs, particularly on laptops (maintaining 60fps until it transitions to 'sleep' mode).
*And* CodeWizard maintains under 100 Mb ram while running. Usually idles around 60-70 Mb (with several files open).

## What's Special?

1. CodeWizard2 now has custom rendering *without* a UI framework like Qt. CodeWizard2 uses OpenGL, and GLFW.
2. CodeWizard2 makes use of the Syntect project for fast highlighting. (With a lot of my signature glue keeping it attached)
3. It contains a custom LSP implementation originally written for CodeWizard.
4. CodeWizard2 is *not* for the average person, there are a lot of undocumented features and key bindings. Which I am *not* changing right now.
5. We use a 'panel' such that you can have whatever arrangement of elements you want.
6. Native SSH connection to cwremote on windows and linux

## Screenshot(s)

Ever seen Asteroids inside a code editor, with no plugins? I didn't think so.  
  
<img width="3840" height="2088" alt="image" src="https://github.com/user-attachments/assets/5eddc5af-5996-4e7b-a364-7e67764d1c80" />
<img width="2256" height="1432" alt="image" src="https://github.com/user-attachments/assets/b85e5d30-b86b-41e6-99cf-8cf50edf1828" />
<img width="2256" height="1432" alt="image" src="https://github.com/user-attachments/assets/32000715-f6c2-4342-bde7-104236f3d9da" />
<img width="2256" height="1432" alt="image" src="https://github.com/user-attachments/assets/63df446e-5361-40cf-94f7-4e239e03c65e" />
<img width="2256" height="1432" alt="image" src="https://github.com/user-attachments/assets/1e04443f-6276-43c3-bb4b-b31bf703463b" />



## Widgets

CodeWizard2 uses `widgets`, of which we have:
1. Editor View (code, images, hex viewer)
2. File Tree
3. Settings Menu
4. File Compare (compairs two files)
5. AI Chat
6. LSP Debug Window
7. Terminal
8. Math Window
9. Asteroids
10. Graph Window
11. Status Bar

## Quick Note

CodeWizard2 builds for Windows and Linux. Emoji rendering is not implemented on Linux. There is a modal option (which I quite enjoy but doesn't match any other editors) available in the settings.

## Building

The build is pinned to Zig 0.15.2, the version required by the selected Ghostty
revision. The wrapper downloads and verifies that Zig release into
`zig-version/` on first use. Zig builds CodeWizard, Ghostty, GLFW, FreeType,
libgrapheme, curl on Windows, and the other native libraries. Cargo builds the
small Syntect bridge.

Windows requires PowerShell and a Rust toolchain installed with rustup:

```powershell
.\build.ps1
```

The release is installed into `build/windows-release`. Use
`.\build.ps1 -Run` to build and launch it. The resulting executable uses the
GNU Windows ABI and does not require the Visual C++ Redistributable.

The Windows build also generates
`build/windows-release/compile_commands.json` for clangd. Configure clangd with:

```text
clangd.exe --compile-commands-dir=<CodeWizard2>/build/windows-release
```

The database contains the Windows target, compile definitions, pinned
dependency paths, and Zig libc/libc++ configuration used by the real build. It
is refreshed automatically whenever `build.ps1` runs.

Linux requires GCC, curl, Rust/rustup, and development packages for X11,
OpenGL, curl, and OpenSSL:

```bash
bash build.sh
```

The release is installed into `build/linux-release`. From Windows, the same
build can be run through WSL with:

```powershell
.\build.ps1 -Target linux-release
```

After bootstrapping, the equivalent direct Zig commands are:

```powershell
.\zig-version\windows-x86_64\zig.exe build -Dtarget=x86_64-windows-gnu --release=fast --prefix build/windows-release
```

```bash
./zig-version/linux-x86_64/zig build -Dtarget=x86_64-linux-gnu --libc /tmp/codewizard-zig-libc-$UID.conf --release=fast --prefix build/linux-release
```

## Important Key Bindings:

1. Ctrl+Shift+P focus the command palette.
2. Ctrl+Shift+O change project folder.
3. Ctrl+O open file.
4. Ctrl+Shift+U run project search through command palette.
5. Ctrl+> or Ctrl+< to jump brackets.
6. F5 run programs

## Out Of The Box

Out of the box, CodeWizard comes with a beautiful UI, Cascadia Code font, highlighting for almost any language, and `pypls`. Pypls is my LSP originally designed for python, but does pretty well in general.

## To install:

1. Download and run installer (in the releases section)
2. To add new languages for CodeWizard, open the file `C:\Users\<username>\AppData\Local\CodeWizard\languages.json` (an example file is below), or inside of CodeWizard's command palette type ```":Open `languages.json` file"``` and it will open it in CodeWizard (remember to restart CodeWizard after making changes).
3. Project specific settings can be created via opening the settings panel and pressing 'Project Specific' which will create a json file and give you the path. This can be used to set specific LSPs and the project build command.

Example languages.json
```json
{
"languages": [
	{
		"name": "c++",
		"line_comment": "//",
		"filetypes": ["cpp", "h", "hpp", "c"],
		"lsp_command": "C:\\Users\\adamj\\Documents\\LanguageServers\\clangd_19.1.2\\bin\\clangd.exe",
		"build_command": "cd /d %FILE_LOCATION% && call \"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat\" x64 && cl.exe %FILE_NAME% && %FILE_NAME_NO_EXT%.exe"
	},
	{
		"name": "python",
		"line_comment": "#",
		"filetypes": ["py", "pyw"],
		"lsp_command": "jedi-language-server",
		"build_command": "cd /d %FILE_LOCATION% && python %FILE_NAME%"
	},
	{
		"name": "go",
		"line_comment": "//",
		"filetypes": ["go"],
		"lsp_command": "gopls",
		"build_command": "cd /d %FILE_LOCATION% && go run ."
	},
	{
		"name": "r",
		"line_comment": "#",
		"filetypes": ["R", "R~"],
		"lsp_command": "%INSTALL_DIR%\\pypls.exe",
		"build_command": ""
	}
]
}
```

## To setup cwremote for remote SSH instances

First, download the executable for the host machine.

### Windows

1. Move the exe file to a sutable location
2. Edit your path to include that folder location
3. Done! (I assume you can figure out ssh)

### Linux

1. Download the cwremote for linux
2. Run `chmod +x cwremote`
3. Run `./cwremote --install`
4. You can now safely delete the downloaded file

Step 3 just coppies the file to a folder on your pc so that it's accessable. To find it you can use `which cwremote`
