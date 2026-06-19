import subprocess
import time

import grpc

_STARTUP_POLL_INTERVAL = 0.1


class MicrodatawigglerLauncher:
    """Launches and manages the microdatawiggler server process.

    Usage as a context manager::

        with MicrodatawigglerLauncher() as launcher:
            client = MicrodatawigglerClient()
            ...

    Or manually::

        launcher = MicrodatawigglerLauncher()
        launcher.start()
        ...
        launcher.stop()
    """

    def __init__(
        self,
        port: int = 50051,
        executable: str = "microdatawiggler",
        startup_timeout: float = 10.0,
    ):
        self._port = port
        self._executable = executable
        self._startup_timeout = startup_timeout
        self._process: subprocess.Popen | None = None

    def start(self) -> "MicrodatawigglerLauncher":
        """Start the server and block until it accepts gRPC connections."""
        cmd = [self._executable, "--port", str(self._port)]
        self._process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            _wait_for_ready(self._process, self._port, self._startup_timeout)
        except Exception:
            self._process.kill()
            self._process.wait()
            self._process = None
            raise
        return self

    def stop(self) -> None:
        """Terminate the server process."""
        if self._process is not None:
            self._process.terminate()
            self._process.wait()
            self._process = None

    def __enter__(self) -> "MicrodatawigglerLauncher":
        return self.start()

    def __exit__(self, *args) -> None:
        self.stop()


def _wait_for_ready(process: subprocess.Popen, port: int, timeout: float) -> None:
    address = f"localhost:{port}"
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            stderr = process.stderr.read().decode(errors="replace")
            raise RuntimeError(
                f"Microdatawiggler process exited with code {process.returncode}: {stderr}"
            )
        try:
            channel = grpc.insecure_channel(address)
            grpc.channel_ready_future(channel).result(timeout=_STARTUP_POLL_INTERVAL)
            channel.close()
            return
        except grpc.FutureTimeoutError:
            continue
    raise RuntimeError(f"Microdatawiggler did not become ready within {timeout}s")
