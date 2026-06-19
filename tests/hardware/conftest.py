import pytest
from microdatawiggler import MicrodatawigglerClient, MicrodatawigglerLauncher


def pytest_addoption(parser):
    parser.addoption("--elf-path", default="firmware_build/simple.elf")


@pytest.fixture(scope="session")
def client(request):
    elf_path = request.config.getoption("--elf-path")
    with MicrodatawigglerLauncher():
        c = MicrodatawigglerClient(elf_path=elf_path)
        c.connect()
        yield c
        c.disconnect()
