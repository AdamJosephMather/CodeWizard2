// cwremote is CodeWizard's small remote filesystem helper.
//
// It writes one JSON handshake line, then exchanges length-prefixed frames on
// stdin/stdout. The process is deliberately dependency-free so it can be built
// on a remote machine with only the Go toolchain.
package main

import (
	"bufio"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
)

const (
	protocolVersion = 1
	rpcChannel      = byte(0)
	maxFrameSize    = 64 * 1024 * 1024
)

type handshake struct {
	Protocol int    `json:"protocol"`
	OS       string `json:"os"`
	Arch     string `json:"arch"`
	Home     string `json:"home"`
	Cwd      string `json:"cwd"`
	Shell    string `json:"shell"`
	Hostname string `json:"hostname"`
}

type request struct {
	ID     uint64          `json:"id"`
	Method string          `json:"method"`
	Params json.RawMessage `json:"params"`
}

type response struct {
	ID    uint64 `json:"id"`
	OK    bool   `json:"ok"`
	Data  any    `json:"data,omitempty"`
	Error string `json:"error,omitempty"`
}

type fileStat struct {
	Size   int64 `json:"size"`
	Mtime  int64 `json:"mtime"`
	IsDir  bool  `json:"isDir"`
	Exists bool  `json:"exists"`
}

type directoryEntry struct {
	Name  string `json:"name"`
	IsDir bool   `json:"isDir"`
	Size  int64  `json:"size"`
	Mtime int64  `json:"mtime"`
}

type server struct {
	in   *bufio.Reader
	out  *bufio.Writer
	info handshake
}

func environmentInfo() handshake {
	home, _ := os.UserHomeDir()
	cwd, _ := os.Getwd()
	hostname, _ := os.Hostname()
	shell := os.Getenv("SHELL")
	if shell == "" {
		if runtime.GOOS == "windows" {
			shell = os.Getenv("COMSPEC")
		} else {
			shell = "/bin/sh"
		}
	}
	return handshake{
		Protocol: protocolVersion,
		OS:       runtime.GOOS,
		Arch:     runtime.GOARCH,
		Home:     home,
		Cwd:      cwd,
		Shell:    shell,
		Hostname: hostname,
	}
}

func install() {
	if runtime.GOOS != "linux" {
		fmt.Fprintln(os.Stderr, "cwremote: --install is only supported on linux")
		os.Exit(1)
	}

	exe, err := os.Executable()
	if err != nil {
		fmt.Fprintln(os.Stderr, "cwremote:", err)
		os.Exit(1)
	}
	exe, err = filepath.EvalSymlinks(exe)
	if err != nil {
		fmt.Fprintln(os.Stderr, "cwremote:", err)
		os.Exit(1)
	}

	src, err := os.Open(exe)
	if err != nil {
		fmt.Fprintln(os.Stderr, "cwremote:", err)
		os.Exit(1)
	}
	defer src.Close()

	info, err := src.Stat()
	if err != nil {
		fmt.Fprintln(os.Stderr, "cwremote:", err)
		os.Exit(1)
	}

	dst, err := os.OpenFile("/usr/local/bin/cwremote", os.O_WRONLY|os.O_CREATE|os.O_TRUNC, info.Mode())
	if err != nil {
		fmt.Fprintln(os.Stderr, "cwremote:", err)
		os.Exit(1)
	}
	defer dst.Close()

	if _, err := io.Copy(dst, src); err != nil {
		fmt.Fprintln(os.Stderr, "cwremote:", err)
		os.Exit(1)
	}

	if err := dst.Close(); err != nil {
		fmt.Fprintln(os.Stderr, "cwremote:", err)
		os.Exit(1)
	}

	fmt.Println("cwremote: installed to /usr/local/bin/cwremote")
}

func main() {
	if len(os.Args) > 1 && os.Args[1] == "--install" {
		install()
		return
	}

	s := &server{
		in:   bufio.NewReader(os.Stdin),
		out:  bufio.NewWriter(os.Stdout),
		info: environmentInfo(),
	}
	if err := s.run(); err != nil && !errors.Is(err, io.EOF) {
		fmt.Fprintln(os.Stderr, "cwremote:", err)
		os.Exit(1)
	}
}

