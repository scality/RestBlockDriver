INTRODUCTION
============

This is a native Linux Kernel Driver aiming at providing a simple way for
REST-based storage to provide volumes and attach them as native linux block
devices, thus taking advantage from Linux's efficient Block Device cache.

A secondary aim is also to help automate management of volumes from the client
machine.

A lot of features are planned, as you can see by looking at the current issues.
You're welcome to report bugs and propose ideas for new features through
github's issue tracker; as well as providing patches through pull requests.

INSTRUCTIONS
=============

Prerequisites
---------

Ubuntu 12.04.4 LTS or CentOS 7 system supported

Minimal Linux kernel version: v3.10

Installing the driver
---------------------

Installing the driver is relatively simple, as it's essentially done by
loading the module into the linux kernel.

Here is how it's done:

    # insmod dewblock.ko

Now, the Rest Block Driver is set for use, and you only need to know how to
control the driver to do the management tasks. To learn that, please continue
reading.

In order to set the driver's parameters, you can add those to the loading
command line of the driver as follows:

    # insmod dewblock.ko thread_pool_size=16

The following parameters are available:
  * debug: log level for the LKM (integer number, 0 to 7: emergency,
                                  alert, critical, error, warning,
                                  notice, info, debug)
  * req_timeout: timeout for requests
  * nb_req_retries: number of retries before aborting a Request
  * mirror_conn_timeout: timeout for connecting to a mirror
  * thread_pool_size: size of the thread pool of each device

Volume provisionning
====================

Currently, the driver does not yet support failover between multiple mirrors
providing the same repository of volumes, but it is nonetheless a feature that
we are aiming for. For this reason, we chose to provide a facility to manage
the mirrors the driver is associated to, and then the usual operations will
operate one one of those mirrors.

For this reason we provide you with three /sys files controlling the mirrors:
 * mirrors: allows listing the mirrors currently available/configured
 * add\_mirrors: allows adding one or more mirrors to the driver
 * remove\_mirrors: allows removing one or more mirrors from the driver

Then, the following management files are available:
 * create: allows creating a volume file on the storage
 * extend: allows extending a volume (increasing size), whether it is attached
 or not
 * destroy: deletes the volume file from the storage

The way those files work is described in the following sections, each dedicated
to one management (/sys) file. Please mind that each one of theses files can be
displayed (using cat on them) to show a simple usage text.


Listing the mirrors
-------------------

To list the mirrors currently configured within the driver, you can simply
display the contents of the mirrors file:

    # cat /sys/class/dewb/mirrors

This file displays the list of mirrors separated by a coma, using the same
format you would to add or remove one or more mirrors.


Adding mirrors
--------------

To add one (or more) new mirror(s) to the driver, you need to write the list of
mirrors separated by commas into the associated file. The format includes
protocol, host (IP only: DNS is not supported yet), optional port, and the
path to the volume repository ("path") which does no require an ending '/'.
In essence, a volume repository URL would look like this:

    http://<ip>[:<port>]/<path>

Thus, to concatenate the multiples mirror urls, you can add them all at once like in
the example:

    # export REST_REPO1=http://127.0.0.1:443/volumes
    # export REST_REPO2=http://192.168.0.3/repository/
    # echo "$REST_REPO1,$REST_REPO2" > /sys/class/dewb/add_mirrors

The driver will properly separate all repositories from the string you gave it,
and add them one by one. In case of error, only the error-yielding mirror will
not be added to the mirror list. All valid mirrors that did not yield any error
will be properly added. You might want to check which ones could be added by
listing the mirrors if you cannot add all your mirrors.

Be careful, though:
 * Every mirror must point to the same volume repository. Doing otherwise is
an unsupported use, and behavior is undefined and untested
 * /!\ Currently, the failover not being supported, not all mirrors might
be actually used.


Removing mirrors
----------------

In order to remove one or more mirrors, the same format is used as for adding
mirrors. Also, since the listing of mirrors outputs them this way, you could
copy and paste part of the mirrors listing if you wished to. In the end,
removing mirrors can be done as follows:

    # echo "http://127.0.0.1:443/volumes" > /sys/class/dewb/remove_mirrors

Please note that if a device is attached, you will not be able to remove the
last mirror. You need to detach manually every device attached by the module
before manually removing the last mirror.

Note also that when unloading the module, the devices are detached
automatically before the module can actually be unloaded.


Creating a new volume
---------------------

To create a volume, just give the name of the file to create to the driver,
accompanied by a byte size, such as in the following example:

    # echo "filename human\_readable\_size" > /sys/class/dewb/create

The volume will be created on the storage with the requested size.
But beware:
  * The human readable size formats understood by the LKM are the following:
    * [integer number]: size in bytes
    * [integer number]k: size in Kilobytes
    * [integer number]M: size in Megabytes
    * [integer number]G: size in Gigabytes
  * At least one mirror must exist and be valid for the volume to be created
  * Created volume are not automatically attached as a device on the system.

Before you can use the volume, you have to attach it through the attach /sys
file (see below).

