from setuptools import setup, Extension

execorder = Extension(
    'execorder',
    sources=['execorder.cpp', 'recording.cpp'],
    extra_compile_args=['/std:c++14'],
    py_limited_api=False,
)

setup(
    name='Execorder',
    version='1.0.0',
    description="Provides a new exec() function which records the execution",
    ext_modules=[execorder]
)
