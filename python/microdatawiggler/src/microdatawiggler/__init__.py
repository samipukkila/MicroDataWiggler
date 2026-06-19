__all__ = ["MicrodatawigglerClient", "MicrodatawigglerLauncher", "ElfSymbolResolver", "Symbol"]


def __getattr__(name: str):
    """Lazy load exports to avoid circular imports."""
    if name == "MicrodatawigglerClient":
        from ._client import MicrodatawigglerClient
        return MicrodatawigglerClient
    if name == "MicrodatawigglerLauncher":
        from ._process import MicrodatawigglerLauncher
        return MicrodatawigglerLauncher
    if name == "ElfSymbolResolver":
        from ._elf import ElfSymbolResolver
        return ElfSymbolResolver
    if name == "Symbol":
        from ._elf import Symbol
        return Symbol
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
