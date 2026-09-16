"""Pass the complete native CLI through without reimplementing its flags."""

import os
import sys

from ._api import native_binary


def main() -> None:
    binary = str(native_binary())
    os.execv(binary, [binary, *sys.argv[1:]])