Extending a volume
------------------

Sometimes, a volume might look to be provisionned too small for the actual
need. For this reason, you can actually extend it through the extend /sys
control file. This command follows the same usage as the create command, thus
using it is simple:

    # echo "volumename human\_readable\_size" > /sys/class/dewb/extend

Be aware that this command can only extend a volume, meaning the size you give
there must be higher than the current size. Also, this is a supported operation
on an attached volume, though any file-system formatted onto the volume should
be extended to the new volume's size manually since most of the filesystems
don't support flexible partition or volume extension (unless you are using LVM
underneath).

Once the operation is complete, the new size will be properly reported to the
system without any additional administrative task. For instance, displaying the
contents of the file /proc/partitions will show you the updated size of the
volume.

Note that the humand readable size format of the extend command follows the
same rules as that of the create command.

Destruction of an existing volume
---------------------------------

Destroying a volume means that it volume will no longer be accessible after
a successful operation. To destroy a volume, give the driver the name of the
file to remove from the storage as the following example shows:

    # echo filename > /sys/class/dewb/destroy

The volume is then removed from storage and is no longer accessible.
But beware:
  * The destroyed volume must exist beforehand
  * Destroying a volume used by other drivers on other machines dewblock
can lead to errors and unexpected behaviors; this is untested.


Attaching and Detaching devices
===============================

For multiple reasons, the driver does not attach automatically the devices when
adding a mirror or creating a new volume. Those reasons include:
  * automatization does not always gain from having generated device names
  * it's sometimes more a pain to synchronize with a generated name than define
it yourself

For those reason, you need to attach the volumes manually to the system, using
the three following management files are available:
 * volumes: Reading the file lists the volumes available on the mirrors
 * attach: Attaches an already provisionned volume as a device
 * detach: Detaches an attached device from the system
(does not delete the volume)

They are described in detail in the following sections.

Listing the volumes
-------------------

Since you might not know from memory which volumes exist on your mirrors,
you might want a way to list those, to attach them easily. One of the ways
provided is to read the content of the volumes /sys file:

 # cat /sys/class/dewb/volumes
 Volume1
 Foo
 Bar
 Baz
 Qux

This way, you can know that you have five volumes available on your mirror,
and know their names, which will allow you to either attach, extend or destroy
them.

Attaching a device
------------------

In order to attach an existing Volume file in the system, you simply need to
write the name of the Volume to the attach control file, followed by the name
of the device you want to appear, as the example states:

    # echo VolumeName DeviceName > /sys/class/dewb/attach

Then, a device named "DeviceName" is created in /dev. You can now use your
device as you wish, be it by writing and reading data directly to it,
creating a file system or even using LVM on top of it. 

Detaching a device
------------------

A device attached may be detached by writing the device's name into the detach
control file as the example shows:

    # echo DeviceName > /sys/class/dewb/detach

The DeviceName is the same Device Name used as the one used for Attach
operations.


Using the devices
=================

Partitioning a device
---------------------

The devices can be partitioned as conventional disks
for instance:

    # fdisk /dev/DeviceName

Then, you use it as any other device: each partition will appear with the same
name as the device itself with a number suffix.

sysfs interface
---------------

For each device, an entry is created in /sys/block
  * /sys/block/MySmallDevice for the volume attached as 'MySmallDevice'
  * /sys/block/dewba for ithe volume attached as 'dewba'
And so on...


Log & Debug
-----------

Logs are enable in the Linux Kernel Module and default is set to INFO. In order
to change the log level of the driver you can do it while loading it as follow:

    # insmod dewblock.ko dewb_log=3

The log level can also be changed using sysfs as follow:

    # echo 3 > /sys/module/dewblock/parameters/debug

Each device inherit the Linux Kernel Module log level. The log level of a device
can be changed as follow:

    # echo 6 > /sys/block/dewb?/dewb\_debug

The log level can be set from debug(7), info(6) ... to emergency (0).

Get information on the device
----------------------------------

The URL associated on CDMI (one mirror only):

    # cat /sys/block/dewb?/dewb\_urls

disk size:

    # cat /sys/block/dewb?/dewb\_size

volume name:

    # cat /sys/block/dewb?/dewb\_name


Tools
=====

Playground Server
-----------------

In the playground directory, you will find a minimalistic REST server written
in python that will allow you to try out the features of the Scality Rest Block
Driver. It was written to support Scality's REST protocol's mandatory semantics.

The Playground Server uses the filesystem to store its data, so using it too
extensively might fill your disk up. By default, the server stores the volumes
in the 'playground\_data' directory, within the directory you started the
server from; and listens on the port 80 (meaning that you might have to start
it as root). By using the options '--port' and '--datapath', you can change
either the port it listens on, or the directory where the volumes are stored.

Please keep in mind that as it is a minimal server script, it is not designed
for performance, but for functional testing mostly.


Remains to be done :
--------------------

  * Fault tolerance when more server (reset or timeout).
  * Optimize sector sizes.
  * Put the device in non-rotational
  * Support DKMS
  * Rollback if connection lost
