from setuptools import setup, Extension

execorder = Extension(
    'execorder',
    sources=['execorder.cpp', 'recording.cpp', 'ceval.c'],
    extra_compile_args=['/std:c++14'],
    #extra_link_args=['/WHOLEARCHIVE:python37.lib'],
    include_dirs=['include', 'include_py'], 
    libraries=['python37', 'python3stub'],
    py_limited_api=False,
)

setup(
    name='Execorder',
    version='0.2.3',
    description="Provides a new exec() function which records the execution",
    ext_modules=[execorder]
)
