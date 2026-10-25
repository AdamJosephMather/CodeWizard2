#pragma once

// Kept in the executable so installed builds need no helper-file lookup.
// stdout is a JSON-lines protocol; notebook output travels over Jupyter IOPub.
inline constexpr const char* jupyterBridge = R"PY(
import sys, json, threading, queue, os, time
def emit(**message):
    print(json.dumps(message, ensure_ascii=True), flush=True)
try:
    from jupyter_client import KernelManager
    from jupyter_client.kernelspec import KernelSpecManager
    specs = KernelSpecManager().get_all_specs()
    emit(event='kernels', names=[name for name, spec in specs.items() if spec['spec'].get('language', '').lower() == 'python'])
except Exception as error:
    emit(event='fatal', message='Jupyter support unavailable: ' + str(error) + '. Install Python packages: python -m pip install jupyter_client ipykernel')
    sys.exit(1)
jobs = queue.Queue()
manager = None
client = None
lock = threading.RLock()
def stop():
    global manager, client
    with lock:
        if manager is not None:
            try:
                manager.shutdown_kernel(now=True)
            except Exception:
                pass
        if client is not None:
            client.stop_channels()
        manager = client = None
def worker():
    global manager, client
    while True:
        request = jobs.get()
        cell = request['cell']
        try:
            with lock:
                if manager is None:
                    manager = KernelManager(kernel_name=request['kernel'])
                    manager.start_kernel(cwd=request['cwd'])
                    client = manager.client()
                    client.start_channels()
                    client.wait_for_ready(timeout=30)
                kc = client
                km = manager
            msg_id = kc.execute(request['code'], allow_stdin=False, stop_on_error=True)
            # Bound captured output per execution, including clear/display loops.
            # Keep draining IOPub after truncation so interrupts and idle arrive.
            remaining_bytes, remaining_lines, remaining_events = 262144, 2000, 256
            truncated = False
            stream_name, stream_text = None, ''
            last_flush = time.monotonic()
            def flush_stream():
                nonlocal stream_text, last_flush
                if stream_text:
                    emit(event='output', cell=cell, kind='stream', content=dict(name=stream_name, text=stream_text))
                    stream_text = ''
                last_flush = time.monotonic()
            def truncate():
                nonlocal truncated
                flush_stream()
                if not truncated:
                    emit(event='output', cell=cell, kind='stream', content=dict(name='stderr', text='\n[Output truncated; interrupt the cell to stop execution.]\n'))
                    truncated = True
            while True:
                try:
                    message = kc.get_iopub_msg(timeout=1)
                except queue.Empty:
                    flush_stream()
                    if not km.is_alive():
                        raise RuntimeError('Kernel exited. Restart the kernel to continue.')
                    continue
                if message.get('parent_header', {}).get('msg_id') != msg_id:
                    continue
                kind = message['header']['msg_type']
                content = message['content']
                if kind == 'status' and content['execution_state'] == 'idle':
                    flush_stream()
                    break
                if kind == 'stream':
                    if truncated:
                        continue
                    remaining_events -= 1
                    text = content.get('text', '')
                    encoded = text.encode('utf-8')
                    accepted = encoded[:remaining_bytes].decode('utf-8', errors='ignore')
                    lines = accepted.splitlines(keepends=True)
                    accepted = ''.join(lines[:remaining_lines])
                    if stream_name != content.get('name', 'stdout'):
                        flush_stream()
                        stream_name = content.get('name', 'stdout')
                    stream_text += accepted
                    remaining_bytes -= len(accepted.encode('utf-8'))
                    remaining_lines -= accepted.count('\n')
                    if len(stream_text) >= 16384 or time.monotonic() - last_flush >= 0.05:
                        flush_stream()
                    if accepted != text or remaining_bytes <= 0 or remaining_lines <= 0 or remaining_events <= 0:
                        truncate()
                elif kind in ('execute_result', 'display_data', 'update_display_data', 'error', 'clear_output', 'execute_input'):
                    flush_stream()
                    # Execution counts and failures are lifecycle information.
                    if kind not in ('execute_input', 'error'):
                        size = len(json.dumps(content).encode('utf-8'))
                        if truncated or remaining_events <= 0 or size > remaining_bytes:
                            truncate()
                            continue
                        remaining_bytes -= size
                        remaining_events -= 1
                    if kind == 'error':
                        content['traceback'] = [line[:4096] for line in content.get('traceback', [])[:32]]
                        content['evalue'] = content.get('evalue', '')[:4096]
                    emit(event='output', cell=cell, kind=kind, content=content)
            emit(event='done', cell=cell)
        except Exception as error:
            emit(event='error', cell=cell, message=str(error))
            stop()
threading.Thread(target=worker, daemon=True).start()
try:
    for line in sys.stdin:
        request = json.loads(line)
        command = request['command']
        if command == 'execute':
            jobs.put(request)
        elif command == 'interrupt':
            with lock:
                if manager is not None:
                    manager.interrupt_kernel()
        elif command == 'shutdown':
            break
finally:
    stop()
)PY";
