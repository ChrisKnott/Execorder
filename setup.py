from setuptools import setup, Extension

execorder = Extension(
    'execorder',
    sources=['execorder.cpp', 'ceval.c', 'recording.cpp'],
    extra_compile_args=['/std:c++14'],
    include_dirs=['include', 'include_py'], #, 'include/internal', 'win32'],
    py_limited_api=False,
    language='c++'
)

setup(
    name='Execorder',
    version='0.2.3',
    description="Provides a new exec() function which records the execution",
    ext_modules=[execorder]
)