func (s *server) run() error {
	line, err := json.Marshal(s.info)
	if err != nil {
		return err
	}
	if _, err = s.out.Write(append(line, '\n')); err != nil {
		return err
	}
	if err = s.out.Flush(); err != nil {
		return err
	}

	for {
		channel, payload, err := readFrame(s.in)
		if err != nil {
			return err
		}
		if channel != rpcChannel {
			return fmt.Errorf("unsupported channel 0x%02x", channel)
		}

		var req request
		if err := json.Unmarshal(payload, &req); err != nil {
			if writeErr := s.writeResponse(response{OK: false, Error: "invalid request: " + err.Error()}); writeErr != nil {
				return writeErr
			}
			continue
		}
		data, callErr := s.handle(req.Method, req.Params)
		resp := response{ID: req.ID, OK: callErr == nil, Data: data}
		if callErr != nil {
			resp.Error = callErr.Error()
			resp.Data = nil
		}
		if err := s.writeResponse(resp); err != nil {
			return err
		}
	}
}

func readFrame(r io.Reader) (byte, []byte, error) {
	var size uint32
	if err := binary.Read(r, binary.BigEndian, &size); err != nil {
		return 0, nil, err
	}
	if size < 1 || size > maxFrameSize {
		return 0, nil, fmt.Errorf("invalid frame size %d", size)
	}
	frame := make([]byte, size)
	if _, err := io.ReadFull(r, frame); err != nil {
		return 0, nil, err
	}
	return frame[0], frame[1:], nil
}

func writeFrame(w io.Writer, channel byte, payload []byte) error {
	if len(payload)+1 > maxFrameSize {
		return fmt.Errorf("response exceeds maximum frame size")
	}
	if err := binary.Write(w, binary.BigEndian, uint32(len(payload)+1)); err != nil {
		return err
	}
	if _, err := w.Write([]byte{channel}); err != nil {
		return err
	}
	_, err := w.Write(payload)
	return err
}

func (s *server) writeResponse(resp response) error {
	payload, err := json.Marshal(resp)
	if err != nil {
		return err
	}
	if err = writeFrame(s.out, rpcChannel, payload); err != nil {
		return err
	}
	return s.out.Flush()
}

func decodeParams(raw json.RawMessage, value any) error {
	if len(raw) == 0 {
		raw = json.RawMessage(`{}`)
	}
	if err := json.Unmarshal(raw, value); err != nil {
		return fmt.Errorf("invalid params: %w", err)
	}
	return nil
}

