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

Ubuntu 12.04.4 LTS system supported

Minimal Linux kernel version: v3.8

Installing the driver
---------------------

Two things are required to install the driver properly:
 * Setting up the udev configuration file
 * inserting the module in linux's kernel

First, to add our udev configuration file, you need to copy the file
udev/50-restblock.rules into the /etc/udev/rules.d directory, and then
reload the udev rules:

    # cp udev/50-restblock.rules /etc/udev/rules.d/
    # udevadm control --reload-rules

The reload of the rules through udevadm is only necessary to avoid rebooting.
At the next boot, the file shall be loaded automatically, and you will only
need to load the kernel module manually if you didn't setup the module for
automated loading at boot-time. Those rules will be used by udev to create
symlinks from the devices and partitions in /dev into /dev/dewb/<VolumeName>/,
for ease of use.

Then the next and last step is to load the kernel module into the linux kernel:
  # insmod dewb

Now, your Rest Block Driver is set for use, and you only need to know how to
control the driver to do the management tasks. To learn that, please continue
the reading of this document.

Semi-automatic provisionning
============================

Currently, the driver does not yet support failover between multiple mirrors
providing the same repository of volumes, but it is nonetheless a feature that
we are aiming for. For this reason, we chose to provide a facility to manage
the mirrors the driver is associated to, and then the usual operations will
operate one one of those mirrors.

Also, the driver automatically attaches the existing volumes when adding a new
mirror, and will automatically detach the attached volumes when removing the
last associated mirror (or unloading the driver).

For this reason we provide you with three /sys files controlling the mirrors:
 * mirrors: allows listing the mirrors currently available/configured
 * add\_mirrors: allows adding one or more mirrors to the driver
 * remove\_mirrors: allows removing one or more mirrors from the driver

Then, the following management files are available:
 * create: allows creating a volume file on the storage
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
mirrors separated by comas into the associated file. The format includes
protocol, host (IP only: DNS is not supported yet), optional port, and the
path to the volume repository ("<path>") which does no require an ending '/'.
In essence, a volume repository URL would look like this:

    http://<ip>[:<port>]/<path>

Thus, catenating the multiples mirror urls, you can add them all at once like in
the example:

    # export REST_REPO1=http://127.0.0.1:443/volumes
    # export REST_REPO2=http://192.168.0.3/repository/
    # echo "$REST_REPO1,$REST_REPO2" > /sys/class/dewb/add_mirrors

The driver will properly separate all repositories from the string you gave it,
and add them one by one. In case of error, only the error-yielding mirror will
not be added to the mirror list. All valid mirrors that did not yield any error
will be properly added. You might want to check which ones could be added by
listing the mirrors if you cannot add all your mirrors.

Also, the volumes present in the repository of the first mirror added will be
automatically attached as devices by the driver. In case some devices might
fail to attach, the operation isn't atomic and the driver will try to attach
all volumes anyways. Any failure might just lead to some missing attached
volumes. To fix that, please refer to the debug/rescue /sys files "attach" and
"detach"

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

Please note that when removing the last mirror, every attached devices will be
detached. If this cannot be done, the removal will fail, and it will be up to
you to unblock the situation manually (umount mounted partitions before
retrying, for instance).


Creating a new volume
---------------------

To create a volume, just give the name of the file to create to the driver,
accompanied by a byte size, such as in the following example:

    # echo "filename filesize\_in\_bytes" > /sys/class/dewb/create

The volume will be created on the storage with the requested size.
But beware:
  * There is currently no easy format to give a Kilo/Mega/Giga byte size
  * At least one mirror must exist and be valid for the volume to be created
  * Create volume automatically attaches the device for the system to use
(a wild /dev/dewb\* appears).

It is now possible to use the volume using the associated /dev device file.
When you'll tire of it, you will be able to destroy the volume, effectively
deleting the file from the storage.

Destruction of an existing volume
---------------------------------

Destroying a volume means that it volume will no longer be accessible after
a successful operation. To destroy a volume, give the driver the name of the
file to remove from the storage as the following example shows:

    # echo filename > /sys/class/dewb/destroy

The volume is then removed from storage and is no longer accessible.
But beware:
  * The destroyed volume (if attached to the system) will be automatically
removed.
  * The destroyed volume must exist beforehand
  * Destroying a volume used by other drivers on other machines dewblock
can lead to errors and unexpected behaviors; this is untested.


Debug or manual provisionning
=============================

The driver currently does not support automatic detection of new volumes on the
storage. For this reason, if you happen to create a file through another mean
that the one provided through this driver, you might need to attach it manually
to the system. It can also be useful in case of automatic attach/detach failure
to operate on the volume.

For this reason, two last management files are available:
 * attach: Attached an already provisionned volume as a device
 * detach: Detaches an attached device from the system
(does not delete the volume)

They are described in detail in the following sections.


Attaching a new device
-------------------

In order to attach an existing Volume file in the system (if it could not be
done automatically), you simply need to write the name of the Volume to the
attach control file, as the example indicates:

    # echo VolumeName > /sys/class/dewb/attach

Then, a device is created in /dev:
  * /dev/dewba for the first attach operation
  * /dev/dewbb for the 2nd
And so on...

The symlink created by udev (using our rules) uses the name of the volume file,
so that attaching a volume named "Disk1" will create the following additional
symlink:
 * /dev/dewb/Disk1/device

Detaching a device
-----------------

A device attached may be detached by writing the volume's name into the detach
control file as the example shows:

    # echo VolumeName > /sys/class/dewb/detach

The VolumeName is the same Volume Name used for Create/Destroy as well
as Attach operations.


Using the devices
=================

Partitioning a device
---------------------

The devices can be partitioned as conventional disks
for instance:

    # fdisk /dev/dewba

Once partitioned, the associated files in /dev are created, for instance for a
volume named "TestVolume", mapped on the device "dewba":
  * /dev/dewba1 also symlinked to /dev/dewb/TestVolume/part-1
  * /dev/dewba2 also symlinked to /dev/dewb/TestVolume/part-2
And so on...

sysfs interface
---------------

For each device, an entry is created in /sys/block
  * /sys/block/dewba for dewba
  * /sys/block/dewbb for dewbb
And so on...


Debugging
---------

It is possible to enable/disable debug trace with the following command:

activate:

    # echo 1 > /sys/block/dewb?/dewb\_debug

disable:

    # echo 0 > /sys/block/dewb?/dewb\_debug


Get information on the device
----------------------------------

The URL associated CDMI:

    # cat /sys/block/dewb?/dewb\_urls

disk size:

    # cat /sys/block/dewb?/dewb\_size

volume name:

    # cat /sys/block/dewb?/dewb\_name

Remains to be done :
--------------------

  * Fault tolerance when more server (reset or timeout).
  * Optimize sector sizes.
  * Put the device in non-rotational
  * Support DKMS
  * Rollback if connection lost
