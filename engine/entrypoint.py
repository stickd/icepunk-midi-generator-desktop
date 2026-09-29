"""PyInstaller entry point; orchestration remains in the existing bridge."""
from generate import main

if __name__ == '__main__':
    raise SystemExit(main())
