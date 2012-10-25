import os
from shutil import copy2 as copy

srcdir = '.'
blddir = 'build'
VERSION = '0.0.1'

TARGET = 'dmx_native'
TARGET_FILE = '%s.node' % TARGET
dest = './%s' % TARGET_FILE

def set_options(opt):
  opt.tool_options('compiler_cxx')

def configure(conf):
  conf.check_tool('compiler_cxx')
  conf.check_tool('node_addon')
  
def build(bld):
  obj = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  obj.cxxflags = '-Wall'
  obj.rpath = '$ORIGIN'
  obj.target = TARGET
  obj.source = 'dmx.cc'
  obj.lib = 'ftd2xx'
  obj.libpath = ['.','..']

def shutdown():
  if os.path.exists('build/Release/%s' % TARGET_FILE):
    copy('build/Release/%s' % TARGET_FILE, dest)
  elif os.path.exists('build/default/%s' % TARGET_FILE):
    copy('build/default/%s' % TARGET_FILE, dest)