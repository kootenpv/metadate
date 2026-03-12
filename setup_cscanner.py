from setuptools import setup, Extension

setup(
    ext_modules=[
        Extension(
            "metadate._cscanner",
            sources=["metadate/_cscanner.c"],
            extra_compile_args=["-O2"],
        ),
    ],
)
