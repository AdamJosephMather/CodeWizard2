package main

import (
	"archive/zip"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"os/exec"

	"golang.org/x/sys/windows/registry"
)

func main() {
	// Determine install directory
	local := os.Getenv("LOCALAPPDATA")
	if local == "" {
		fatal("LOCALAPPDATA is empty; cannot determine install path")
	}
	installDir := filepath.Join(local, "CodeWizard")
	//installDir = "C:\\Users\\adamj\\Downloads\\TestCodeWizardInstall"

	fmt.Println("Installer starting")

	exists, err := dirExists(installDir)
	fatalIf(err)

	if !exists {
		fatalIf(os.MkdirAll(installDir, 0o755))
		fmt.Println("Created:", installDir)
	}

	if exists {
		choice := ""
		fmt.Print("Do you want to uninstall, update, or re-register? [uninstall/update/reregister (rere)]: ")
		fmt.Scanln(&choice)
		
		if strings.ToLower(choice) == "uninstall" {
			fmt.Println("Uninstalling CodeWizard...")
			destroyDir(installDir)
			unregister()
			fmt.Println("Uninstalled CodeWizard.")
		} else if strings.ToLower(choice) == "update" {
			fmt.Println("Updating CodeWizard...")
			destroyDir(installDir+"\\CodeWizard")
			zipPath, err := resolveBesideMe("CodeWizard.zip")
			fatalIf(err)
			fatalIf(extractZip(zipPath, installDir))
		} else if strings.ToLower(choice) == "reregister" || strings.ToLower(choice) == "rere" {
			fmt.Println("Registering CodeWizard...")
			register(installDir)
			fmt.Println("Registered CodeWizard.")
		} else {
			fmt.Println("Unclear operation requested.")
		}
	}else {
		choice := ""
		fmt.Print("Do you want to install CodeWizard? ")
		fmt.Scanln(&choice)
		
		if strings.ToLower(choice) == "yes" || strings.ToLower(choice) == "y" {
			fmt.Println("Installing CodeWizard...")
			zipPath, err := resolveBesideMe("CodeWizard.zip")
			fatalIf(err)
			fatalIf(extractZip(zipPath, installDir))
			
			zipPath, err = resolveBesideMe("Extras.zip")
			fatalIf(err)
			fatalIf(extractZip(zipPath, installDir))
			
			register(installDir)
		}else {
			fmt.Println("No work was done.")
		}
	}
	
	fmt.Println("Done. Press enter to finish.")
	fmt.Scanln()
}

func destroyDir(path string) error {
	// Check if the directory exists first
	info, err := os.Stat(path)
	if os.IsNotExist(err) {
		return nil // nothing to delete, so that's fine
	}
	if err != nil {
		return err
	}

	// Ensure it's a directory before removing
	if !info.IsDir() {
		return fmt.Errorf("%s is not a directory", path)
	}

	// Remove recursively
	return os.RemoveAll(path)
}

func resolveBesideMe(name string) (string, error) {
	exe, err := os.Executable()
	if err != nil {
		return "", err
	}
	base := filepath.Dir(exe)
	candidate := filepath.Join(base, name)
	if _, err := os.Stat(candidate); err == nil {
		return candidate, nil
	}
	// fall back to cwd
	if _, err := os.Stat(name); err == nil {
		return name, nil
	}
	return "", fmt.Errorf("payload not found: tried %q and %q", candidate, name)
}

func dirExists(path string) (bool, error) {
	info, err := os.Stat(path)
	if err == nil {
		return info.IsDir(), nil
	}
	if os.IsNotExist(err) {
		return false, nil
	}
	return false, err
}

func extractZip(zipPath, dest string) error {
	r, err := zip.OpenReader(zipPath)
	if err != nil {
		return err
	}
	defer r.Close()

	// Detect common top-level folder
	var topLevel string
	for _, f := range r.File {
		parts := strings.SplitN(f.Name, "/", 2)
		if len(parts) > 1 {
			topLevel = parts[0]
			break
		}
	}

	for _, f := range r.File {
		name := f.Name
		if topLevel != "" && strings.HasPrefix(name, topLevel+"/") {
			name = strings.TrimPrefix(name, topLevel+"/")
		}
		
		relPath, err := sanitizeZipPath(name)
		if err != nil {
			return err
		}
		if relPath == "" {
			continue
		}

		targetPath := filepath.Join(dest, relPath)

		if f.FileInfo().IsDir() {
			if err := os.MkdirAll(targetPath, f.Mode()); err != nil {
				return err
			}
			continue
		}

		if err := os.MkdirAll(filepath.Dir(targetPath), 0o755); err != nil {
			return err
		}

		if err := writeZipFile(f, targetPath); err != nil {
			return err
		}
	}
	return nil
}

