# SSH remote editing

CodeWizard can use a remote filesystem and terminal over the system OpenSSH
client. The editor UI, settings, themes, fonts, syntax highlighting, and
language-server processes remain local.

## 1. Build the remote helper

The helper is a small Go program with no third-party dependencies:

```sh
cd cwremote
go test ./...
go build -o cwremote .
```

Copy it to the remote machine:

```sh
scp cwremote user@host:~/bin/cwremote
ssh user@host chmod +x ~/bin/cwremote
```

`cwremote` must be on the remote login `PATH`. Alternatively, set
`ssh_helper_path` in CodeWizard's local settings to its absolute remote path.

## 2. Confirm normal SSH authentication

CodeWizard uses the installed `ssh` executable. Configure a key/SSH agent or
enter a password in the second connection prompt. A blank password enables
OpenSSH batch mode and uses the normal key/agent configuration:

```sh
ssh -T user@host cwremote
```

The command prints a one-line JSON handshake and then waits for protocol input.
Press Ctrl+C after seeing the handshake.

The relevant optional local settings are:

```json
{
  "ssh_key_path": "",
  "ssh_helper_path": "cwremote"
}
```

An empty key path lets OpenSSH use its normal configuration, agent, and default
identity files. Host aliases and options from the user's SSH config continue to
work. Passwords are never placed in command-line arguments or settings. They
are supplied through OpenSSH's `SSH_ASKPASS` mechanism and discarded by
CodeWizard after the connection attempt.

## 3. Connect

1. Open the command palette.
2. Choose **Connect via SSH**.
3. Enter `user@host`, `user@host:port`, or `user@[IPv6-address]:port`.
4. Enter the SSH password, or leave it blank to use a key or agent.

After the handshake, CodeWizard switches the active file backend to the remote
machine and opens the helper's working directory. Use **Open Folder** to type a
different remote directory. File-open and Save As prompts also accept remote
paths.

Choose **Disconnect SSH** to close the helper and return to the previous local
folder.

## Behavior

- Reads, atomic writes, binary checks, metadata, search, directory indexing,
  compare inputs, and graph data files go through the active backend.
- The file tree loads remote directories asynchronously. Metadata displayed in
  the status bar is cached for two seconds.
- New terminal widgets run `ssh -tt` inside the existing PTY/ConPTY. The
  existing terminal parser, input, mouse, and resizing code is unchanged.
- Local language servers receive `didOpen` contents under a fake local workspace
  path. Goto-definition paths, diagnostics, and workspace edits are mapped back
  to remote paths. Workspace-wide indexing remains limited because remote files
  are not mirrored locally.
- Build and Git palette commands remain local and should not be used for a
  remote project yet.

## Protocol and security

The helper accepts framed JSON RPCs only over stdin/stdout of the authenticated
SSH session. It intentionally applies the connected user's normal filesystem
permissions rather than adding another authorization layer. File saves use a
temporary file in the destination directory followed by a rename.

The current protocol is version 1. See [cwremote/README.md](cwremote/README.md)
for its method list and framing details.
