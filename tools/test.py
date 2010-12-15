#!/usr/bin/env python

import subprocess
import platform
import time
import sys
import os

TEST_DIR = "regression"

CLASSPATH_DIR = os.popen('tools/classpath-config').read().strip()

NO_SYSTEM_CLASSLOADER = [ "-bootclasspath", "regression:" + CLASSPATH_DIR + "/share/classpath/glibj.zip", "-Djava.library.path=" + CLASSPATH_DIR + "/lib/classpath/", "-Xnosystemclassloader" ]

TESTS = [
  #                            Exit
  #  Test                      Code  Extra VM arguments       Architectures
  # ========================== ====  =======================  =============
  ( "jvm/EntryTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm/ExitStatusIsZeroTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm/ExitStatusIsOneTest", 1, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm/ArgsTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "java.lang.VMClassTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "java.lang.reflect.ClassTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.ArrayMemberTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.BranchTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.CFGCrashTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.ClassExceptionsTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.CloneTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.FibonacciTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.GcTortureTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.GetstaticPatchingTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.IntegerArithmeticExceptionsTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.InterfaceFieldInheritanceTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.InterfaceInheritanceTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.InvokeResultTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.InvokeTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.InvokeinterfaceTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.InvokestaticPatchingTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.LoadConstantsTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.MethodInvocationAndReturnTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.MethodInvocationExceptionsTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.NoSuchMethodErrorTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.ObjectArrayTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.ObjectCreationAndManipulationExceptionsTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.ObjectStackTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.ParameterPassingTest", 100, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.PopTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.RegisterAllocatorTortureTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.StringTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.SubroutineTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.SynchronizationExceptionsTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.TrampolineBackpatchingTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.VirtualAbstractInterfaceMethodTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "jvm.WideTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
, ( "sun.misc.UnsafeTest", 0, NO_SYSTEM_CLASSLOADER, [ "i386", "x86_64" ] )
]

verbose = False

def guess_arch():
  arch = platform.machine()
  if arch == "i686":
    return "i386"
  return arch

ARCH = guess_arch()

def is_test_supported(t):
  klass, expected_retval, extra_args, archs = t
  return ARCH in archs

def success(s):
  return "\033[32m" + s + "\033[0m"

def failure(s):
  return "\033[31m" + s + "\033[0m"

def run(program, *args):
  pid = os.fork()
  if not pid:
    os.execvp(program, (program,) +  args)
  return os.wait()[0]

def main():
  retval = passed = failed = 0
  start = time.time()
  for t in TESTS:
    klass, expected_retval, extra_args, archs = t
    if is_test_supported(t):
      command = ["./jato", "-cp", TEST_DIR ] + extra_args + [ klass ]
      if verbose:
        print command
      retval = subprocess.call(command)
      if retval != expected_retval:
        print klass + ": Test FAILED"
        failed += 1
      else:
        passed += 1

  end = time.time()

  elapsed = end - start

  status = ""
  if passed != 0 or failed != 0:
    if passed == 0:
      status += "No tests passed"
    elif passed == 1:
      status += "%d test passed" % passed
    else:
      status += "%d tests passed" % passed

    if failed == 1:
      status += ", %d test failed" % failed
    elif failed > 1:
      status += ", %d tests failed" % failed
  else:
    status += "No tests run"

  if failed > 0:
    status = failure(status)
  elif passed > 0:
    status = success(status)

  print "%s (%.2f s) " % (status, elapsed)
  sys.exit(retval)

if __name__ == '__main__':
  sys.exit(main())
