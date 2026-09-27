import os
import subprocess
import sys


# Try to install the required packages
def install_package(package):
    subprocess.check_call([sys.executable, "-m", "pip", "install", package])


# Install intelhex
try:
    import intelhex

    print("intelhex module already installed")
except ImportError:
    print("Installing intelhex module...")
    install_package("intelhex")
    print("intelhex module installed successfully")
