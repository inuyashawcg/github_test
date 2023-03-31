//===- Device.h - qihai ----------------------------------------*- C++ -*-===//
//
// Copyright (C) 2020-2022 Terapines Technology (Wuhan) Co., Ltd
// All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// \Device.h
// This file contains the declaration of the Device class, which is
// the base class of all different types of device.
//
//===----------------------------------------------------------------------===//

#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/domainset.h>
#include <sys/eventhandler.h>
#include <sys/filio.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/poll.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/condvar.h>
#include <sys/queue.h>
#include <machine/bus.h>
#include <sys/random.h>
#include <sys/rman.h>
#include <sys/sbuf.h>
#include <sys/selinfo.h>
#include <sys/signalvar.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/cpuset.h>

#include <net/vnet.h>

#include <machine/cpu.h>
#include <machine/stdarg.h>

#include <vm/uma.h>
#include <vm/vm.h>

#include <ddb/ddb.h>
#include <sysctlstatus/Sysctl.h>
#include <sysctlstatus/SysctlDebug.h>
#include <vm/KernelAllocator.h>
#include <gr/KernelSTL.h>
#include <kernel/KernelObject.h>

#define DRI_DEFERRED_PROBE 1

class Device : public KernelObject {
public:
  Device() = default;
  virtual ~Device() = default;

private:
  Kernel::STL::string Name;
  size_t Size;
  u_int Refs;
  Device* Parent;
  Driver* CurrentDriver;
};

class Driver : public KernelObject {
public:
  virtual int identify(Device* Dev);
  virtual int probe(Device* Dev);
  virtual int attach(Device* Dev);
  virtual int detach(Device* Dev);
  virtual int shutdown(Device* Dev);
  virtual int suspend(Device* Dev);
  virtual int resume(Device* Dev);

protected:
  int Pass;
  int Flags;
  u_int Refs;
  Kernel::STL::string Name;
};

class BusDriver : public Driver {

};

#endif  // _DEVICE_H_