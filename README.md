# CodeWizard2

A rewrite of CodeWizard. CodeWizard2 is *probably not* a useful project to anybody besides myself.  
*However* it is **much** faster than some IDEs, particularly on laptops (maintaining 60fps until it transitions to 'sleep' mode).
*And* CodeWizard maintains under 100 Mb ram while running. Usually idles around 60-70 Mb (with several files open).

## What's special?

1. CodeWizard2 now has custom rendering *without* a UI framework like Qt. CodeWizard2 uses OpenGL, and GLFW.
2. CodeWizard2 has a custom written remake of the TextMate highlighting engine (with several known issues) that works pretty well.
3. It contains a custom LSP implimentation originally written for CodeWizard.
4. CodeWizard2 is *not* for the average person, there is a lot of undocumented features and keybindings. Which I am *not* changing right now.
5. We use a 'panel' such that you can have whatever arrangement of elements you want.

## Screenshot(s)

![image](https://github.com/user-attachments/assets/1eebe5f3-637c-4890-98dc-a1e50f01dd62)
With multiple panels open
![image](https://github.com/user-attachments/assets/6ec91b67-7702-49cd-b942-298cdb7dc8f9)
Just to show off a little
![image](https://github.com/user-attachments/assets/e8b650bc-4fbd-429c-a919-a5790c723c83)
No, there's no limit to the number or configuration of panels.

## Quick note

CodeWizard2 is only available on Windows. There is a modal option (which I quite enjoy but doesn't match any other editors) available in the settings.

## Important keybindings:

1. Ctrl+Shift+P focus the command palette.
2. Ctrl+Shift+O change project folder.
3. Ctrl+O open folder.
4. Ctrl+Shift+U run project search through command palette.
5. Ctrl+> or Ctrl+< to jump brackets.
6. F5 run programs

## To install:

1. Download source code
2. Build source code
3. Copy app.png and the 'cascadia' folder into the resulting build folder.
4. The resulting CodeWizard.exe should work as an app if you run it (you can pin it to the taskbar).
5. To set languages for CodeWizard, create a file (if it doesn't exist) under C:\Users\<username>\AppData\Local\CodeWizard\languages.json (an example file is below)
6. For every language, you will likely want a textmate file which you can point to in the languages.json (note '%INSTALL_DIR%' will be replaced with 'C:\Users\<username>\AppData\Local\CodeWizard') - the textmate language files can be found here: https://github.com/microsoft/vscode/tree/main/extensions (look for .tmLanguage.json)
7. Project specific settings can be created via opening the settings panel and presseing 'Project Specific' which will create a json file and give you the path. This can be used to set specific LSPs and the project build command.

Example languages.json
```json
{
"languages": [
	{
		"name": "c++",
		"line_comment": "//",
		"textmatefile": "%INSTALL_DIR%\\highlightingfiles\\cpp.tmLanguage.json",
		"filetypes": ["cpp", "h", "hpp", "c"],
		"lsp_command": "C:\\Users\\adamj\\Documents\\LanguageServers\\clangd_19.1.2\\bin\\clangd.exe",
		"build_command": "cd /d %FILE_LOCATION% && call \"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat\" x64 && cl.exe %FILE_NAME% && %FILE_NAME_NO_EXT%.exe"
	},
	{
		"name": "python",
		"line_comment": "#",
		"textmatefile": "%INSTALL_DIR%\\highlightingfiles\\MagicPython.tmLanguage.json",
		"filetypes": ["py", "pyw"],
		"lsp_command": "jedi-language-server",
		"build_command": "cd /d %FILE_LOCATION% && python %FILE_NAME%"
	},
	{
		"name": "go",
		"line_comment": "//",
		"textmatefile": "%INSTALL_DIR%\\highlightingfiles\\go.tmLanguage.json",
		"filetypes": ["go"],
		"lsp_command": "gopls",
		"build_command": "cd /d %FILE_LOCATION% && go run ."
	},
	{
		"name": "r",
		"line_comment": "#",
		"textmatefile": "%INSTALL_DIR%\\highlightingfiles\\r.tmLanguage.json",
		"filetypes": ["R", "R~"],
		"lsp_command": "",
		"build_command": ""
	}
]
}
```
