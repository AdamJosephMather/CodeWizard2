package main

import (
	"bufio"
	"bytes"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"io"
	"os"
	"path/filepath"
	"testing"
)

func call(t *testing.T, s *server, method string, params any) any {
	t.Helper()
	raw, err := json.Marshal(params)
	if err != nil {
		t.Fatal(err)
	}
	data, err := s.handle(method, raw)
	if err != nil {
		t.Fatalf("%s: %v", method, err)
	}
	return data
}

func TestFileLifecycle(t *testing.T) {
	root := t.TempDir()
	path := filepath.Join(root, "nested", "hello.txt")
	s := &server{}

	content := []byte("hello from cwremote\n")
	call(t, s, "file/write", map[string]any{
		"path":    path,
		"content": base64.StdEncoding.EncodeToString(content),
	})

	exists := call(t, s, "file/exists", map[string]any{"path": path}).(map[string]any)
	if exists["exists"] != true {
		t.Fatal("written file does not exist")
	}

	read := call(t, s, "file/read", map[string]any{"path": path}).(map[string]any)
	decoded, err := base64.StdEncoding.DecodeString(read["content"].(string))
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(decoded, content) {
		t.Fatalf("read %q, want %q", decoded, content)
	}

	replacement := []byte("replacement\n")
	call(t, s, "file/write", map[string]any{
		"path":    path,
		"content": base64.StdEncoding.EncodeToString(replacement),
	})
	read = call(t, s, "file/read", map[string]any{"path": path}).(map[string]any)
	decoded, err = base64.StdEncoding.DecodeString(read["content"].(string))
	if err != nil || !bytes.Equal(decoded, replacement) {
		t.Fatalf("atomic overwrite read %q, want %q (error: %v)", decoded, replacement, err)
	}

	list := call(t, s, "dir/list", map[string]any{"path": filepath.Dir(path)}).(map[string]any)
	entries := list["entries"].([]directoryEntry)
	if len(entries) != 1 || entries[0].Name != "hello.txt" {
		t.Fatalf("unexpected listing: %#v", entries)
	}

	call(t, s, "file/delete", map[string]any{"path": path})
	exists = call(t, s, "file/exists", map[string]any{"path": path}).(map[string]any)
	if exists["exists"] != false {
		t.Fatal("deleted file still exists")
	}
}

func TestBinaryPreview(t *testing.T) {
	path := filepath.Join(t.TempDir(), "binary.dat")
	if err := os.WriteFile(path, []byte{1, 2, 0, 4}, 0o644); err != nil {
		t.Fatal(err)
	}
	got := call(t, &server{}, "file/isBinary", map[string]any{"path": path}).(map[string]any)
	if got["isBinary"] != true {
		t.Fatal("NUL-containing file was not detected as binary")
	}
}

func TestFrameRoundTripAndPartialReads(t *testing.T) {
	var encoded bytes.Buffer
	payload := []byte(`{"id":7}`)
	if err := writeFrame(&encoded, rpcChannel, payload); err != nil {
		t.Fatal(err)
	}
	channel, got, err := readFrame(&oneByteReader{r: bytes.NewReader(encoded.Bytes())})
	if err != nil {
		t.Fatal(err)
	}
	if channel != rpcChannel || !bytes.Equal(got, payload) {
		t.Fatalf("got channel=%d payload=%q", channel, got)
	}
}

func TestRejectsOversizedFrame(t *testing.T) {
	var encoded bytes.Buffer
	if err := binary.Write(&encoded, binary.BigEndian, uint32(maxFrameSize+1)); err != nil {
		t.Fatal(err)
	}
	if _, _, err := readFrame(bufio.NewReader(&encoded)); err == nil {
		t.Fatal("expected oversized frame error")
	}
}

func callJSON(t *testing.T, s *server, method string, params any) map[string]any {
	t.Helper()
	raw, err := json.Marshal(params)
	if err != nil {
		t.Fatal(err)
	}
	data, err := s.handle(method, raw)
	if err != nil {
		t.Fatalf("%s: %v", method, err)
	}
	b, err := json.Marshal(data)
	if err != nil {
		t.Fatalf("%s: marshal: %v", method, err)
	}
	var out map[string]any
	if err := json.Unmarshal(b, &out); err != nil {
		t.Fatalf("%s: unmarshal: %v", method, err)
	}
	return out
}

