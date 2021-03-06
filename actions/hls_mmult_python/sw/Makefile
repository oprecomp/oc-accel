#
# Copyright 2017 International Business Machines
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

#
# Generate HDL version of the HLS sources
#
# The generated HDL depends on the chip which is used and
# therefore must match what is being used to build the
# toplevel SNAP bitstream.
#
# FIXME Pass part_number and other parameters from toplevel
#      build-system as required.
#

# Finding $SNAP_ROOT
ifndef SNAP_ROOT
# check if we are in sw folder of an action (three directories below snap root)
ifneq ("$(wildcard ../../../actions)","")
SNAP_ROOT=$(abspath ../../../)
else
$(info You are not building your software from the default directory (/path/to/snap/actions/<action_name>/sw) or specified a wrong $$SNAP_ROOT.)
$(error Please make sure that $$SNAP_ROOT is set up correctly.)
endif
endif

# This is solution specific. Check if we can replace this by generics too.
LDLIBS += -lm

snap_mmult_python: ${snap_mmult_python_objs}

projs += snap_mmult_python

CXXFLAGS = -std=c++11 -fpermissive -DFLOATX_ENABLE
PyCFLAGS=$(shell python3-config --includes)
PyCFLAGS+=-fPIC -O2 -DPY_WRAP 
FLOATXDIR = ../../../transprecision/FloatX/src/
VERSION:=$(GIT_BRANCH)
PyCFLAGS += -DGIT_VERSION=\"$(VERSION)\" \


PREREQ_EXECUTABLES = ld swig
K := $(foreach exec,$(PREREQ_EXECUTABLES),\
        $(if $(shell which $(exec)),some string,$(error "No $(exec) in PATH")))

# Steps to wrap wih swig:
#
# 1. swig -python -c++ XXX.i
#
# 2. c++ -c -fpic XXX.cpp (this command will generate XXX.o file)
#
# 3. c++ -c -fpic XXX_wrap.cxx -I/usr/include/python2.7 (this command will generate XXX_wrap.o file)
#
# 4. c++ -shared XXX.o XXX_wrap.o -o XXX.so
#CPP=g++ 

pywrap:
	[ -f ./numpy.i ] && echo "numpy.i already here, good" || wget https://raw.githubusercontent.com/numpy/numpy/master/tools/swig/numpy.i
	swig -python -o snap_mmult_python_wrap.c  dieres.i
	$(CXX) $(CXXFLAGS) -c -fpic snap_mmult_python.c snap_mmult_python_wrap.c $(PyCFLAGS) -I$(FLOATXDIR) -I$(SNAP_ROOT)/software/include/ -I$(SNAP_ROOT)/actions/hls_mmult_python/include
	$(CXX) -shared snap_mmult_python.o snap_mmult_python_wrap.o $(SNAP_ROOT)/software/lib/libosnap.a $(OCSE_ROOT)/libocxl/libocxl.a -o _dieres.so

pywrap_clean:
	$(RM) -rf ./*_wrap* ./*.so ./*.o
#LD_LIBRARY_PATH=/tools/projects/ocse/libocxl/ python3 -c "import snap_mmult_python"

# If you have the host code outside of the default snap directory structure, 
# change to /path/to/snap/actions/software.mk
include $(SNAP_ROOT)/actions/software.mk
