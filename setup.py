from setuptools import setup, Extension

execorder = Extension(
    'execorder',
    sources=['execorder.cpp', 'ceval.cpp', 'recording.cpp'],
    extra_compile_args=['-std=c++11', '-O3'],
    include_dirs=["include"],
    py_limited_api=False
)

setup(
    name='Execorder',
    version='0.1.0',
    description="Provides a new exec() function which records the execution",
    ext_modules=[execorder]
)