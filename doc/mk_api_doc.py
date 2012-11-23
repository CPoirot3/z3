import os
import shutil
import re
import pydoc
import sys
import subprocess

def mk_dir(d):
    if not os.path.exists(d):
        os.makedirs(d)

# Eliminate def_API and extra_API directives from file 'inf'.
# The result is stored in 'outf'.
def cleanup_API(inf, outf):
    pat1  = re.compile(".*def_API.*")
    pat2  = re.compile(".*extra_API.*")
    _inf  = open(inf, 'r')
    _outf = open(outf, 'w') 
    for line in _inf:
        if not pat1.match(line) and not pat2.match(line):
            _outf.write(line)

try:
    mk_dir('api/html')
    cleanup_API('../src/api/z3_api.h', 'z3_api.h')
    print "Removed annotations from z3_api.h."
    DEVNULL = open(os.devnull, 'wb')
    try:
        if subprocess.call(['doxygen', 'z3api.dox'], stdout=DEVNULL, stderr=DEVNULL) != 0:
            print "ERROR: doxygen returned nonzero return code"
            exit(1)
    except:
        print "ERROR: failed to execute 'doxygen', make sure doxygen (http://www.doxygen.org) is available in your system."
        exit(1)
    print "Generated C and .NET API documentation."
    os.remove('z3_api.h')
    print "Removed temporary file z3_api.h."
    sys.path.append('../src/api/python')
    pydoc.writedoc('z3')
    shutil.move('z3.html', 'api/html/z3.html')
    print "Generated Python documentation."
    print "Documentation was successfully generated at subdirectory './api/html'."
except:
    print "ERROR: failed to generate documentation"
    exit(1)