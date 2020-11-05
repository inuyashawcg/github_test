/*
 * This file is produced automatically.
 * Do not modify anything in here by hand.
 *
 * Created from source file
 *   /usr/src/12/sys/dev/virtio/virtio_if.m
 * with
 *   makeobjops.awk
 *
 * See the source file for legal information
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/bus.h>
#include "virtio_if.h"


static int
virtio_default_attach_completed(device_t dev)
{
	return (0);
}

struct kobjop_desc virtio_attach_completed_desc = {
	0, { &virtio_attach_completed_desc, (kobjop_t)virtio_default_attach_completed }
};


static int
virtio_default_config_change(device_t dev)
{
	return (0);
}

struct kobjop_desc virtio_config_change_desc = {
	0, { &virtio_config_change_desc, (kobjop_t)virtio_default_config_change }
};

