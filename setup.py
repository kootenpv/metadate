"""Build script for the optional C-accelerated scanner."""

import platform
from setuptools import setup, Extension

compile_args = ["/O2"] if platform.system() == "Windows" else ["-O2"]

setup(
    ext_modules=[
        Extension(
            "metadate._cscanner",
            sources=["metadate/_cscanner.c"],
            extra_compile_args=compile_args,
        ),
    ],
)