func sanitizeZipPath(name string) (string, error) {
	// prevent Zip Slip: no drive letters, no absolute paths, no .. to escape
	name = strings.ReplaceAll(name, "\\", "/")
	if strings.HasPrefix(name, "/") || strings.Contains(name, ":") {
		return "", errors.New("invalid zip entry path")
	}
	clean := filepath.Clean(name)
	if clean == ".." || strings.HasPrefix(clean, ".."+string(filepath.Separator)) {
		return "", errors.New("zip entry attempts path traversal")
	}
	return clean, nil
}

func writeZipFile(f *zip.File, target string) error {
	rc, err := f.Open()
	if err != nil {
		return err
	}
	defer rc.Close()

	out, err := os.OpenFile(target, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, f.Mode())
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, rc)
	return err
}

func register(installLoc string) error {
	exe := filepath.Join(installLoc, "CodeWizard", "CodeWizard.exe")

	// NOTE: path is relative to registry root (no HKEY_CURRENT_USER prefix)
	const subkey = `Software\Microsoft\Windows\CurrentVersion\App Paths\CodeWizard.exe`

	k, _, err := registry.CreateKey(registry.CURRENT_USER, subkey, registry.WRITE|registry.SET_VALUE)
	if err != nil { return err }
	defer k.Close()

	// (Default) value = full path to the exe
	if err := k.SetStringValue("", exe); err != nil { return err }

	// Optional: semicolon-separated extra lookup paths for DLLs/etc.
	if err := k.SetStringValue("Path", filepath.Join(installLoc, "CodeWizard")); err != nil { return err }
	
	fatalIf(createStartMenuShortcut("CodeWizard", exe, "", filepath.Dir(exe), exe))

	return nil
}

func createStartMenuShortcut(name, target, args, workingDir, icon string) error {
	// %AppData%\Microsoft\Windows\Start Menu\Programs\YourApp.lnk
	linkPath := filepath.Join(
		os.Getenv("AppData"),
		"Microsoft", "Windows", "Start Menu", "Programs",
		name+".lnk",
	)

	// single-quote escape for PowerShell
	esc := func(s string) string { return strings.ReplaceAll(s, `'`, `''`) }

	ps := fmt.Sprintf(`
$W = New-Object -ComObject WScript.Shell
$S = $W.CreateShortcut('%s')
$S.TargetPath = '%s'
$S.Arguments = '%s'
$S.WorkingDirectory = '%s'
$S.IconLocation = '%s'
$S.Save()`,
		esc(linkPath),
		esc(target),
		esc(args),
		esc(workingDir),
		esc(icon),
	)

	cmd := exec.Command("powershell", "-NoProfile", "-NonInteractive", "-Command", ps)
	cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
	return cmd.Run()
}

func unregister() error {
	// 1) Remove App Paths entry (HKCU\Software\...\App Paths\CodeWizard.exe)
	const subkey = `Software\Microsoft\Windows\CurrentVersion\App Paths\CodeWizard.exe`
	_ = registry.DeleteKey(registry.CURRENT_USER, subkey) // ignore if it doesn't exist

	// 2) Remove Start menu shortcut(s)
	removeStartMenuShortcut("CodeWizard")

	return nil
}

// removeStartMenuShortcut deletes the .lnk from user and common Start Menu locations.
func removeStartMenuShortcut(name string) {
	// Per-user Start Menu
	if appData := os.Getenv("AppData"); appData != "" {
		userLink := filepath.Join(
			appData, "Microsoft", "Windows", "Start Menu", "Programs", name+".lnk",
		)
		_ = os.Remove(userLink) // ignore if not found
	}

	// All users (requires admin to have been created there)
	if programData := os.Getenv("ProgramData"); programData != "" {
		commonLink := filepath.Join(
			programData, "Microsoft", "Windows", "Start Menu", "Programs", name+".lnk",
		)
		_ = os.Remove(commonLink) // ignore if not found
	}
}

func fatalIf(err error) {
	if err != nil {
		fatal(err.Error())
	}
}

func fatal(msg string) {
	fmt.Fprintln(os.Stderr, "ERROR:", msg)
	os.Exit(1)
}