func TestFileScan(t *testing.T) {
	root := t.TempDir()
	s := &server{}

	os.WriteFile(filepath.Join(root, "a.txt"), []byte("a\n"), 0o644)
	os.MkdirAll(filepath.Join(root, "sub"), 0o755)
	os.WriteFile(filepath.Join(root, "sub", "b.txt"), []byte("b\n"), 0o644)
	os.MkdirAll(filepath.Join(root, ".hidden"), 0o755)
	os.WriteFile(filepath.Join(root, ".hidden", "c.txt"), []byte("c\n"), 0o644)

	data := callJSON(t, s, "file/scan", map[string]any{
		"path":     root,
		"maxFiles": 100,
	})

	files := data["files"].([]any)
	if len(files) != 2 {
		t.Fatalf("expected 2 files (hidden skipped), got %d: %#v", len(files), files)
	}

	names := map[string]bool{}
	for _, f := range files {
		fm := f.(map[string]any)
		names[fm["name"].(string)] = true
	}
	if !names["a.txt"] || !names["b.txt"] {
		t.Fatalf("expected a.txt and b.txt, got %v", names)
	}
}

func TestFileScanMaxFiles(t *testing.T) {
	root := t.TempDir()
	s := &server{}

	for i := 0; i < 5; i++ {
		name := filepath.Join(root, "sub"+string(rune('a'+i)))
		os.MkdirAll(name, 0o755)
		os.WriteFile(filepath.Join(name, "file.txt"), []byte("x"), 0o644)
	}

	data := callJSON(t, s, "file/scan", map[string]any{
		"path":     root,
		"maxFiles": 3,
	})

	files := data["files"].([]any)
	if len(files) != 3 {
		t.Fatalf("expected 3 files (maxFiles limit), got %d", len(files))
	}
}

func TestFileSearch(t *testing.T) {
	root := t.TempDir()
	s := &server{}

	os.WriteFile(filepath.Join(root, "hello.go"), []byte("package main\n\nfunc Hello() {\n\tfmt.Println(\"hello world\")\n}\n"), 0o644)
	os.WriteFile(filepath.Join(root, "other.go"), []byte("package main\n\nfunc Other() {\n\tfmt.Println(\"goodbye\")\n}\n"), 0o644)
	os.WriteFile(filepath.Join(root, "binary.bin"), []byte{0, 1, 2, 0, 4}, 0o644)

	data := callJSON(t, s, "file/search", map[string]any{
		"files":      []string{filepath.Join(root, "hello.go"), filepath.Join(root, "other.go"), filepath.Join(root, "binary.bin")},
		"searchTerm": "hello",
	})

	results := data["results"].([]any)
	if len(results) != 1 {
		t.Fatalf("expected 1 file with matches, got %d: %#v", len(results), results)
	}
	r := results[0].(map[string]any)
	if r["path"] != filepath.Join(root, "hello.go") {
		t.Fatalf("expected hello.go, got %v", r["path"])
	}
	matches := r["matches"].([]any)
	if len(matches) != 2 {
		t.Fatalf("expected 2 matches (line 3 'Hello' + line 4 'hello'), got %d", len(matches))
	}
}

func TestFileSearchCaseInsensitive(t *testing.T) {
	root := t.TempDir()
	s := &server{}

	path := filepath.Join(root, "case.txt")
	os.WriteFile(path, []byte("Hello World\nHELLO again\n"), 0o644)

	data := callJSON(t, s, "file/search", map[string]any{
		"files":      []string{path},
		"searchTerm": "hello",
	})

	results := data["results"].([]any)
	if len(results) != 1 {
		t.Fatalf("expected 1 file, got %d", len(results))
	}
	r := results[0].(map[string]any)
	matches := r["matches"].([]any)
	if len(matches) != 2 {
		t.Fatalf("expected 2 case-insensitive matches, got %d", len(matches))
	}
}

type oneByteReader struct {
	r io.Reader
}

func (r *oneByteReader) Read(p []byte) (int, error) {
	if len(p) > 1 {
		p = p[:1]
	}
	return r.r.Read(p)
}
