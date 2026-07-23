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

type oneByteReader struct {
	r io.Reader
}

func (r *oneByteReader) Read(p []byte) (int, error) {
	if len(p) > 1 {
		p = p[:1]
	}
	return r.r.Read(p)
}
