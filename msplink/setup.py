#!/usr/bin/env python3
# encoding: utf-8

from distutils.core import setup, Extension

msplink_module = Extension('msplink', sources =
    ['msplink.c',
     'parse.c',
     'send.c',
     'serial.c',
     'checksums.c'])

setup(name='msplink',
      version='0.1.0',
      description='A thread-safe Multi-Wii Serial Protocol (MSP) protocol driver',
      author='Daniel Hooper',
      url='https://github.com/hooper-engineering/python-msptools',
      ext_modules=[msplink_module])
