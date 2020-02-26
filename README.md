Project Clearwater is backed by Metaswitch Networks.  We have discontinued active support for this project as of 1st December 2019.  The mailing list archive is available in GitHub.  All of the documentation and source code remains available for the community in GitHub.  Metaswitch’s Clearwater Core product, built on Project Clearwater, remains an active and successful commercial offering.  Please contact clearwater@metaswitch.com for more information. Note – this email is for commercial contacts with Metaswitch.  We are no longer offering support for Project Clearwater via this contact.

# Astaire

## Active Resync for Memcached Clusters

Astaire pro-actively resynchronises data across a cluster of `Memcached` nodes, allowing for faster scale-up/scale-down.  Astaire works with the Project Clearwater `MemcachedStore` to create a dynamically scalable, geographically redundant, highly consistent transient data store.

Astaire is optional, the `MemcachedStore` implementation is capable of elastically scaling up/down without loss of data, but without Astaire, all the keys in the store have to be rewritten at least once before the resize can be called complete (and hence another resize can be started).  This means that resizing the cluster takes as long as the longest lived key in the store (potentially unbounded).

## How it works

`MemcachedStore` arranges the keys it is storing into a large number of "virtual buckets" (`vbuckets`) and allocates these `vbuckets` to available `Memcached` cluster members based on a deterministic algorithm (allowing each `MemcachedStore` instance to independently decide on the same allocation).  During a scaling operation, some of these `vbuckets` will be re-homed, either being moved onto the new servers or being moved off servers before they are terminated.  Without Astaire, `MemcachedStore` does these moves lazily, moving each key only when it is next written to the store.

Astaire uses `MemcachedStoreView` (a part of `MemcachedStore`) to calculate which `vbuckets` are being re-homed and then uses the newly added (in v1.6) `Memcached TAP protocol` to stream the affected keys off their old home and to inject them into their new home.  By taking advantage of `Memcached`'s built in consistency primitives and the work already done in `MemcachedStore` to deal with data-contention between clients in a large cluster, Astaire is able to stream the data into the correct new homes at close to line speed with no loss of data integrity.

If you want to run a large Clearwater deployment (or any large `MemcachedStore`-based cluster), we strongly recommend taking advantage of Astaire to allow quicker resizing operations, especially in orchestrated environments where long waits may cause wide-reaching slowdowns.

## Using Astaire

Astaire is very easy to use, and integrates into the standard resizing algorithm for a `MemcachedStore`-based cluster:

1. Update the `/etc/clearwater/cluster_settings` file to contain `servers` and `new_servers` lines on each node.
1. Reload the `MemcachedStore` (to pick up those changes) on each node.
1. Run `sudo service astaire reload` on each node in the cluster.
1. Run `sudo service astaire wait-sync` on each node (this will wait until the resynchronization has completed).
1. Update `/etc/clearwater/cluster_settings` file to only list the new `servers` list.
1. Reload `MemcachedStore` to complete the resize.
1. If you were scaling down your cluster, you may destroy the extra nodes safely now.

## SNMP Statistics

Astaire can produce SNMP statistics while it is processing a resynchronization, to enable these statistics, install the `clearwater-snmp-handler-astaire` package and then use your favorite SNMP client to query the Astaire-related statistics listed in [PROJECT-CLEARWATER-MIB](https://raw.githubusercontent.com/Metaswitch/clearwater-snmp-handlers/master/PROJECT-CLEARWATER-MIB).

By tracking these statistics, an orchestrator can avoid having to rely on `wait-sync` to determine when a resize operation is safe to complete.  To do this, the orchestrator should track the `astaireBucketsNeedingResync` statistic and wait for it to return to 0.  This is effectively what `wait-sync` does under the covers.

## Diagnostics

Astaire will produce standard Clearwater logs in `/var/log/astaire/astaire_current.log` and will produce problem determination logs to syslog in the event of major events occurring.

Astaire can also report certain state changes over SNMP INFORMs.  To see the list of alarms that are currently implemented, see <https://github.com/Metaswitch/cpp-common/blob/master/src/alarmdefinition.cpp>.  To enable alarm generation, add `snmp_ip=<ip address>` to `/etc/clearwater/config` and install `clearwater-snmp-handler-alarm`.  SNMP alarms will then be sent to the provided IP address.

## Throttling

Astaire is intended to run in the background and not interfere with the business logic of the node it runs on. It is therefore CPU throttled to prevent it from stealing too much CPU from other processes on the node. This is done by the `astaire-throttle` service. This service is installed alongside Astaire and is run automatically.

By default the throttling service limits Astaire to 5% of the total CPU resource on the node. To change this limit, set the `astaire_cpu_limit_percentage` option in `/etc/clearwater/config` and run `sudo restart astaire-throttle`. Note that this is an advanced setting and should be used with caution - setting the limit too high can cause disruption to other services on the node.
