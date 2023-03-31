//===- DeviceManager.h - qihai ----------------------------------*- C++ -*-===//
//
// Copyright (C) 2020-2022 Terapines Technology (Wuhan) Co., Ltd
// All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// \DeviceManager.h
// This file contains the declaration of the DeviceManager class, which is
// the manager of all devices and drivers. This class has the same function
// as struct devclass in FreeBSD.
//
//===----------------------------------------------------------------------===//

#ifndef _DEVICE_MANAGER_H_
#define _DEVICE_MANAGER_H_

#include <Device.h>

#define DM_HAS_CHILDREN   1

class DeviceManager : public KernelObject {
public:
  DeviceManager() = default;
  virtual ~DeviceManager() = default;

private:
  Kernel::STL::string Name;
  DeviceManager* Parent;
  int MaxUnit;  // size of devices list.
  int Flags;
  std::forward_list<Driver*, KernelAllocator<Driver*>> Drivers;
  std::vector<Device*, KernelAllocator<Device*>> Devices;

  struct sysctl_ctx_list SysctlCtx;
  struct sysctl_oid *SysctlTree;
};

// 全局 DeviceManager list
std::forward_list<DeviceManager*, KernelAllocator<DeviceManager*>> DeviceManagerList;

#endif  // _DEVICE_MANAGER_H_


class Driver foo {
// 把现有的驱动的功能函数做成类成员函数
};

Driver foo;

// 宏定义 DRIVER_HANDLER(..., foo, ...)，把驱动程序注册到全局链表 DeviceManagerList
// 该宏需要关联 SYSINIT(.., SI_SUB_DRIVERS ,..)