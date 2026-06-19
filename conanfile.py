from conan import ConanFile
from conan.tools.cmake import cmake_layout


class MicrodatawigglerRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    def build_requirements(self):
        self.tool_requires("cmake/4.2.3")
        self.test_requires("gtest/1.17.0")
        self.test_requires("fff/1.1")

    def layout(self):
        cmake_layout(self)
