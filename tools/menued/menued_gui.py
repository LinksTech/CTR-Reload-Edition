"""PyInstaller entry point for the menu editor.

A script of its own instead of "python -m menued", because PyInstaller needs an
unambiguous entry point and not a package module. Contains no logic itself
- only calls menued.app.main.
"""
from menued.app import main

if __name__ == "__main__":
    main()