func cleanPath(path string) (string, error) {
	if strings.TrimSpace(path) == "" {
		return "", errors.New("path is required")
	}
	if strings.HasPrefix(path, "~/") || strings.HasPrefix(path, `~\`) {
		home, err := os.UserHomeDir()
		if err != nil {
			return "", err
		}
		path = filepath.Join(home, path[2:])
	}
	return filepath.Clean(path), nil
}

func statPath(path string) (fileStat, error) {
	info, err := os.Stat(path)
	if errors.Is(err, os.ErrNotExist) {
		return fileStat{Exists: false}, nil
	}
	if err != nil {
		return fileStat{}, err
	}
	return fileStat{
		Size:   info.Size(),
		Mtime:  info.ModTime().Unix(),
		IsDir:  info.IsDir(),
		Exists: true,
	}, nil
}

func atomicWrite(path string, content []byte) error {
	parent := filepath.Dir(path)
	if err := os.MkdirAll(parent, 0o755); err != nil {
		return err
	}
	mode := os.FileMode(0o644)
	if info, err := os.Stat(path); err == nil {
		mode = info.Mode().Perm()
	}
	temp, err := os.CreateTemp(parent, "."+filepath.Base(path)+".cwtmp-*")
	if err != nil {
		return err
	}
	tempName := temp.Name()
	defer os.Remove(tempName)

	if err = temp.Chmod(mode); err == nil {
		_, err = temp.Write(content)
	}
	if err == nil {
		err = temp.Sync()
	}
	closeErr := temp.Close()
	if err == nil {
		err = closeErr
	}
	if err != nil {
		return err
	}
	return os.Rename(tempName, path)
}

func isBinary(data []byte) bool {
	for _, b := range data {
		if b == 0 {
			return true
		}
	}
	return false
}

func (s *server) handle(method string, raw json.RawMessage) (any, error) {
	switch method {
	case "env/info":
		s.info = environmentInfo()
		return s.info, nil

	case "file/read":
		var p struct {
			Path string `json:"path"`
		}
		if err := decodeParams(raw, &p); err != nil {
			return nil, err
		}
		path, err := cleanPath(p.Path)
		if err != nil {
			return nil, err
		}
		content, err := os.ReadFile(path)
		if err != nil {
			return nil, err
		}
		return map[string]any{
			"content": base64.StdEncoding.EncodeToString(content),
			"size":    len(content),
		}, nil

	case "file/write":
		var p struct {
			Path    string `json:"path"`
			Content string `json:"content"`
		}
		if err := decodeParams(raw, &p); err != nil {
			return nil, err
		}
		path, err := cleanPath(p.Path)
		if err != nil {
			return nil, err
		}
		content, err := base64.StdEncoding.DecodeString(p.Content)
		if err != nil {
			return nil, fmt.Errorf("invalid base64 content: %w", err)
		}
		if err = atomicWrite(path, content); err != nil {
			return nil, err
		}
		return map[string]any{"ok": true}, nil

	case "file/stat", "file/exists", "file/mtime":
		var p struct {
			Path string `json:"path"`
		}
		if err := decodeParams(raw, &p); err != nil {
			return nil, err
		}
		path, err := cleanPath(p.Path)
		if err != nil {
			return nil, err
		}
		stat, err := statPath(path)
		if err != nil {
			return nil, err
		}
		switch method {
		case "file/exists":
			return map[string]any{"exists": stat.Exists}, nil
		case "file/mtime":
			return map[string]any{"mtime": stat.Mtime}, nil
		default:
			return stat, nil
		}

	case "file/isBinary":
		var p struct {
			Path string `json:"path"`
		}
		if err := decodeParams(raw, &p); err != nil {
			return nil, err
		}
		path, err := cleanPath(p.Path)
		if err != nil {
			return nil, err
		}
		f, err := os.Open(path)
		if err != nil {
			return nil, err
		}
		defer f.Close()
		preview := make([]byte, 4096)
		n, readErr := f.Read(preview)
		if readErr != nil && !errors.Is(readErr, io.EOF) {
			return nil, readErr
		}
		preview = preview[:n]
		return map[string]any{
			"isBinary": isBinary(preview),
			"preview":  base64.StdEncoding.EncodeToString(preview),
		}, nil

	case "dir/list":
		var p struct {
			Path string `json:"path"`
		}
		if err := decodeParams(raw, &p); err != nil {
			return nil, err
		}
		path, err := cleanPath(p.Path)
		if err != nil {
			return nil, err
		}
		items, err := os.ReadDir(path)
		if err != nil {
			return nil, err
		}
		entries := make([]directoryEntry, 0, len(items))
		for _, item := range items {
			info, infoErr := item.Info()
			if infoErr != nil {
				continue
			}
			entries = append(entries, directoryEntry{
				Name:  item.Name(),
				IsDir: item.IsDir(),
				Size:  info.Size(),
				Mtime: info.ModTime().Unix(),
			})
		}
		sort.Slice(entries, func(i, j int) bool {
			if entries[i].IsDir != entries[j].IsDir {
				return entries[i].IsDir
			}
			return strings.ToLower(entries[i].Name) < strings.ToLower(entries[j].Name)
		})
		return map[string]any{"entries": entries}, nil

	case "file/delete":
		var p struct {
			Path string `json:"path"`
		}
		if err := decodeParams(raw, &p); err != nil {
			return nil, err
		}
		path, err := cleanPath(p.Path)
		if err != nil {
			return nil, err
		}
		return map[string]any{"ok": true}, os.Remove(path)

	case "file/rename":
		var p struct {
			OldPath string `json:"oldPath"`
			NewPath string `json:"newPath"`
		}
		if err := decodeParams(raw, &p); err != nil {
			return nil, err
		}
		oldPath, err := cleanPath(p.OldPath)
		if err != nil {
			return nil, err
		}
		newPath, err := cleanPath(p.NewPath)
		if err != nil {
			return nil, err
		}
		return map[string]any{"ok": true}, os.Rename(oldPath, newPath)

	case "file/mkdir":
		var p struct {
			Path string `json:"path"`
		}
		if err := decodeParams(raw, &p); err != nil {
			return nil, err
		}
		path, err := cleanPath(p.Path)
		if err != nil {
			return nil, err
		}
		return map[string]any{"ok": true}, os.MkdirAll(path, 0o755)

	case "file/scan":
		var p struct {
			Path     string `json:"path"`
			MaxFiles uint64 `json:"maxFiles"`
		}
		if err := decodeParams(raw, &p); err != nil {
			return nil, err
		}
		root, err := cleanPath(p.Path)
		if err != nil {
			return nil, err
		}
		max := p.MaxFiles
		if max == 0 {
			max = 2000
		}

		type scannedFile struct {
			Name string `json:"name"`
			Path string `json:"path"`
		}
		files := make([]scannedFile, 0, max)
		queue := []string{root}
		seen := uint64(0)

		for len(queue) > 0 && seen < max {
			curDir := queue[0]
			queue = queue[1:]

			items, readErr := os.ReadDir(curDir)
			if readErr != nil {
				continue
			}
			for _, item := range items {
				if seen >= max {
					break
				}
				name := item.Name()
				if item.IsDir() {
					if len(name) > 0 && name[0] != '.' {
						queue = append(queue, filepath.Join(curDir, name))
					}
					continue
				}
				if !item.Type().IsRegular() {
					continue
				}
				fullPath := filepath.Join(curDir, name)
				files = append(files, scannedFile{Name: name, Path: fullPath})
				seen++
			}
		}

		return map[string]any{"files": files}, nil

	case "file/search":
		var p struct {
			Files      []string `json:"files"`
			SearchTerm string   `json:"searchTerm"`
		}
		if err := decodeParams(raw, &p); err != nil {
			return nil, err
		}
		if strings.TrimSpace(p.SearchTerm) == "" {
			return map[string]any{"results": []any{}}, nil
		}

		lowerTerm := strings.ToLower(p.SearchTerm)
		termLen := len(lowerTerm)

		type searchMatch struct {
			Line    int    `json:"line"`
			Content string `json:"content"`
		}
		type searchResult struct {
			Path    string        `json:"path"`
			Matches []searchMatch `json:"matches"`
		}
		results := make([]searchResult, 0)

		for _, filePath := range p.Files {
			cleaned, cleanErr := cleanPath(filePath)
			if cleanErr != nil {
				continue
			}

			data, readErr := os.ReadFile(cleaned)
			if readErr != nil {
				continue
			}
			if isBinary(data) {
				continue
			}

			content := string(data)
			lowerContent := strings.ToLower(content)
			var matches []searchMatch
			lineNum := 1
			lineStart := 0

			for i := 0; i <= len(lowerContent)-termLen; i++ {
				if lowerContent[i] == '\n' {
					lineNum++
					lineStart = i + 1
				}
				if strings.HasPrefix(lowerContent[i:], lowerTerm) {
					endLine := strings.IndexByte(content[lineStart:], '\n')
					if endLine < 0 {
						endLine = len(content) - lineStart
					}
					line := strings.TrimRight(content[lineStart:lineStart+endLine], " \t\r\n")
					matches = append(matches, searchMatch{Line: lineNum, Content: line})
					// skip to end of this line so we don't match twice on the same line
					if i < len(lowerContent) {
						nl := strings.IndexByte(lowerContent[i:], '\n')
						if nl >= 0 {
							i += nl
						} else {
							break
						}
					}
				}
			}

			if len(matches) > 0 {
				results = append(results, searchResult{Path: cleaned, Matches: matches})
			}
		}

		return map[string]any{"results": results}, nil

	default:
		return nil, fmt.Errorf("unknown method %q", method)
	}
}
