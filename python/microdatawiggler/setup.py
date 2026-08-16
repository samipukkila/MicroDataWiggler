import os
from pathlib import Path
from setuptools import setup
from setuptools.command.build_py import build_py

PROTO_DIR = Path(__file__).resolve().parent.parent.parent / "microdatawiggler" / "proto"
OUT_DIR = Path(__file__).resolve().parent / "src" / "microdatawiggler"
VERSION_FILE = Path(__file__).resolve().parent.parent.parent / "VERSION"


def read_version():
    return os.environ.get("PACKAGE_VERSION") or VERSION_FILE.read_text().strip()


def generate_proto():
    from grpc_tools import protoc
    ret = protoc.main([
        "grpc_tools.protoc",
        f"--proto_path={PROTO_DIR}",
        f"--python_out={OUT_DIR}",
        f"--grpc_python_out={OUT_DIR}",
        str(PROTO_DIR / "microdatawiggler.proto"),
    ])
    if ret != 0:
        raise SystemExit(f"protoc failed with code {ret}")
    grpc_stub = OUT_DIR / "microdatawiggler_pb2_grpc.py"
    text = grpc_stub.read_text()
    grpc_stub.write_text(text.replace(
        "import microdatawiggler_pb2 as microdatawiggler__pb2",
        "from . import microdatawiggler_pb2 as microdatawiggler__pb2",
    ))


class BuildPyWithProto(build_py):
    def run(self):
        generate_proto()
        super().run()


if __name__ == "__main__":
    setup(cmdclass={"build_py": BuildPyWithProto}, version=read_version())
