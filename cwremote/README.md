# cwremote

`cwremote` is the dependency-free remote helper used by CodeWizard's SSH mode.
It serves filesystem RPCs over stdin/stdout and is normally launched through
the user's existing `ssh` executable.

Build and test it with:

```sh
go test ./...
go build -o cwremote .
```

Copy the resulting binary to the remote machine and put it on `PATH`, or pass
its full path in the CodeWizard SSH connection settings.

On startup the helper emits one JSON handshake line. Every later message is a
big-endian 32-bit length, a one-byte channel tag, and a JSON payload. Protocol
version 1 currently supports:

- `env/info`
- `file/read`, `file/write`, `file/stat`, `file/exists`, `file/mtime`
- `file/isBinary`, `file/delete`, `file/rename`, `file/mkdir`
- `dir/list`

Writes use a temporary file in the destination directory followed by a rename,
so successful saves are atomic on normal local filesystems.
