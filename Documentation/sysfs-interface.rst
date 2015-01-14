New sysfs Interface Definition
==============================

:Author: Nicolas Trangez
:Contact: nicolas.trangez@scality.com
:Organization: Scality SA
:Status: Work in progress

:abstract:

    Proposal for reworking the srb_ *sysfs* interface, providing new features
    or streamlining existing settings.

    .. _srb: https://github.com/scality/RestBlockDriver

Goal
----
The current *sysfs* interface to manage *srb* is not very flexible. A major
drawback is the global pool of CDMI server addresses it maintains: there's no
way to assign specific servers to specific volume bindings. Furthermore, the
current implementation assumes all registered CDMI servers point at the exact
same storage. In a Scality environment, this implies it's not possible to have
multiple volume bindings using different backing RINGs.

Next to lacking features, the interface is also somewhat inconsistent, e.g. the
*volumes* listing exposes a file containing one volume name per line, whilst
*urls* uses comma-separated formatting.

Finally, one could question the need for some features, e.g. the volume listing
pointed at before. This can easily be done from user-space, there's no need to
add code to implement this to the module. The same applies for volume creation,
extension (from a CDMI point-of-view), or deletion.

Proposed Interface
------------------
The configuration of *srb* consists of two categories: *global* configuration,
and per-volume settings.

To create and configure a device, the following steps should be taken:

- Create a new named *srb* device
- Configure the device, except for its backing URLs
  At this point the device node exists, but the device size equals 0, and can't
  be opened
- Set up the device URL list
  At this point, the device size (and other properties, if applicable) are
  updated, and the device can be opened

To release a device, its attach count must be 0, and the corresponding call on
the *sysfs* interface of the driver can be triggered.

Global Settings and Actions
+++++++++++++++++++++++++++
All paths are relative to where *sysfs* is mounted.

bus/srb/debug
~~~~~~~~~~~~~
Driver debug level, readable and writable. Can be set at module loading time.
Defaults to the numeric representation of `INFO`.

bus/srb/create
~~~~~~~~~~~~~~
Create a new named *srb* device by writing to this entry. The format is

::

    name option-list

where

  name
    A name for the device node

  option-list
    A list of options, separated by commas.

Options are

  max_hw_sectors_kb=N
    The `max_hw_sectors_kb` value of the device (rounded up to match an
    integer number of sectors by the driver). When not provided, the default as
    chosen by the kernel will be used.

An example could be

::

    $ echo "myvolume max_hw_sectors_kb=$(( 32 * 1024 * 1024))" > create


The following errors can be returned:

  *EINVAL*
    Unable to parse the provided values

  *EEXIST*
    A device with the given name already exists

  *ENAMETOOLONG*
    The provided name is too long to be a valid device name

Note a device is not ready for use after creation: it will only active after
setting at least its list of backing URLs.

bus/srb/destroy
~~~~~~~~~~~~~~~
To destroy an *srb* device, the name of the device should be written to this
node. The following errors can be returned:

  *EBUSY*
    The device is currently in use

  *ENODEV*
    No such device

  *EINVAL*
    Invalid input

Example usage::

    $ echo myvolume > destroy

bus/srb/volumes
~~~~~~~~~~~~~~~
This read-only node lists all existing *srb* devices. The names are separated by
a newline.

Example usage::

    $ cat volumes
    myvolume
    hisvolume
    hervolume

Volume Settings and Actions
+++++++++++++++++++++++++++
Several settings and actions are provided on *srb* devices. These are exposed in
the *srb* directory under the device entry in *class/block*.

class/block/<name>/srb/debug
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
This is a readable and writable setting, specifying the debug level for messages
originating from the device handler functions. Defaults to the numeric
representation of `WARNING`.

The following errors can be returned:

  *EINVAL*
    Unable to parse value, or invalid value

class/block/<name>/srb/max_thread_pool_size
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
This is a readable and writable setting, specifying the size of the thread pool
used by the volume. Defaults to 8.

The following errors can be returned:

  *EINVAL*
    Unable to parse value, or invalid value

class/block/<name>/srb/urls
~~~~~~~~~~~~~~~~~~~~~~~~~~~
This setting is both readable and writable. When read, it returns the current
value in the same format as expected when written to.

After writing a list of URLs to this device for the first time, the device will
be usable (can be opened).

It is possible to write an empty string to this setting, which removes all
backing URLs from the device. This is *only* possible when the device is not in
use. When no backing URLs are present, the device can't be opened.

URLs should be provided in the following format::

    cdmi+http://169.254.0.1:8080/container/myvolume

Other formats could be added in the future. URLs are separated by a newline. The
path portion of the URL should be properly escaped.

To remove a URL, the whole list must be rewritten (except for the URL to be
removed, obviously).

The following errors can be returned:

  *EINVAL*
    Unable to parse value, or invalid value

  *EBUSY*
    The device is currently in use and empty list of URLs is provided.

class/block/<name>/srb/connections
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
This read-only entry lists all current connections in the following format::

    <TID> <URL> <local IP>:<local port> <RX> <TX>

where the fields corresponds to the following values:

  TID
    The thread ID managing the connection

  URL
    The URL used with this connection. This contains the remote IP and port.

  local IP
    Local IP address of the connection

  local port
    Local port of the connection

  RX
    Bytes received through the connection (including headers)

  TX
    Bytes transmitted through the connection (including requests, headers,...)
